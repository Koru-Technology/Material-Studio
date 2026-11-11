#include "ios_system.h"
#import <Foundation/Foundation.h>
#ifdef IRON_AUDIO
#import <AVFoundation/AVFoundation.h>
#endif
#import <UIKit/UIKit.h>
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <mach/mach_time.h>
#include <objc/runtime.h>
#include <wchar.h>

#define MAX_TOUCH_COUNT 10

static char              ios_title[1024];
static void             *touches[MAX_TOUCH_COUNT];
static int               backing_width;
static int               backing_height;
static bool              shift_down = false;
static bool              visible;
static MyView           *my_view;
static MyViewController *my_view_controller;
static bool              keyboard_shown = false;
static char              language[3];
static char              sysid[512];
static const char       *video_formats[]                 = {"mp4", NULL};
static void (*resize_callback)(int x, int y, void *data) = NULL;
static void *resize_callback_data                        = NULL;

void iron_internal_call_resize_callback(int width, int height) {
	if (resize_callback != NULL) {
		resize_callback(width, height, resize_callback_data);
	}
}

static int get_touch_index(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == touch) {
			return i;
		}
	}
	return -1;
}

static int add_touch(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == NULL) {
			touches[i] = touch;
			return i;
		}
	}
	return -1;
}

static int remove_touch(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == touch) {
			touches[i] = NULL;
			return i;
		}
	}
	return -1;
}

int iron_window_width() {
	return backing_width;
}

int iron_window_height() {
	return backing_height;
}

CAMetalLayer *get_metal_layer(void) {
	return [my_view metal_layer];
}

id get_metal_device(void) {
	return [my_view metal_device];
}

id get_metal_queue(void) {
	return [my_view metal_queue];
}

void iron_display_init(void) {}

iron_display_mode_t iron_display_current_mode(int display) {
	UIScreen           *screen = [UIScreen mainScreen];
	iron_display_mode_t dm;
	dm.width           = iron_window_width();
	dm.height          = iron_window_height();
	dm.frequency       = (int)screen.maximumFramesPerSecond;
	dm.bits_per_pixel  = 32;
	dm.pixels_per_inch = UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad ? 326.0f : 460.0f; // Approx
	return dm;
}

int iron_count_displays(void) {
	return 1;
}

int iron_primary_display(void) {
	return 0;
}

bool iron_mouse_can_lock(void) {
	return false;
}

void iron_mouse_show(void) {}
void iron_mouse_hide(void) {}
void iron_mouse_set_position(int x, int y) {}
void iron_mouse_get_position(int *x, int *y) {}
void iron_mouse_set_cursor(iron_cursor_t cursor_index) {}

bool with_autoreleasepool(bool (*f)(void)) {
	@autoreleasepool {
		return f();
	}
}

const char *iron_get_resource_path(void) {
	return [[[NSBundle mainBundle] resourcePath] cStringUsingEncoding:1];
}

bool iron_internal_handle_messages(void) {
	SInt32 result;
	do {
		result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE);
	} while (result == kCFRunLoopRunHandledSource);
	return true;
}

void iron_set_keep_screen_on(bool on) {}

void iron_keyboard_show(void) {
	keyboard_shown = true;
	[my_view show_keyboard];
}

void iron_keyboard_hide(void) {
	keyboard_shown = false;
	[my_view hide_keyboard];
}

bool iron_keyboard_active(void) {
	return keyboard_shown;
}

void iron_load_url(const char *url) {
	NSURL *nsURL = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
	[[UIApplication sharedApplication] openURL:nsURL options:@{} completionHandler:nil];
}

const char *iron_language(void) {
	NSString   *nsstr = [[NSLocale preferredLanguages] objectAtIndex:0];
	const char *lang  = [nsstr UTF8String];
	language[0]       = lang[0];
	language[1]       = lang[1];
	language[2]       = 0;
	return language;
}

void iron_internal_shutdown(void) {}

void iron_init(iron_window_options_t *win) {
	gpu_init(win->depth_bits, true);
}

const char *iron_system_id(void) {
	const char *name     = [[[UIDevice currentDevice] name] UTF8String];
	const char *vendorId = [[[[UIDevice currentDevice] identifierForVendor] UUIDString] UTF8String];
	strcpy(sysid, name);
	strcat(sysid, "-");
	strcat(sysid, vendorId);
	return sysid;
}

