// iron_folder.h on iOS: folders on network shares (SMB) need file coordination and security scoped access

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <iron_folder.h>
#include <iron_system.h>
#include <stdlib.h>
#include <string.h>

static NSURL *iron_folder_root = nil; // Accessed until the app quits

static NSString *iron_folder_bookmark_path(void) {
	return [[NSString stringWithUTF8String:iron_internal_save_path()] stringByAppendingPathComponent:@"materials_folder.bookmark"];
}

static NSURL *iron_folder_url(const char *path) {
	return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
}

iron_dir_entry_t *iron_folder_list(const char *path, int *count) {
	@autoreleasepool {
		*count                       = 0;
		__block NSArray<NSURL *> *items = nil;
		NSError                   *coord_err = nil;
		NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
		[coordinator coordinateReadingItemAtURL:iron_folder_url(path)
		                                options:NSFileCoordinatorReadingWithoutChanges
		                                  error:&coord_err
		                             byAccessor:^(NSURL *url) {
			                             items = [[NSFileManager defaultManager] contentsOfDirectoryAtURL:url
			                                                                  includingPropertiesForKeys:@[ NSURLIsDirectoryKey, NSURLContentModificationDateKey ]
			                                                                                     options:NSDirectoryEnumerationSkipsHiddenFiles
			                                                                                       error:nil];
		                             }];
		if (items == nil) {
			return NULL;
		}
		iron_dir_entry_t *entries = malloc(sizeof(iron_dir_entry_t) * (items.count > 0 ? items.count : 1));
		for (NSURL *item in items) {
			NSNumber *is_dir = nil;
			NSDate   *date   = nil;
			[item getResourceValue:&is_dir forKey:NSURLIsDirectoryKey error:nil];
			[item getResourceValue:&date forKey:NSURLContentModificationDateKey error:nil];
			entries[*count].name   = strdup(item.lastPathComponent.UTF8String);
			entries[*count].is_dir = is_dir.boolValue;
			entries[*count].mtime  = date != nil ? date.timeIntervalSince1970 : 0;
			(*count)++;
		}
		return entries;
	}
}

void iron_folder_list_free(iron_dir_entry_t *entries, int count) {
	if (entries == NULL) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		free(entries[i].name);
	}
	free(entries);
}

double iron_folder_mtime(const char *path) {
	@autoreleasepool {
		NSDate *date = nil;
		[iron_folder_url(path) getResourceValue:&date forKey:NSURLContentModificationDateKey error:nil];
		return date != nil ? date.timeIntervalSince1970 : 0;
	}
}

uint8_t *iron_folder_read(const char *path, size_t *size) {
	@autoreleasepool {
		__block NSData *data      = nil;
		NSError        *coord_err = nil;
		NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
		[coordinator coordinateReadingItemAtURL:iron_folder_url(path)
		                                options:NSFileCoordinatorReadingWithoutChanges
		                                  error:&coord_err
		                             byAccessor:^(NSURL *url) {
			                             data = [NSData dataWithContentsOfURL:url options:0 error:nil];
		                             }];
		if (data == nil) {
			return NULL;
		}
		uint8_t *bytes = malloc(data.length > 0 ? data.length : 1);
		memcpy(bytes, data.bytes, data.length);
		*size = data.length;
		return bytes;
	}
}

bool iron_folder_write(const char *path, const void *bytes, size_t size) {
	@autoreleasepool {
		__block BOOL     ok        = NO;
		__block NSError *write_err = nil;
		NSError         *coord_err = nil;
		NSData          *data      = [NSData dataWithBytes:bytes length:size];
		NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
		[coordinator coordinateWritingItemAtURL:iron_folder_url(path)
		                                options:NSFileCoordinatorWritingForReplacing
		                                  error:&coord_err
		                             byAccessor:^(NSURL *url) {
			                             // Not atomic: the temporary file plus rename does not work on every share
			                             ok = [data writeToURL:url options:0 error:&write_err];
		                             }];
		if (!ok) {
			// A file changed on the server behind the share's back leaves a stale handle: remove it and retry once
			NSLog(@"[folder] write failed %s coord=%@ write=%@, retrying", path, coord_err, write_err);
			[coordinator coordinateWritingItemAtURL:iron_folder_url(path)
			                                options:NSFileCoordinatorWritingForDeleting
			                                  error:nil
			                             byAccessor:^(NSURL *url) {
				                             [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
			                             }];
			write_err = nil;
			[coordinator coordinateWritingItemAtURL:iron_folder_url(path)
			                                options:NSFileCoordinatorWritingForReplacing
			                                  error:&coord_err
			                             byAccessor:^(NSURL *url) {
				                             ok = [data writeToURL:url options:0 error:&write_err];
			                             }];
			if (!ok) {
				NSLog(@"[folder] write failed again %s coord=%@ write=%@", path, coord_err, write_err);
			}
		}
		return ok;
	}
}

// Picker

@interface IronFolderPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) void (*done)(char *path);
@property(nonatomic, strong) UIDocumentPickerViewController *picker;
@end

static IronFolderPicker *iron_folder_picker = nil;

static void iron_folder_set_root(NSURL *url) {
	if (iron_folder_root != nil) {
		[iron_folder_root stopAccessingSecurityScopedResource];
	}
	iron_folder_root = url;
	if (url != nil) {
		[url startAccessingSecurityScopedResource];
	}
}

static void iron_folder_save_bookmark(NSURL *url) {
	NSError *err      = nil;
	NSData  *bookmark = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:&err];
	if (bookmark == nil || ![bookmark writeToFile:iron_folder_bookmark_path() atomically:YES]) {
		NSLog(@"[folder] bookmark not saved: %@", err);
	}
}

@implementation IronFolderPicker

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
	NSURL *url = urls.firstObject;
	if (url == nil) {
		self.done(NULL);
		return;
	}
	iron_folder_set_root(url);
	iron_folder_save_bookmark(url);
	self.done((char *)url.path.UTF8String);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
	self.done(NULL);
}

@end

void iron_folder_pick(void (*done)(char *path)) {
	if (iron_folder_picker == nil) {
		iron_folder_picker        = [[IronFolderPicker alloc] init];
		UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ] asCopy:NO];
		picker.allowsMultipleSelection         = NO;
		picker.modalPresentationStyle          = UIModalPresentationFormSheet;
		picker.delegate                        = iron_folder_picker;
		iron_folder_picker.picker              = picker;
	}
	iron_folder_picker.done = done;
	UIViewController *root  = [UIApplication sharedApplication].keyWindow.rootViewController;
	[root presentViewController:iron_folder_picker.picker animated:YES completion:nil];
}

char *iron_folder_restore(void) {
	@autoreleasepool {
		NSData *bookmark = [NSData dataWithContentsOfFile:iron_folder_bookmark_path()];
		if (bookmark == nil) {
			return NULL;
		}
		BOOL     stale = NO;
		NSError *err   = nil;
		NSURL   *url   = [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:&err];
		if (url == nil) {
			NSLog(@"[folder] bookmark not resolved: %@", err);
			return NULL;
		}
		iron_folder_set_root(url);
		if (stale) {
			iron_folder_save_bookmark(url);
		}
		return strdup(url.path.UTF8String);
	}
}

void iron_folder_forget(void) {
	iron_folder_set_root(nil);
	[[NSFileManager defaultManager] removeItemAtPath:iron_folder_bookmark_path() error:nil];
}
