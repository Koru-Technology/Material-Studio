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
static KeyInputView     *key_input_view = nil;
static float             pointer_x      = 0.0; // Last mouse position sent, for the trackpad gestures
static float             pointer_y      = 0.0;
static int               pointer_button = 0;   // Button of the trackpad or mouse click being held
static char              language[3];
static char              sysid[512];
static const char       *video_formats[]                 = {"mp4", NULL};
static void (*resize_callback)(int x, int y, void *data) = NULL;
static void *resize_callback_data                        = NULL;
static void  pencil_flush(bool all);
static bool  ios_presses(NSSet<UIPress *> *presses, bool down, bool typing);

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

static void end_finger_touches(float x, float y) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] != NULL) {
			iron_internal_surface_trigger_touch_end(i, x, y);
			touches[i] = NULL;
		}
	}
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
	pencil_flush(false); // Samples whose final force did not arrive in time
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

static int pencil_preference(UIPencilPreferredAction a) {
	switch (a) {
	case UIPencilPreferredActionSwitchEraser:
		return IRON_PENCIL_PREFERS_SWITCH_ERASER;
	case UIPencilPreferredActionSwitchPrevious:
		return IRON_PENCIL_PREFERS_SWITCH_PREVIOUS;
	case UIPencilPreferredActionShowColorPalette:
		return IRON_PENCIL_PREFERS_COLOR_PALETTE;
	case UIPencilPreferredActionShowInkAttributes:
		return IRON_PENCIL_PREFERS_INK_ATTRIBUTES;
	default:
		break;
	}
	if (@available(iOS 17.5, *)) {
		if (a == UIPencilPreferredActionShowContextualPalette) {
			return IRON_PENCIL_PREFERS_CONTEXTUAL_PALETTE;
		}
	}
	return IRON_PENCIL_PREFERS_IGNORE;
}

int iron_pencil_preferred_action(int gesture) {
	if (gesture == IRON_PENCIL_GESTURE_SQUEEZE) {
		if (@available(iOS 17.5, *)) {
			return pencil_preference(UIPencilInteraction.preferredSqueezeAction);
		}
		return IRON_PENCIL_PREFERS_IGNORE;
	}
	return pencil_preference(UIPencilInteraction.preferredTapAction);
}

void iron_haptic_tick(float x, float y) {
	if (@available(iOS 17.5, *)) {
		static UICanvasFeedbackGenerator *generator = nil;
		if (generator == nil) {
			generator = [UICanvasFeedbackGenerator feedbackGeneratorForView:my_view];
		}
		float s = my_view.contentScaleFactor;
		[generator alignmentOccurredAtLocation:CGPointMake(x / s, y / s)];
	}
}

// Pencil force as 0..1, a pencil without pressure (usb-c) reports full pressure
static float ios_pencil_pressure(UITouch *touch) {
	if (touch.maximumPossibleForce <= 0.0) {
		return 1.0;
	}
	float p = touch.force / touch.maximumPossibleForce;
	return p < 0.0001 ? 0.0001 : (p > 1.0 ? 1.0 : p); // Above 0, a pressure of 0 is hover
}

// The pencil has its own slot, it never counts as a finger. Fingers landing while it draws are ignored.
static void *pencil_touch = NULL;
static int   mouse_owner  = 0; // 0 nobody, 1 the first finger, 2 the pencil

// Pencil samples wait here until their force is final (it is estimated first and refined a few ms later),
// so the stroke does not start with a wrong pressure. A sample is sent anyway once it waited too long.
typedef struct {
	float x;
	float y;
	float pressure;
	float altitude;
	float azimuth;
	float roll;
	double time;
	long   update_index; // -1 once final
} pencil_sample_t;

#define PENCIL_PENDING_MAX 256
#define PENCIL_WAIT_MAX    0.05
static pencil_sample_t pencil_pending[PENCIL_PENDING_MAX];
static int             pencil_pending_count = 0;