const char *iron_internal_save_path(void) {
	NSArray  *paths        = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
	NSString *resolvedPath = [paths objectAtIndex:0];
	NSString *appName      = [NSString stringWithUTF8String:iron_application_name()];
	resolvedPath           = [resolvedPath stringByAppendingPathComponent:appName];
	NSFileManager *fileMgr = [[NSFileManager alloc] init];
	NSError       *error;
	[fileMgr createDirectoryAtPath:resolvedPath withIntermediateDirectories:YES attributes:nil error:&error];
	resolvedPath = [resolvedPath stringByAppendingString:@"/"];
	return [resolvedPath cStringUsingEncoding:1];
}

const char **iron_video_formats(void) {
	return video_formats;
}

double iron_frequency(void) {
	mach_timebase_info_data_t info;
	mach_timebase_info(&info);
	return (double)info.denom / (double)info.numer / 1e-9;
}

uint64_t iron_timestamp(void) {
	uint64_t time = mach_absolute_time();
	return time;
}

int main(int argc, char *argv[]) {
	int res = 0;
	@autoreleasepool {
		[IronSceneDelegate description]; // otherwise removed by the linker
		res = UIApplicationMain(argc, argv, nil, nil);
	}
	return res;
}

int iron_window_x() {
	return 0;
}

int iron_window_y() {
	return 0;
}

void iron_window_resize(int width, int height) {}
void iron_window_move(int x, int y) {}
void iron_window_change_mode(iron_window_mode_t mode) {}
void iron_window_destroy() {}
void iron_window_show() {}
void iron_window_hide() {}
void iron_window_create(iron_window_options_t *win) {}

void iron_window_set_title(const char *title) {
	strcpy(ios_title, title);
}

void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {
	resize_callback      = callback;
	resize_callback_data = data;
}

void iron_window_set_close_callback(bool (*callback)(void *), void *data) {}

iron_window_mode_t iron_window_get_mode() {
	return IRON_WINDOW_MODE_FULLSCREEN;
}

int iron_window_display() {
	return 0;
}

// Pencil force as 0..1, a pencil without pressure (usb-c) reports full pressure
static float ios_pencil_pressure(UITouch *touch) {
	if (touch.maximumPossibleForce <= 0.0) {
		return 1.0;
	}
	float p = touch.force / touch.maximumPossibleForce;
	return p < 0.0001 ? 0.0001 : (p > 1.0 ? 1.0 : p); // Above 0, a pressure of 0 is hover
}

@implementation MyView

+ (Class)layerClass {
	return [CAMetalLayer class];
}

- (void)hoverGesture:(UIHoverGestureRecognizer *)recognizer {
	CGPoint point = [recognizer locationInView:self];
	float   x     = point.x * self.contentScaleFactor;
	float   y     = point.y * self.contentScaleFactor;
	iron_internal_pen_trigger_move(x, y, 0.0); // Pencil hover
}

- (id)initWithFrame:(CGRect)frame {
	self                    = [super initWithFrame:(CGRect)frame];
	self.contentScaleFactor = [UIScreen mainScreen].scale;
	backing_width           = frame.size.width * self.contentScaleFactor;
	backing_height          = frame.size.height * self.contentScaleFactor;

	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		touches[i] = NULL;
	}

	device  = MTLCreateSystemDefaultDevice();
	queue   = [device newCommandQueue];
	library = [device newDefaultLibrary];

	CAMetalLayer *layer   = (CAMetalLayer *)self.layer;
	layer.device          = device;
	layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = YES;
	layer.opaque          = YES;
	layer.backgroundColor = nil;

	[self addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(hoverGesture:)]];
	return self;
}

