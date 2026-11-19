#import "ios_file_dialog.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

UIViewController *iron_ios_view_controller(void); // ios_system.m, the canvas view controller (picks import files)
UIViewController *iron_ios_presenter(void);

void IOSFileDialogOpen() {
	// A new picker each time: one presented again after a dismissal can come up empty (no locations, no files)
	// UTTypeFolder lets a whole material folder be picked; multiple selection imports several at once
	UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData, UTTypeFolder ]
	                                                                                                     asCopy:NO];
	picker.delegate                = (id<UIDocumentPickerDelegate>)iron_ios_view_controller();
	picker.allowsMultipleSelection = YES;
	picker.modalPresentationStyle  = UIModalPresentationFormSheet;
	[iron_ios_presenter() presentViewController:picker animated:YES completion:nil];
}

wchar_t *IOSFileDialogSave() {
	NSArray  *paths              = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	NSString *documentsDirectory = [paths objectAtIndex:0];
	NSString *fileName           = @"/untitled";
	NSString *filePath           = [documentsDirectory stringByAppendingString:fileName];
	return (wchar_t *)[filePath cStringUsingEncoding:NSUTF32LittleEndianStringEncoding];
}

void IOSDeleteFile(const char *path) {
	NSError  *error  = nil;
	NSString *nspath = [NSString stringWithUTF8String:path];
	[[NSFileManager defaultManager] removeItemAtPath:nspath error:&error];
}