static void pencil_emit(pencil_sample_t *e) {
	iron_internal_pen_trigger_sample(e->x, e->y, e->pressure, e->altitude, e->azimuth, e->roll, e->time);
	iron_internal_pen_trigger_move((int)e->x, (int)e->y, e->pressure);
	if (mouse_owner == 2) {
		iron_internal_mouse_trigger_move((int)e->x, (int)e->y);
		iron_internal_surface_trigger_move(0, (int)e->x, (int)e->y); // The ui follows touch 0 on ios, e.g. dragging a slider
	}
}

static void pencil_flush(bool all) {
	double now = CACurrentMediaTime();
	int    n   = 0;
	while (n < pencil_pending_count) {
		pencil_sample_t *e = &pencil_pending[n];
		if (!all && e->update_index >= 0 && now - e->time < PENCIL_WAIT_MAX) {
			break; // Samples stay in order, the next ones wait too
		}
		pencil_emit(e);
		n++;
	}
	if (n > 0) {
		memmove(pencil_pending, pencil_pending + n, sizeof(pencil_sample_t) * (pencil_pending_count - n));
		pencil_pending_count -= n;
	}
}

static void pencil_read(UITouch *t, UIView *view, pencil_sample_t *e) {
	CGPoint p = [t preciseLocationInView:view];
	float   s = view.contentScaleFactor;
	e->x      = p.x * s;
	e->y      = p.y * s;
	e->pressure = ios_pencil_pressure(t);
	e->altitude = t.altitudeAngle;
	e->azimuth  = [t azimuthAngleInView:view];
	e->roll     = 0.0;
	if (@available(iOS 17.5, *)) {
		e->roll = t.rollAngle;
	}
	e->time = t.timestamp;
}

static void pencil_push(UITouch *t, UIView *view) {
	if (pencil_pending_count == PENCIL_PENDING_MAX) {
		pencil_flush(true);
	}
	pencil_sample_t *e = &pencil_pending[pencil_pending_count++];
	pencil_read(t, view, e);
	bool waiting    = (t.estimatedPropertiesExpectingUpdates & UITouchPropertyForce) && t.estimationUpdateIndex != nil;
	e->update_index = waiting ? t.estimationUpdateIndex.longValue : -1;
}

// Every sample since the last event, the pencil reports up to 240 per second
static void pencil_push_coalesced(UITouch *touch, UIEvent *event, UIView *view) {
	NSArray<UITouch *> *all = event != nil ? [event coalescedTouchesForTouch:touch] : nil;
	if (all == nil || all.count == 0) {
		pencil_push(touch, view);
		return;
	}
	for (UITouch *t in all) {
		pencil_push(t, view);
	}
}

static void pencil_update(UITouch *t, UIView *view) {
	if (t.estimationUpdateIndex == nil) {
		return;
	}
	long index = t.estimationUpdateIndex.longValue;
	for (int i = 0; i < pencil_pending_count; ++i) {
		pencil_sample_t *e = &pencil_pending[i];
		if (e->update_index == index) {
			float  x    = e->x;
			float  y    = e->y;
			double time = e->time;
			pencil_read(t, view, e);
			e->x    = x; // Only force and angles are refined
			e->y    = y;
			e->time = time;
			if (!(t.estimatedPropertiesExpectingUpdates & UITouchPropertyForce)) {
				e->update_index = -1;
			}
			return;
		}
	}
}

@implementation KeyInputView

- (BOOL)canBecomeFirstResponder {
	return YES;
}

- (BOOL)hasText {
	return YES;
}

- (void)insertText:(NSString *)text {
	[my_view insertText:text];
}

- (void)deleteBackward {
	[my_view deleteBackward];
}

- (UITextAutocorrectionType)autocorrectionType {
	return UITextAutocorrectionTypeNo;
}