- (void)layoutSubviews {
	backing_width  = self.frame.size.width * self.contentScaleFactor;
	backing_height = self.frame.size.height * self.contentScaleFactor;
	gpu_resize(backing_width, backing_height);
	iron_internal_call_resize_callback(backing_width, backing_height);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = get_touch_index((__bridge void *)touch);
		if (index == -1) {
			index = add_touch((__bridge void *)touch);
		}
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_press(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_start(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_press(x, y, ios_pencil_pressure(touch));
			}
		}
	}
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = get_touch_index((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_move(x, y);
			}
			iron_internal_surface_trigger_move(index, x, y);
			if (touch.type == UITouchTypePencil) {
				if (!(touch.estimatedProperties & UITouchPropertyForce)) {
					// usb-c pencil with no pressure
					iron_internal_pen_trigger_move(x, y, ios_pencil_pressure(touch));
				}
			}
		}
	}
}

- (void)touchesEstimatedPropertiesUpdated:(NSSet<UITouch *> *)touches {
	for (UITouch *touch in touches) {
		if (touch.type == UITouchTypePencil) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			// pencil with pressure
			iron_internal_pen_trigger_move(x, y, ios_pencil_pressure(touch));
		}
	}
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = remove_touch((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_release(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_end(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_release(x, y, 0.0);
			}
		}
	}
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = remove_touch((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_release(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_end(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_release(x, y, 0.0);
			}
		}
	}
}

- (void)show_keyboard {
	[self becomeFirstResponder];
}

- (void)hide_keyboard {
	[self resignFirstResponder];
}

- (BOOL)hasText {
	return YES;
}

- (void)insertText:(NSString *)text {
	if ([text length] == 1) {
		unichar ch = [text characterAtIndex:[text length] - 1];
		if (ch == 8212) {
			ch = '_';
		}
		if (ch == L'\n') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_RETURN);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_RETURN);
			return;
		}
		if (ch == L'.') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_PERIOD);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_PERIOD);
		}
		else if (ch == L'%') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_PERCENT);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_PERCENT);
		}
		else if (ch == L'(') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_OPEN_PAREN);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_OPEN_PAREN);
		}
		else if (ch == L'&') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_AMPERSAND);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_AMPERSAND);
		}
		else if (ch == L'$') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_DOLLAR);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_DOLLAR);
		}
		else if (ch == L'#') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_HASH);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_HASH);
		}
		else if (ch >= L'a' && ch <= L'z') {
			if (shift_down) {
				iron_internal_keyboard_trigger_key_up(KEY_CODE_SHIFT);
				shift_down = false;
			}
			iron_internal_keyboard_trigger_key_down(ch + KEY_CODE_A - L'a');
			iron_internal_keyboard_trigger_key_up(ch + KEY_CODE_A - L'a');
		}
		else {
			if (!shift_down) {
				iron_internal_keyboard_trigger_key_down(KEY_CODE_SHIFT);
				shift_down = true;
			}
			iron_internal_keyboard_trigger_key_down(ch + KEY_CODE_A - L'A');
			iron_internal_keyboard_trigger_key_up(ch + KEY_CODE_A - L'A');
		}
		iron_internal_keyboard_trigger_key_press(ch);
	}
}

- (void)deleteBackward {
	iron_internal_keyboard_trigger_key_down(KEY_CODE_BACKSPACE);
	iron_internal_keyboard_trigger_key_up(KEY_CODE_BACKSPACE);
}

- (BOOL)canBecomeFirstResponder {
	return YES;
}

- (void)onKeyboardHide:(NSNotification *)notification {
	iron_keyboard_hide();
}

- (CAMetalLayer *)metal_layer {
	return (CAMetalLayer *)self.layer;
}

- (id<MTLDevice>)metal_device {
	return device;
}

- (id<MTLCommandQueue>)metal_queue {
	return queue;
}

@end

@implementation MyViewController

