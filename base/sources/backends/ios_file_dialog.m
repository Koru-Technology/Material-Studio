#import "ios_file_dialog.h"

#import <UIKit/UIKit.h>

static UIDocumentPickerViewController *documentPicker = nil;

UIViewController *iron_ios_view_controller(void); // ios_system.m, the canvas view controller (picks import files)

void IOSFileDialogOpen() {
	UIViewController<UIDocumentPickerDelegate> *myViewController = (UIViewController<UIDocumentPickerDelegate> *)iron_ios_view_controller();
	if (documentPicker == nil) {
		// public.folder lets a whole material folder be picked; multiple selection imports several at once
		documentPicker          = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[ @"public.data", @"public.folder" ]
                                                                                     inMode:UIDocumentPickerModeOpen];
		documentPicker.delegate = myViewController;
		documentPicker.allowsMultipleSelection = YES;
		documentPicker.modalPresentationStyle = UIModalPresentationFormSheet;
	}
	[myViewController presentViewController:documentPicker animated:YES completion:nil];
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