- (UITextAutocapitalizationType)autocapitalizationType {
	return UITextAutocapitalizationTypeNone;
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	ios_presses(presses, true, true);
	[super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	ios_presses(presses, false, true);
	[super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	ios_presses(presses, false, true);
	[super pressesCancelled:presses withEvent:event];
}

@end

@implementation MyView

+ (Class)layerClass {
	return [CAMetalLayer class];
}

- (void)hoverGesture:(UIHoverGestureRecognizer *)recognizer {
	CGPoint point  = [recognizer locationInView:self];
	float   x      = point.x * self.contentScaleFactor;
	float   y      = point.y * self.contentScaleFactor;
	float   height = 0.5;
	bool    pencil = true;
	if (@available(iOS 16.1, *)) {
		height = recognizer.zOffset;
		pencil = height > 0.0; // Trackpad or mouse pointer otherwise
	}
	if (recognizer.state != UIGestureRecognizerStateBegan && recognizer.state != UIGestureRecognizerStateChanged) {
		if (pencil) {
			iron_internal_pen_trigger_hover(x, y, -1.0); // Lifted away, the brush cursor hides
		}
		return;
	}
	if (!pencil) {
		pointer_x = x;
		pointer_y = y;
		if (mouse_owner == 0) {
			iron_internal_mouse_trigger_move(x, y);
		}
		return;
	}
	iron_internal_pen_trigger_hover(x, y, height);
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)a shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)b {
	return YES; // Pinch and rotate together on the trackpad
}

- (void)scrollGesture:(UIPanGestureRecognizer *)recognizer {
	static bool panning = false;
	float       s       = self.contentScaleFactor;
	CGPoint     t       = [recognizer translationInView:self];
	[recognizer setTranslation:CGPointZero inView:self];
	bool zoom = (recognizer.modifierFlags & (UIKeyModifierCommand | UIKeyModifierControl)) != 0;
	if (recognizer.state == UIGestureRecognizerStateBegan && !zoom && mouse_owner == 0) {
		panning = true; // Middle button drag pans
		iron_internal_mouse_trigger_press(2, pointer_x, pointer_y);
	}
	else if (recognizer.state == UIGestureRecognizerStateChanged) {
		if (panning) {
			pointer_x += t.x * s;
			pointer_y += t.y * s;
			iron_internal_mouse_trigger_move(pointer_x, pointer_y);
		}
		else if (t.y != 0.0) {
			iron_internal_mouse_trigger_scroll(-t.y * s / 40.0);
		}
	}
	else if (recognizer.state != UIGestureRecognizerStateBegan && panning) {
		panning = false;
		iron_internal_mouse_trigger_release(2, pointer_x, pointer_y);
	}
}

- (void)pinchGesture:(UIPinchGestureRecognizer *)recognizer {
	static CGFloat last = 1.0;
	if (recognizer.state == UIGestureRecognizerStateBegan) {
		last = 1.0;
	}
	else if (recognizer.state == UIGestureRecognizerStateChanged) {
		CGFloat d = recognizer.scale - last;
		last      = recognizer.scale;
		iron_internal_mouse_trigger_scroll(-d * 10.0);
	}
}

- (void)rotationGesture:(UIRotationGestureRecognizer *)recognizer {
	static CGFloat last = 0.0;
	if (recognizer.state == UIGestureRecognizerStateBegan) {
		last = 0.0;
	}
	else if (recognizer.state == UIGestureRecognizerStateChanged) {
		CGFloat d = recognizer.rotation - last;
		last      = recognizer.rotation;
		iron_internal_mouse_trigger_twist(d);
	}
}

// The system pointer hides where the app draws its brush cursor
- (UIPointerRegion *)pointerInteraction:(UIPointerInteraction *)interaction
                       regionForRequest:(UIPointerRegionRequest *)request
                          defaultRegion:(UIPointerRegion *)defaultRegion {
	float s      = self.contentScaleFactor;
	bool  hidden = iron_internal_pointer_hidden(request.location.x * s, request.location.y * s);
	return [UIPointerRegion regionWithRect:CGRectMake(request.location.x - 1.0, request.location.y - 1.0, 2.0, 2.0) identifier:@(hidden)];
}

- (UIPointerStyle *)pointerInteraction:(UIPointerInteraction *)interaction styleForRegion:(UIPointerRegion *)region {
	return [(NSNumber *)region.identifier boolValue] ? [UIPointerStyle hiddenPointerStyle] : nil;
}

// Double tap and squeeze, the location is where the pencil hovers, or -1 when unknown
static void pencil_gesture(UIView *view, int gesture, bool hovering, CGPoint at) {
	float s = view.contentScaleFactor;
	iron_internal_pencil_gesture_callback(gesture, hovering ? at.x * s : -1.0, hovering ? at.y * s : -1.0);
}

- (void)pencilInteractionDidTap:(UIPencilInteraction *)interaction {
	pencil_gesture(self, IRON_PENCIL_GESTURE_DOUBLE_TAP, false, CGPointZero);
}

- (void)pencilInteraction:(UIPencilInteraction *)interaction didReceiveTap:(UIPencilInteractionTap *)tap API_AVAILABLE(ios(17.5)) {
	UIPencilHoverPose *pose = tap.hoverPose;
	pencil_gesture(self, IRON_PENCIL_GESTURE_DOUBLE_TAP, pose != nil, pose != nil ? pose.location : CGPointZero);
}

- (void)pencilInteraction:(UIPencilInteraction *)interaction didReceiveSqueeze:(UIPencilInteractionSqueeze *)squeeze API_AVAILABLE(ios(17.5)) {
	if (squeeze.phase != UIPencilInteractionPhaseEnded) {
		return;
	}
	UIPencilHoverPose *pose = squeeze.hoverPose;
	pencil_gesture(self, IRON_PENCIL_GESTURE_SQUEEZE, pose != nil, pose != nil ? pose.location : CGPointZero);
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
	UIPencilInteraction *pencil_interaction = [[UIPencilInteraction alloc] init];
	pencil_interaction.delegate             = self;
	[self addInteraction:pencil_interaction];

	key_input_view = [[KeyInputView alloc] initWithFrame:CGRectZero];
	[self addSubview:key_input_view];

	// Trackpad and mouse: two finger scroll pans (Cmd zooms), pinch zooms, rotate rolls the view.
	// Only indirect input, fingers on the screen stay with the touch handling.
	UIPanGestureRecognizer *scroll = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(scrollGesture:)];
	scroll.allowedScrollTypesMask  = UIScrollTypeMaskAll;
	scroll.allowedTouchTypes       = @[];
	scroll.cancelsTouchesInView    = NO;
	scroll.delegate                = self;
	[self addGestureRecognizer:scroll];
	UIPinchGestureRecognizer *pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinchGesture:)];
	pinch.allowedTouchTypes         = @[ @(UITouchTypeIndirectPointer) ];
	pinch.cancelsTouchesInView      = NO;
	pinch.delegate                  = self;
	[self addGestureRecognizer:pinch];
	UIRotationGestureRecognizer *rotation = [[UIRotationGestureRecognizer alloc] initWithTarget:self action:@selector(rotationGesture:)];
	rotation.allowedTouchTypes            = @[ @(UITouchTypeIndirectPointer) ];
	rotation.cancelsTouchesInView         = NO;
	rotation.delegate                     = self;
	[self addGestureRecognizer:rotation];
	[self addInteraction:[[UIPointerInteraction alloc] initWithDelegate:self]];
	[[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
	                                                  object:nil
	                                                   queue:[NSOperationQueue mainQueue]
	                                              usingBlock:^(NSNotification *note) {
		                                              iron_internal_memory_warning_callback();
	                                              }];
	return self;
}