- (void)loadView {
	visible   = true;
	self.view = my_view = [[MyView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	[self.view addInteraction:[[UIDropInteraction alloc] initWithDelegate:self]];
	[self setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
}

- (void)setVisible:(BOOL)value {
	visible = value;
}

// Log import problems to both the system log and stderr (visible with devicectl --console)
#define IMPORT_LOG(...) do { NSString *_m = [NSString stringWithFormat:__VA_ARGS__]; NSLog(@"%@", _m); fprintf(stderr, "%s\n", _m.UTF8String); fflush(stderr); } while (0)

// Copy one file through a file coordinator, so file-provider / network (SMB) files get downloaded first.
static BOOL importCopyFile(NSURL *src, NSString *dst) {
	__block BOOL     ok       = NO;
	__block NSError *copy_err = nil;
	NSError         *coord_err = nil;
	NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
	[coordinator coordinateReadingItemAtURL:src
	                                options:NSFileCoordinatorReadingWithoutChanges
	                                  error:&coord_err
	                             byAccessor:^(NSURL *new_url) {
		                             [[NSFileManager defaultManager] removeItemAtPath:dst error:nil];
		                             // Read only the file's contents: a full copy also reads the file's security info (ACL),
		                             // which the iPadOS SMB client refuses for files on the share
		                             NSData *data = [NSData dataWithContentsOfURL:new_url options:0 error:&copy_err];
		                             ok           = data != nil && [data writeToFile:dst options:NSDataWritingAtomic error:&copy_err];
	                             }];
	if (!ok) {
		IMPORT_LOG(@"[import] copy failed %@ -> %@ coord=%@ copy=%@", src, dst, coord_err, copy_err);
	}
	return ok;
}

// Copy a file or a whole folder tree; folders are walked file by file instead of one bulk copy,
// because a bulk copy of a not-yet-downloaded provider folder fails silently.
static BOOL importCopyTree(NSURL *src, NSString *dst) {
	NSNumber *is_dir = nil;
	[src getResourceValue:&is_dir forKey:NSURLIsDirectoryKey error:nil];
	if (![is_dir boolValue] && !src.hasDirectoryPath) {
		return importCopyFile(src, dst);
	}
	[[NSFileManager defaultManager] createDirectoryAtPath:dst withIntermediateDirectories:YES attributes:nil error:nil];
	__block NSArray<NSURL *> *children = nil;
	__block NSError          *list_err = nil;
	NSError                  *coord_err = nil;
	NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
	[coordinator coordinateReadingItemAtURL:src
	                                options:NSFileCoordinatorReadingWithoutChanges
	                                  error:&coord_err
	                             byAccessor:^(NSURL *new_url) {
		                             children = [[NSFileManager defaultManager] contentsOfDirectoryAtURL:new_url
		                                                                      includingPropertiesForKeys:@[ NSURLIsDirectoryKey ]
		                                                                                         options:NSDirectoryEnumerationSkipsHiddenFiles
		                                                                                           error:&list_err];
	                             }];
	if (children == nil) {
		IMPORT_LOG(@"[import] cannot list %@ coord=%@ list=%@", src, coord_err, list_err);
		return NO;
	}
	BOOL any = NO;
	for (NSURL *child in children) {
		any |= importCopyTree(child, [dst stringByAppendingPathComponent:child.lastPathComponent]);
	}
	return any;
}

// Copies the file into the documents folder, returns the copy or nil
static NSString *importCopy(NSURL *url) {
	if (url == nil) {
		IMPORT_LOG(@"[import] no URL received");
		return nil;
	}
	NSArray  *paths       = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	NSString *folder_name = [NSString stringWithUTF8String:ios_title];
	NSString *file_path   = [[paths objectAtIndex:0] stringByAppendingPathComponent:folder_name];
	if (![[NSFileManager defaultManager] fileExistsAtPath:file_path]) {
		[[NSFileManager defaultManager] createDirectoryAtPath:file_path withIntermediateDirectories:YES attributes:nil error:nil];
	}
	NSString *suggestedName = url.lastPathComponent;
	file_path               = [file_path stringByAppendingPathComponent:suggestedName];
	BOOL scoped             = [url startAccessingSecurityScopedResource];
	[[NSFileManager defaultManager] removeItemAtPath:file_path error:nil];
	BOOL ok = importCopyTree(url, file_path);
	if (scoped) {
		[url stopAccessingSecurityScopedResource];
	}
	if (!ok) {
		IMPORT_LOG(@"[import] import failed %@ -> %@", url, file_path);
		return nil;
	}
	// Files shared by other apps arrive in Documents/Inbox, the copy is kept instead
	if ([url.path containsString:@"/Documents/Inbox/"]) {
		[[NSFileManager defaultManager] removeItemAtURL:url error:nil];
	}
	return file_path;
}

void importFile(NSURL *url) {
	NSString *file_path = importCopy(url);
	if (file_path == nil) {
		return;
	}
	const char *cpath = [file_path cStringUsingEncoding:NSUTF8StringEncoding];
	iron_internal_drop_files_callback(cpath);
}

// A file shared with the app (Open in / Share sheet): copied off the main thread, opened on it
static void openSharedFile(NSURL *url) {
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		NSString *file_path = importCopy(url);
		if (file_path == nil) {
			return;
		}
		char *cpath = strdup([file_path cStringUsingEncoding:NSUTF8StringEncoding]);
		dispatch_async(dispatch_get_main_queue(), ^{
			iron_internal_open_file_callback(cpath);
			free(cpath);
		});
	});
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
	// Copy off the main thread: pulling folders from a network share can take a while
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		for (NSURL *url in urls) {
			importFile(url);
		}
	});
}

- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session {
	CGPoint point = [session locationInView:self.view];
	float   x     = point.x * my_view.contentScaleFactor;
	float   y     = point.y * my_view.contentScaleFactor;
	iron_internal_mouse_trigger_move(x, y);
	iron_internal_surface_trigger_move(0, x, y);
	for (UIDragItem *item in session.items) {
		[item.itemProvider loadInPlaceFileRepresentationForTypeIdentifier:item.itemProvider.registeredTypeIdentifiers[0]
		                                                completionHandler:^(NSURL *_Nullable url, BOOL isInPlace, NSError *_Nullable error) {
			                                                if (url == nil) {
				                                                IMPORT_LOG(@"[import] drop failed for %@: %@", item.itemProvider.registeredTypeIdentifiers, error);
			                                                }
			                                                importFile(url);
		                                                }];
	}
}

- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session {
	return [[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy];
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
	return UIRectEdgeAll;
}

@end

@implementation IronSceneDelegate

- (void)mainLoop {
	@autoreleasepool {
		// Launch arguments (devicectl launch ... -- --script ~/Documents/x.c), "~/" is the app container
		NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
		int                  argc = (int)args.count;
		char               **argv = calloc(argc + 1, sizeof(char *));
		NSString            *home = NSHomeDirectory();
		for (int i = 0; i < argc; ++i) {
			NSString *a = args[i];
			if ([a hasPrefix:@"~/"]) {
				a = [home stringByAppendingPathComponent:[a substringFromIndex:2]];
			}
			argv[i] = strdup([a UTF8String]);
		}
		kickstart(argc, argv);
	}
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
#ifdef IRON_AUDIO
	AVAudioSession *sessionInstance = [AVAudioSession sharedInstance];
	NSError        *error;
	NSString       *category = AVAudioSessionCategoryAmbient;
	[sessionInstance setCategory:category error:&error];
#endif

	UIWindowScene *windowScene = (UIWindowScene *)scene;
	self.window                = [[UIWindow alloc] initWithWindowScene:windowScene];
	self.window.frame          = windowScene.coordinateSpace.bounds;
	[self.window setBackgroundColor:[UIColor blackColor]];

	my_view_controller                           = [[MyViewController alloc] init];
	my_view_controller.view.multipleTouchEnabled = YES;
	[self.window setRootViewController:my_view_controller];
	[self.window makeKeyAndVisible];
	[my_view_controller setVisible:YES];
	[self performSelectorOnMainThread:@selector(mainLoop) withObject:nil waitUntilDone:NO];

	// Launched by sharing a file with the app
	for (UIOpenURLContext *context in connectionOptions.URLContexts) {
		openSharedFile(context.URL);
	}
}

- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
	for (UIOpenURLContext *context in URLContexts) {
		openSharedFile(context.URL);
	}
}

- (void)sceneDidDisconnect:(UIScene *)scene {
	iron_internal_shutdown_callback();
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
}

- (void)sceneWillResignActive:(UIScene *)scene {
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
	iron_internal_foreground_callback();
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
	iron_internal_background_callback();
}

@end

#ifdef WITH_GAMEPAD

const char *iron_gamepad_vendor(int gamepad) {
	return "nobody";
}

const char *iron_gamepad_product_name(int gamepad) {
	return "none";
}

bool iron_gamepad_connected(int num) {
	return true;
}

void iron_gamepad_rumble(int gamepad, float left, float right) {}

#endif

bool _save_and_quit_callback_internal() {
	return false;
}

volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

void iron_exec_async(const char *path, char *argv[]) {}