- (void)layoutSubviews {
	backing_width  = self.frame.size.width * self.contentScaleFactor;
	backing_height = self.frame.size.height * self.contentScaleFactor;
	gpu_resize(backing_width, backing_height);
	iron_internal_call_resize_callback(backing_width, backing_height);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
	float sc = self.contentScaleFactor;
	for (UITouch *touch in touches) {
		CGPoint point = [touch locationInView:self];
		float   x     = point.x * sc;
		float   y     = point.y * sc;
		if (touch.type == UITouchTypePencil) {
			if (pencil_touch != NULL) {
				continue;
			}
			pencil_touch = (__bridge void *)touch;
			end_finger_touches(x, y); // Fingers resting on the screen stop counting, the pencil takes over the mouse
			if (mouse_owner == 1) {
				iron_internal_mouse_trigger_release(0, x, y);
			}
			mouse_owner          = 2;
			pencil_pending_count = 0;
			iron_internal_mouse_trigger_press(0, x, y);
			iron_internal_surface_trigger_touch_start(0, x, y); // Free while the pencil draws, fingers are ignored
			iron_internal_pen_trigger_press(x, y, ios_pencil_pressure(touch));
			pencil_push_coalesced(touch, event, self);
			pencil_flush(false);
			continue;
		}
		if (pencil_touch != NULL) {
			continue; // Palm or finger while drawing
		}
		int index = get_touch_index((__bridge void *)touch);
		if (index == -1) {
			index = add_touch((__bridge void *)touch);
		}
		if (index >= 0) {
			if (index == 0 && mouse_owner == 0) {
				mouse_owner    = 1;
				pointer_button = (event.buttonMask & UIEventButtonMaskSecondary) ? 1 : 0; // Right click on a trackpad or mouse
				pointer_x      = x;
				pointer_y      = y;
				iron_internal_mouse_trigger_press(pointer_button, x, y);
			}
			iron_internal_surface_trigger_touch_start(index, x, y);
		}
	}
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
	float sc = self.contentScaleFactor;
	for (UITouch *touch in touches) {
		if ((__bridge void *)touch == pencil_touch) {
			pencil_push_coalesced(touch, event, self);
			pencil_flush(false);
			UITouch *predicted = [[event predictedTouchesForTouch:touch] lastObject];
			if (predicted != nil) {
				CGPoint p = [predicted preciseLocationInView:self];
				iron_internal_pen_trigger_predicted(p.x * sc, p.y * sc);
			}
			continue;
		}
		int index = get_touch_index((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * sc;
			float   y     = point.y * sc;
			if (index == 0 && mouse_owner == 1) {
				pointer_x = x;
				pointer_y = y;
				iron_internal_mouse_trigger_move(x, y);
			}
			iron_internal_surface_trigger_move(index, x, y);
		}
	}
}

- (void)touchesEstimatedPropertiesUpdated:(NSSet<UITouch *> *)touches {
	for (UITouch *touch in touches) {
		if (touch.type == UITouchTypePencil) {
			pencil_update(touch, self);
		}
	}
	pencil_flush(false);
}

- (void)touch_finished:(UITouch *)touch event:(UIEvent *)event {
	float   sc    = self.contentScaleFactor;
	CGPoint point = [touch locationInView:self];
	float   x     = point.x * sc;
	float   y     = point.y * sc;
	if ((__bridge void *)touch == pencil_touch) {
		pencil_push_coalesced(touch, event, self);
		pencil_flush(true);
		pencil_touch = NULL;
		iron_internal_pen_trigger_release(x, y, 0.0); // The stroke keeps the last pressure
		if (mouse_owner == 2) {
			iron_internal_mouse_trigger_release(0, x, y);
			iron_internal_surface_trigger_touch_end(0, x, y);
			mouse_owner = 0;
		}
		return;
	}
	int index = remove_touch((__bridge void *)touch);
	if (index >= 0) {
		if (index == 0 && mouse_owner == 1) {
			iron_internal_mouse_trigger_release(pointer_button, x, y); // The mask is empty once released
			mouse_owner = 0;
		}
		iron_internal_surface_trigger_touch_end(index, x, y);
	}
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		[self touch_finished:touch event:event];
	}
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		[self touch_finished:touch event:event];
	}
}

- (void)show_keyboard {
	[key_input_view becomeFirstResponder];
}

- (void)hide_keyboard {
	[key_input_view resignFirstResponder];
	[self becomeFirstResponder]; // Hardware keys keep coming
}

- (BOOL)hasText {
	return YES;
}

// Hardware keyboard, by HID usage
static int ios_key_code(long usage) {
	if (usage >= 0x04 && usage <= 0x1D) {
		return KEY_CODE_A + (int)(usage - 0x04);
	}
	if (usage >= 0x1E && usage <= 0x26) {
		return KEY_CODE_1 + (int)(usage - 0x1E);
	}
	if (usage >= 0x3A && usage <= 0x45) {
		return KEY_CODE_F1 + (int)(usage - 0x3A);
	}
	if (usage >= 0x59 && usage <= 0x61) {
		return KEY_CODE_NUMPAD_1 + (int)(usage - 0x59);
	}
	switch (usage) {
	case 0x27:
		return KEY_CODE_0;
	case 0x62:
		return KEY_CODE_NUMPAD_0;
	case 0x28:
	case 0x58:
		return KEY_CODE_RETURN;
	case 0x29:
		return KEY_CODE_ESCAPE;
	case 0x2A:
		return KEY_CODE_BACKSPACE;
	case 0x2B:
		return KEY_CODE_TAB;
	case 0x2C:
		return KEY_CODE_SPACE;
	case 0x2D:
		return KEY_CODE_HYPHEN_MINUS;
	case 0x2E:
		return KEY_CODE_EQUALS;
	case 0x2F:
		return KEY_CODE_OPEN_BRACKET;
	case 0x30:
		return KEY_CODE_CLOSE_BRACKET;
	case 0x31:
		return KEY_CODE_BACK_SLASH;
	case 0x33:
		return KEY_CODE_SEMICOLON;
	case 0x34:
		return KEY_CODE_QUOTE;
	case 0x35:
		return KEY_CODE_BACK_QUOTE;
	case 0x36:
		return KEY_CODE_COMMA;
	case 0x37:
		return KEY_CODE_PERIOD;
	case 0x38:
		return KEY_CODE_SLASH;
	case 0x39:
		return KEY_CODE_CAPS_LOCK;
	case 0x4A:
		return KEY_CODE_HOME;
	case 0x4B:
		return KEY_CODE_PAGE_UP;
	case 0x4C:
		return KEY_CODE_DELETE;
	case 0x4D:
		return KEY_CODE_END;
	case 0x4E:
		return KEY_CODE_PAGE_DOWN;
	case 0x4F:
		return KEY_CODE_RIGHT;
	case 0x50:
		return KEY_CODE_LEFT;
	case 0x51:
		return KEY_CODE_DOWN;
	case 0x52:
		return KEY_CODE_UP;
	case 0xE0:
	case 0xE4:
		return KEY_CODE_CONTROL;
	case 0xE1:
	case 0xE5:
		return KEY_CODE_SHIFT;
	case 0xE2:
	case 0xE6:
		return KEY_CODE_ALT;
	case 0xE3:
	case 0xE7:
		return KEY_CODE_META; // Cmd, works as control
	default:
		return KEY_CODE_UNKNOWN;
	}
}

// While text is typed, characters, return and backspace arrive through insertText and deleteBackward.
// The keys sent here are the others: arrows, escape, modifiers and any key held with Cmd or Ctrl (copy, paste, undo).
static bool ios_key_for_text(UIKey *key, int code) {
	if (code == KEY_CODE_LEFT || code == KEY_CODE_RIGHT || code == KEY_CODE_UP || code == KEY_CODE_DOWN || code == KEY_CODE_ESCAPE ||
	    code == KEY_CODE_DELETE || code == KEY_CODE_HOME || code == KEY_CODE_END || code == KEY_CODE_META || code == KEY_CODE_CONTROL ||
	    code == KEY_CODE_ALT) {
		return true;
	}
	return (key.modifierFlags & (UIKeyModifierCommand | UIKeyModifierControl)) != 0;
}

// Returns whether the presses were all taken
static bool ios_presses(NSSet<UIPress *> *presses, bool down, bool typing) {
	bool all = true;
	for (UIPress *press in presses) {
		UIKey *key = press.key;
		if (key == nil) {
			all = false;
			continue;
		}
		int code = ios_key_code((long)key.keyCode);
		if (code == KEY_CODE_UNKNOWN || (typing && !ios_key_for_text(key, code))) {
			all = false;
			continue;
		}
		if (code == KEY_CODE_SHIFT) {
			shift_down = down;
		}
		if (down) {
			iron_internal_keyboard_trigger_key_down(code);
		}
		else {
			iron_internal_keyboard_trigger_key_up(code);
		}
	}
	return all;
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	if (keyboard_shown || !ios_presses(presses, true, false)) { // The key input view takes them while typing
		[super pressesBegan:presses withEvent:event];
	}
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	if (keyboard_shown || !ios_presses(presses, false, false)) {
		[super pressesEnded:presses withEvent:event];
	}
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	if (keyboard_shown || !ios_presses(presses, false, false)) {
		[super pressesCancelled:presses withEvent:event];
	}
}

// Shown in the iPadOS shortcut overlay (hold Cmd). The key reaches the app as Cmd + key, handled by the keymap.
static void ios_shortcut(int code) {
	iron_internal_keyboard_trigger_key_down(code);
	iron_internal_keyboard_trigger_key_up(code);
}

- (void)shortcutUndo:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_Z);
}
- (void)shortcutSave:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_S);
}
- (void)shortcutOpen:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_O);
}
- (void)shortcutNew:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_N);
}
- (void)shortcutExport:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_E);
}
- (void)shortcutPrefs:(UIKeyCommand *)c {
	ios_shortcut(KEY_CODE_K);
}

- (NSArray<UIKeyCommand *> *)keyCommands {
	if (keyboard_shown) {
		return @[];
	}
	UIKeyModifierFlags cmd       = UIKeyModifierCommand;
	UIKeyModifierFlags cmd_shift = UIKeyModifierCommand | UIKeyModifierShift;
	return @[
		[UIKeyCommand commandWithTitle:@"Undo" image:nil action:@selector(shortcutUndo:) input:@"z" modifierFlags:cmd propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Redo" image:nil action:@selector(shortcutUndo:) input:@"z" modifierFlags:cmd_shift propertyList:nil],
		[UIKeyCommand commandWithTitle:@"New Project" image:nil action:@selector(shortcutNew:) input:@"n" modifierFlags:cmd propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Open Project" image:nil action:@selector(shortcutOpen:) input:@"o" modifierFlags:cmd propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Save" image:nil action:@selector(shortcutSave:) input:@"s" modifierFlags:cmd propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Save As" image:nil action:@selector(shortcutSave:) input:@"s" modifierFlags:cmd_shift propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Export Textures" image:nil action:@selector(shortcutExport:) input:@"e" modifierFlags:cmd propertyList:nil],
		[UIKeyCommand commandWithTitle:@"Preferences" image:nil action:@selector(shortcutPrefs:) input:@"k" modifierFlags:cmd propertyList:nil],
	];
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

- (void)viewDidAppear:(BOOL)animated {
	[super viewDidAppear:animated];
	if (!keyboard_shown) {
		[my_view becomeFirstResponder]; // Hardware keys
	}
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
	// The main loop ended (a script quit): nothing draws or takes input any more, a frozen app is worse than closing it
	exit(0);
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
	// Extra time for the callback to finish (saving), the gpu is not available once in the background
	UIApplication           *app  = [UIApplication sharedApplication];
	__block UIBackgroundTaskIdentifier task = [app beginBackgroundTaskWithExpirationHandler:^{
		[app endBackgroundTask:task];
		task = UIBackgroundTaskInvalid;
	}];
	iron_internal_resign_active_callback();
	if (task != UIBackgroundTaskInvalid) {
		[app endBackgroundTask:task];
	}
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
