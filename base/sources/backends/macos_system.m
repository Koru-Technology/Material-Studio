#import <CoreText/CoreText.h>
#import "macos_system.h"
#import <Cocoa/Cocoa.h>
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <mach/mach_time.h>
#include <objc/runtime.h>
#include <stdbool.h>
#ifdef IRON_EXTERNAL_LOOP
#include "apple_host.h"
#endif

struct window_data {
	id   handle;
	id   view;
	bool fullscreen;
	void (*resize_callback)(int width, int height, void *data);
	void *resize_callback_data;
	bool (*close_callback)(void *data);
	void *close_callback_data;
};

static struct window_data windows[1]              = {0};
static bool               controlkey_mouse_button = false;
static bool               keyboard_shown          = false;
static NSApplication     *myapp;
static NSWindow          *window;
static BasicMTKView      *view;
static int                current_cursor_index = 0;
static int                key_translated[128];
static bool               engine_started = false; // iron_init ran: the view may be resized into the gpu

static void init_key_translation(void) {
	for (int i = 0; i < 128; ++i)
		key_translated[i] = KEY_CODE_UNKNOWN;
	key_translated[0]   = KEY_CODE_A;
	key_translated[1]   = KEY_CODE_S;
	key_translated[2]   = KEY_CODE_D;
	key_translated[3]   = KEY_CODE_F;
	key_translated[4]   = KEY_CODE_H;
	key_translated[5]   = KEY_CODE_G;
	key_translated[6]   = KEY_CODE_Z;
	key_translated[7]   = KEY_CODE_X;
	key_translated[8]   = KEY_CODE_C;
	key_translated[9]   = KEY_CODE_V;
	key_translated[11]  = KEY_CODE_B;
	key_translated[12]  = KEY_CODE_Q;
	key_translated[13]  = KEY_CODE_W;
	key_translated[14]  = KEY_CODE_E;
	key_translated[15]  = KEY_CODE_R;
	key_translated[16]  = KEY_CODE_Y;
	key_translated[17]  = KEY_CODE_T;
	key_translated[18]  = KEY_CODE_1;
	key_translated[19]  = KEY_CODE_2;
	key_translated[20]  = KEY_CODE_3;
	key_translated[21]  = KEY_CODE_4;
	key_translated[22]  = KEY_CODE_6;
	key_translated[23]  = KEY_CODE_5;
	key_translated[24]  = KEY_CODE_PLUS;
	key_translated[25]  = KEY_CODE_9;
	key_translated[26]  = KEY_CODE_7;
	key_translated[27]  = KEY_CODE_HYPHEN_MINUS;
	key_translated[28]  = KEY_CODE_8;
	key_translated[29]  = KEY_CODE_0;
	key_translated[30]  = KEY_CODE_CLOSE_BRACKET;
	key_translated[31]  = KEY_CODE_O;
	key_translated[32]  = KEY_CODE_U;
	key_translated[33]  = KEY_CODE_OPEN_BRACKET;
	key_translated[34]  = KEY_CODE_I;
	key_translated[35]  = KEY_CODE_P;
	key_translated[36]  = KEY_CODE_RETURN;
	key_translated[37]  = KEY_CODE_L;
	key_translated[38]  = KEY_CODE_J;
	key_translated[39]  = KEY_CODE_QUOTE;
	key_translated[40]  = KEY_CODE_K;
	key_translated[41]  = KEY_CODE_SEMICOLON;
	key_translated[42]  = KEY_CODE_BACK_SLASH;
	key_translated[43]  = KEY_CODE_COMMA;
	key_translated[44]  = KEY_CODE_SLASH;
	key_translated[45]  = KEY_CODE_N;
	key_translated[46]  = KEY_CODE_M;
	key_translated[47]  = KEY_CODE_PERIOD;
	key_translated[48]  = KEY_CODE_TAB;
	key_translated[49]  = KEY_CODE_SPACE;
	key_translated[50]  = KEY_CODE_BACK_QUOTE;
	key_translated[51]  = KEY_CODE_BACKSPACE;
	key_translated[53]  = KEY_CODE_ESCAPE;
	key_translated[64]  = KEY_CODE_F17;
	key_translated[65]  = KEY_CODE_DECIMAL;
	key_translated[67]  = KEY_CODE_MULTIPLY;
	key_translated[69]  = KEY_CODE_ADD;
	key_translated[71]  = KEY_CODE_NUM_LOCK;
	key_translated[75]  = KEY_CODE_DIVIDE;
	key_translated[76]  = KEY_CODE_RETURN;
	key_translated[78]  = KEY_CODE_SUBTRACT;
	key_translated[79]  = KEY_CODE_F18;
	key_translated[80]  = KEY_CODE_F19;
	key_translated[82]  = KEY_CODE_NUMPAD_0;
	key_translated[83]  = KEY_CODE_NUMPAD_1;
	key_translated[84]  = KEY_CODE_NUMPAD_2;
	key_translated[85]  = KEY_CODE_NUMPAD_3;
	key_translated[86]  = KEY_CODE_NUMPAD_4;
	key_translated[87]  = KEY_CODE_NUMPAD_5;
	key_translated[88]  = KEY_CODE_NUMPAD_6;
	key_translated[89]  = KEY_CODE_NUMPAD_7;
	key_translated[90]  = KEY_CODE_F20;
	key_translated[91]  = KEY_CODE_NUMPAD_8;
	key_translated[92]  = KEY_CODE_NUMPAD_9;
	key_translated[96]  = KEY_CODE_F5;
	key_translated[97]  = KEY_CODE_F6;
	key_translated[98]  = KEY_CODE_F7;
	key_translated[99]  = KEY_CODE_F3;
	key_translated[100] = KEY_CODE_F8;
	key_translated[101] = KEY_CODE_F9;
	key_translated[103] = KEY_CODE_F11;
	key_translated[105] = KEY_CODE_F13;
	key_translated[106] = KEY_CODE_F16;
	key_translated[107] = KEY_CODE_F14;
	key_translated[109] = KEY_CODE_F10;
	key_translated[111] = KEY_CODE_F12;
	key_translated[113] = KEY_CODE_F15;
	key_translated[114] = KEY_CODE_INSERT;
	key_translated[115] = KEY_CODE_HOME;
	key_translated[116] = KEY_CODE_PAGE_UP;
	key_translated[117] = KEY_CODE_DELETE;
	key_translated[118] = KEY_CODE_F4;
	key_translated[119] = KEY_CODE_END;
	key_translated[120] = KEY_CODE_F2;
	key_translated[121] = KEY_CODE_PAGE_DOWN;
	key_translated[122] = KEY_CODE_F1;
	key_translated[123] = KEY_CODE_LEFT;
	key_translated[124] = KEY_CODE_RIGHT;
	key_translated[125] = KEY_CODE_DOWN;
	key_translated[126] = KEY_CODE_UP;
}

@implementation BasicMTKView

static bool shift    = false;
static bool ctrl     = false;
static bool alt      = false;
static bool cmd      = false;
static bool capslock = false;

static bool trackpad_hide_shift = false; // Shift selects the middle mouse button for trackpad drag

- (void)flagsChanged:(NSEvent *)theEvent {
	if (shift) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_SHIFT);
		shift = false;
	}
	if (ctrl) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_CONTROL);
		ctrl = false;
	}
	if (alt) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_ALT);
		alt = false;
	}
	if (cmd) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_META);
		cmd = false;
	}
	if (capslock) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_CAPS_LOCK);
		capslock = false;
	}

	if (([theEvent modifierFlags] & NSEventModifierFlagShift) && !trackpad_hide_shift) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_SHIFT);
		shift = true;
	}
	if ([theEvent modifierFlags] & NSEventModifierFlagControl) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_CONTROL);
		ctrl = true;
	}
	if ([theEvent modifierFlags] & NSEventModifierFlagOption) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_ALT);
		alt = true;
	}
	if ([theEvent modifierFlags] & NSEventModifierFlagCommand) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_META);
		cmd = true;
	}
	if ([theEvent modifierFlags] & NSEventModifierFlagCapsLock) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_CAPS_LOCK);
		capslock = true;
	}
}

- (void)keyDown:(NSEvent *)theEvent {
	if ([theEvent isARepeat])
		return;

	unsigned short kc   = [theEvent keyCode];
	int            code = (kc < 128) ? key_translated[kc] : KEY_CODE_UNKNOWN;
	if (code != KEY_CODE_UNKNOWN) {
		iron_internal_keyboard_trigger_key_down(code);
	}

	NSString *chars = [theEvent characters];
	if (![chars length])
		return;
	unichar ch = [chars characterAtIndex:0];

	if (ch >= 0xF700)
		return;
	if (ch == 27)
		return;

	if ([theEvent modifierFlags] & NSEventModifierFlagCommand) {
		NSString *base = [theEvent charactersIgnoringModifiers];
		unichar   bc   = [base length] ? [base characterAtIndex:0] : ch;
		if (bc == 'x') {
			iron_internal_cut_callback();
		}
		if (bc == 'c') {
			iron_internal_copy_callback();
		}
		if (bc == 'v') {
			NSPasteboard *board = [NSPasteboard generalPasteboard];
			NSString     *data  = [board stringForType:NSPasteboardTypeString];
			const char   *text  = data != nil ? [data UTF8String] : NULL;
			if (text != NULL) {
				iron_internal_paste_callback((char *)text);
			}
		}
		return;
	}

	if (ch == NSCarriageReturnCharacter || ch == NSNewlineCharacter || ch == NSEnterCharacter) {
		iron_internal_keyboard_trigger_key_press('\n');
		return;
	}
	if (ch == 0x7f) {
		iron_internal_keyboard_trigger_key_press('\x08');
		return;
	}

	iron_internal_keyboard_trigger_key_press(ch);
}

- (void)keyUp:(NSEvent *)theEvent {
	unsigned short kc   = [theEvent keyCode];
	int            code = (kc < 128) ? key_translated[kc] : KEY_CODE_UNKNOWN;
	if (code != KEY_CODE_UNKNOWN) {
		iron_internal_keyboard_trigger_key_up(code);
	}
}

// In the engine view's pixels, top left origin (the view may not fill the window)
static int get_mouse_x(NSEvent *event) {
	NSPoint point = [view convertPoint:[event locationInWindow] fromView:nil];
	return (int)(point.x * [[view window] backingScaleFactor]);
}

static int get_mouse_y(NSEvent *event) {
	NSPoint point = [view convertPoint:[event locationInWindow] fromView:nil];
	return (int)(([view bounds].size.height - point.y) * [[view window] backingScaleFactor]);
}

- (void)mouseDown:(NSEvent *)theEvent {
	if ([theEvent modifierFlags] & NSEventModifierFlagControl) {
		controlkey_mouse_button = true;
		iron_internal_mouse_trigger_press(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	else {
		controlkey_mouse_button = false;
		iron_internal_mouse_trigger_press(0, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}

	if ([theEvent subtype] == NSEventSubtypeTabletPoint) {
		iron_internal_pen_trigger_press(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)mouseUp:(NSEvent *)theEvent {
	if (controlkey_mouse_button) {
		iron_internal_mouse_trigger_release(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	else {
		iron_internal_mouse_trigger_release(0, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	controlkey_mouse_button = false;

	if ([theEvent subtype] == NSEventSubtypeTabletPoint) {
		iron_internal_pen_trigger_release(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)mouseMoved:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)mouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));

	if ([theEvent subtype] == NSEventSubtypeTabletPoint) {
		iron_internal_pen_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)rightMouseDown:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_press(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)rightMouseUp:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_release(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)rightMouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseDown:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_press(2, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseUp:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_release(2, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

static bool   trackpad_drag   = false;
static int    trackpad_button = 1;
static double trackpad_lift   = 0.0;  // Fingers lifted, waiting for momentum scrolling
static float  trackpad_x      = 0.0f; // Movement remainder
static float  trackpad_y      = 0.0f;

static void trackpad_release(void) {
	trackpad_drag = false;
	trackpad_lift = 0.0;
	int x;
	int y;
	iron_mouse_get_position(&x, &y);
	iron_internal_mouse_trigger_release(trackpad_button, x, y);
	if (trackpad_hide_shift) {
		trackpad_hide_shift = false;
		if ([NSEvent modifierFlags] & NSEventModifierFlagShift) {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_SHIFT);
			shift = true;
		}
	}
}

- (void)scrollWheel:(NSEvent *)theEvent {
	// Two finger trackpad drag - right mouse button, shift - middle mouse button, continues with momentum scrolling
	NSEventPhase phase    = [theEvent phase];
	NSEventPhase momentum = [theEvent momentumPhase];
	if ([theEvent hasPreciseScrollingDeltas] && (trackpad_drag || (phase == NSEventPhaseBegan && iron_mouse_trackpad_drag && !iron_mouse_is_locked()))) {
		if (phase == NSEventPhaseBegan || phase == NSEventPhaseMayBegin) { // Fingers down, stops momentum
			trackpad_lift = 0.0;
			if (!trackpad_drag) {
				trackpad_drag   = true;
				trackpad_button = ([theEvent modifierFlags] & NSEventModifierFlagShift) ? 2 : 1;
				trackpad_x      = 0.0f;
				trackpad_y      = 0.0f;
				if (trackpad_button == 2 && shift) { // Plain middle mouse button drag
					trackpad_hide_shift = true;
					shift               = false;
					iron_internal_keyboard_trigger_key_up(KEY_CODE_SHIFT);
				}
				iron_internal_mouse_trigger_press(trackpad_button, get_mouse_x(theEvent), get_mouse_y(theEvent));
			}
		}
		else if (phase == NSEventPhaseChanged || momentum == NSEventPhaseBegan || momentum == NSEventPhaseChanged) {
			trackpad_lift = 0.0;
			float scale   = [[[NSApplication sharedApplication] mainWindow] backingScaleFactor];
			float dir     = [theEvent isDirectionInvertedFromDevice] ? 1.0f : -1.0f; // Follow the fingers
			trackpad_x += [theEvent scrollingDeltaX] * scale * dir;
			trackpad_y += [theEvent scrollingDeltaY] * scale * dir;
			int movement_x = (int)trackpad_x;
			int movement_y = (int)trackpad_y;
			trackpad_x -= movement_x;
			trackpad_y -= movement_y;
			// Cursor stays in place
			iron_internal_mouse_trigger_movement(movement_x, movement_y);
		}
		else if (phase == NSEventPhaseEnded) {
			trackpad_lift = [theEvent timestamp];
		}
		else if (phase == NSEventPhaseCancelled || momentum == NSEventPhaseEnded || momentum == NSEventPhaseCancelled) {
			trackpad_release();
		}
		return;
	}

	float delta = [theEvent deltaY];
	iron_internal_mouse_trigger_scroll(-delta);
}

- (void)magnifyWithEvent:(NSEvent *)theEvent {
	// Pinch to zoom - mouse wheel
	iron_internal_mouse_trigger_scroll(-[theEvent magnification] * 10.0f);
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
	NSPasteboard   *pboard         = [sender draggingPasteboard];
	NSDragOperation sourceDragMask = [sender draggingSourceOperationMask];
	if ([[pboard types] containsObject:NSURLPboardType]) {
		if (sourceDragMask & NSDragOperationLink) {
			return NSDragOperationLink;
		}
	}
	return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
	NSPasteboard *pboard = [sender draggingPasteboard];
	if ([[pboard types] containsObject:NSURLPboardType]) {
		NSArray *urls = [pboard readObjectsForClasses:@[ [NSURL class] ] options:nil];
		for (NSURL *fileURL in urls) {
			iron_internal_drop_files_callback((char *)[fileURL.path UTF8String]);
		}
	}
	return YES;
}

- (id)initWithFrame:(NSRect)frameRect {
	self = [super initWithFrame:frameRect];

	device       = MTLCreateSystemDefaultDevice();
	commandQueue = [device newCommandQueue];
	library      = [device newDefaultLibrary];

	CAMetalLayer *metalLayer   = (CAMetalLayer *)self.layer;
	metalLayer.device          = device;
	metalLayer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
	metalLayer.framebufferOnly = YES;
	metalLayer.opaque          = YES;
	metalLayer.backgroundColor = nil;
	return self;
}

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (BOOL)becomeFirstResponder {
	return YES;
}

- (BOOL)resignFirstResponder {
	return YES;
}

- (void)resize:(NSSize)size {
	[self setFrameSize:size];
}

#ifdef IRON_EXTERNAL_LOOP
// Laid out by the native app: follow the view's own size instead of the window's
- (void)setFrameSize:(NSSize)size {
	[super setFrameSize:size];
	[self engineResize];
}

- (void)viewDidChangeBackingProperties {
	[super viewDidChangeBackingProperties];
	[self engineResize];
}

- (void)engineResize {
	if (!engine_started || self.window == nil) {
		return;
	}
	int w = iron_window_width();
	int h = iron_window_height();
	if (w > 0 && h > 0) {
		gpu_resize(w, h);
		iron_internal_call_resize_callback(w, h);
	}
}

- (void)updateTrackingAreas {
	[super updateTrackingAreas];
	for (NSTrackingArea *area in self.trackingAreas) {
		[self removeTrackingArea:area];
	}
	// Hover (the brush cursor) while the pointer is over the view, without a button held
	[self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
	                                                   options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
	                                                     owner:self
	                                                  userInfo:nil]];
}
#endif

- (CAMetalLayer *)metalLayer {
	return (CAMetalLayer *)self.layer;
}

- (id<MTLDevice>)metalDevice {
	return device;
}

- (id<MTLCommandQueue>)metalQueue {
	return commandQueue;
}

@end

void iron_copy_to_clipboard(const char *text) {
	NSPasteboard *board = [NSPasteboard generalPasteboard];
	[board clearContents];
	[board setString:[NSString stringWithUTF8String:text] forType:NSPasteboardTypeString];
}

int iron_count_displays(void) {
	NSArray *screens = [NSScreen screens];
	return (int)[screens count];
}

int iron_primary_display(void) {
	NSArray  *screens      = [NSScreen screens];
	NSScreen *mainScreen   = [NSScreen mainScreen];
	int       max_displays = 8;
	for (int i = 0; i < max_displays; ++i) {
		if (mainScreen == screens[i]) {
			return i;
		}
	}
	return -1;
}

void iron_display_init(void) {}

float iron_display_backing_scale(void) {
	return [[NSScreen mainScreen] backingScaleFactor];
}

iron_display_mode_t iron_display_current_mode(int display) {
	NSArray            *screens    = [NSScreen screens];
	NSScreen           *screen     = screens[display];
	NSRect              screenRect = [screen frame];
	iron_display_mode_t dm;
	dm.width          = screenRect.size.width;
	dm.height         = screenRect.size.height;
	dm.frequency      = 60;
	dm.bits_per_pixel = 32;

	NSDictionary *description         = [screen deviceDescription];
	NSSize        displayPixelSize    = [[description objectForKey:NSDeviceSize] sizeValue];
	NSNumber     *screenNumber        = [description objectForKey:@"NSScreenNumber"];
	CGSize        displayPhysicalSize = CGDisplayScreenSize([screenNumber unsignedIntValue]);            // in millimeters
	double        ppi                 = displayPixelSize.width / (displayPhysicalSize.width * 0.039370); // Convert MM to INCH
	dm.pixels_per_inch                = round(ppi);

	return dm;
}

bool iron_mouse_can_lock(void) {
	return true;
}

void iron_mouse_show(void) {
	CGDisplayShowCursor(kCGDirectMainDisplay);
}

void iron_mouse_hide(void) {
	CGDisplayHideCursor(kCGDirectMainDisplay);
}

void iron_mouse_set_position(int x, int y) {
	NSWindow *window      = windows[0].handle;
	float     scale       = [window backingScaleFactor];
	NSRect    rect        = [view bounds];
	NSPoint   windowpoint = [view convertPoint:NSMakePoint(x / scale, rect.size.height - y / scale) toView:nil];
	NSPoint   screenpoint = [window convertPointToScreen:windowpoint];
	CGPoint   cgpoint     = CGPointMake(screenpoint.x, [[NSScreen mainScreen] frame].size.height - screenpoint.y);
	CGDisplayMoveCursorToPoint(kCGDirectMainDisplay, cgpoint);
	CGAssociateMouseAndMouseCursorPosition(true);
}

void iron_mouse_get_position(int *x, int *y) {
	NSWindow *window = windows[0].handle;
	float     scale  = [window backingScaleFactor];
	NSRect    rect   = [view bounds];
	NSPoint   point  = [view convertPoint:[window mouseLocationOutsideOfEventStream] fromView:nil];
	*x               = (int)(point.x * scale);
	*y               = (int)((rect.size.height - point.y) * scale);
}

void iron_mouse_set_cursor(iron_cursor_t cursor_index) {
	if (current_cursor_index == cursor_index) {
		return;
	}
	current_cursor_index = cursor_index;
	if (cursor_index == IRON_CURSOR_HAND) {
		[[NSCursor pointingHandCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_IBEAM) {
		[[NSCursor IBeamCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_SIZEWE) {
		[[NSCursor resizeLeftRightCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_SIZENS) {
		[[NSCursor resizeUpDownCursor] set];
	}
	else {
		[[NSCursor arrowCursor] set];
	}
}

void iron_keyboard_show(void) {
	keyboard_shown = true;
}

void iron_keyboard_hide(void) {
	keyboard_shown = false;
}

bool iron_keyboard_active(void) {
	return keyboard_shown;
}

const char *iron_system_id(void) {
	return "macOS";
}

void iron_set_keep_screen_on(bool on) {}

@interface IronApplication : NSApplication {
}
- (void)terminate:(id)sender;
@end

@interface IronAppDelegate : NSObject <NSWindowDelegate> {
}
- (void)windowWillClose:(NSNotification *)notification;
- (void)windowDidResize:(NSNotification *)notification;
- (void)windowWillMiniaturize:(NSNotification *)notification;
- (void)windowDidDeminiaturize:(NSNotification *)notification;
- (void)windowDidResignMain:(NSNotification *)notification;
- (void)windowDidBecomeMain:(NSNotification *)notification;
@end

static IronAppDelegate *delegate;

CAMetalLayer *get_metal_layer(void) {
	return [view metalLayer];
}

id get_metal_device(void) {
	return [view metalDevice];
}

id get_metal_queue(void) {
	return [view metalQueue];
}

bool iron_internal_handle_messages(void) {
#ifdef IRON_EXTERNAL_LOOP
	// The app's run loop delivers the events
	if (trackpad_lift > 0.0 && [[NSProcessInfo processInfo] systemUptime] - trackpad_lift > 0.1) {
		trackpad_release();
	}
	return true;
#else
	NSEvent *event;
	while ((event = [myapp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode
	                                     dequeue:YES]) != nil) { // distantPast: non-blocking
		// Key up is not delivered to the view while cmd is held, keys would stay down
		if ([event type] == NSEventTypeKeyUp && ([event modifierFlags] & NSEventModifierFlagCommand)) {
			[[myapp keyWindow] sendEvent:event];
		}
		else {
			[myapp sendEvent:event];
		}
	}
	[myapp updateWindows];

	if (trackpad_lift > 0.0 && [[NSProcessInfo processInfo] systemUptime] - trackpad_lift > 0.1) {
		trackpad_release();
	}

	// Sleep for a frame to limit the calls when the window is not visible.
	if (!window.visible) {
		[NSThread sleepForTimeInterval:1.0 / 60];
	}
	return true;
#endif
}

#ifndef IRON_EXTERNAL_LOOP // The native app makes the window and the menu
static void create_window(iron_window_options_t *options) {
	int width     = options->width / [[NSScreen mainScreen] backingScaleFactor];
	int height    = options->height / [[NSScreen mainScreen] backingScaleFactor];
	int styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	if ((options->features & IRON_WINDOW_FEATURES_RESIZABLE) || (options->features & IRON_WINDOW_FEATURES_MAXIMIZABLE)) {
		styleMask |= NSWindowStyleMaskResizable;
	}
	if (options->features & IRON_WINDOW_FEATURES_MINIMIZABLE) {
		styleMask |= NSWindowStyleMaskMiniaturizable;
	}

	view = [[BasicMTKView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
	[view registerForDraggedTypes:[NSArray arrayWithObjects:NSURLPboardType, nil]];
	window   = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height) styleMask:styleMask backing:NSBackingStoreBuffered defer:TRUE];
	delegate = [IronAppDelegate alloc];
	[window setDelegate:delegate];
	[window setTitle:[NSString stringWithCString:options->title encoding:NSUTF8StringEncoding]];
	[window setAcceptsMouseMovedEvents:YES];
	[[window contentView] addSubview:view];
	[window center];
	if (options->x >= 0 && options->y >= 0) {
		float scale = [[NSScreen mainScreen] backingScaleFactor];
		[window setFrameTopLeftPoint:NSMakePoint(options->x / scale, [[NSScreen mainScreen] frame].size.height - options->y / scale)];
	}

	windows[0].handle = window;
	windows[0].view   = view;

	[window makeKeyAndOrderFront:nil];

	if (options->mode == IRON_WINDOW_MODE_FULLSCREEN) {
		[window toggleFullScreen:nil];
		windows[0].fullscreen = true;
	}
}
#endif

void iron_window_set_close_callback(bool (*callback)(void *), void *data) {
	windows[0].close_callback      = callback;
	windows[0].close_callback_data = data;
}

#ifndef IRON_EXTERNAL_LOOP
static void add_menubar(void) {
	NSString *appName = [[NSProcessInfo processInfo] processName];

	NSMenu     *appMenu      = [NSMenu new];
	NSString   *quitTitle    = [@"Quit " stringByAppendingString:appName];
	NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle action:@selector(terminate:) keyEquivalent:@"q"];
	[appMenu addItem:quitMenuItem];

	NSMenuItem *appMenuItem = [NSMenuItem new];
	[appMenuItem setSubmenu:appMenu];

	NSMenu *menubar = [NSMenu new];
	[menubar addItem:appMenuItem];
	[NSApp setMainMenu:menubar];
}
#endif

#ifdef IRON_EXTERNAL_LOOP
void *iron_apple_engine_view(void) {
	if (view == nil) {
		view = [[BasicMTKView alloc] initWithFrame:NSMakeRect(0, 0, 640, 480)];
		[view registerForDraggedTypes:[NSArray arrayWithObjects:NSURLPboardType, nil]];
	}
	return (__bridge void *)view;
}

void *iron_apple_engine_view_controller(void) {
	return NULL;
}

void iron_apple_start(void) {
	NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
	int                  argc = (int)args.count;
	char               **argv = calloc(argc + 1, sizeof(char *));
	for (int i = 0; i < argc; ++i) {
		argv[i] = strdup([args[i] UTF8String]);
	}
	kickstart(argc, argv);
}

bool iron_apple_should_terminate(void) {
	if (!engine_started) {
		return true;
	}
	// The save prompt, then iron_stop: the frame driver sees the engine stop and quits
	if (windows[0].close_callback != NULL) {
		windows[0].close_callback(windows[0].close_callback_data);
	}
	else {
		iron_stop();
	}
	return false;
}

void iron_apple_resign_active(void) {}
void iron_apple_enter_background(void) {}
void iron_apple_enter_foreground(void) {}
void iron_apple_open_url(const char *url) {}

static void adopt_host_window(void) {
	myapp  = NSApp;
	window = [view window];
	[window setAcceptsMouseMovedEvents:YES];
	windows[0].handle = window;
	windows[0].view   = view;
	delegate          = [[IronAppDelegate alloc] init];
	// The app owns the window's delegate, the engine listens to the notifications instead
	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	[center addObserver:delegate selector:@selector(windowWillMiniaturize:) name:NSWindowWillMiniaturizeNotification object:window];
	[center addObserver:delegate selector:@selector(windowDidDeminiaturize:) name:NSWindowDidDeminiaturizeNotification object:window];
	[center addObserver:delegate selector:@selector(windowDidResignKey:) name:NSWindowDidResignKeyNotification object:window];
	[center addObserver:delegate selector:@selector(windowDidBecomeKey:) name:NSWindowDidBecomeKeyNotification object:window];
	// Closing the window quits through the app, which asks to save first
	NSButton *close = [window standardWindowButton:NSWindowCloseButton];
	[close setTarget:NSApp];
	[close setAction:@selector(terminate:)];
	// Key up is not delivered to the view while cmd is held, keys would stay down
	[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
	                                      handler:^NSEvent *(NSEvent *event) {
		                                      if (([event modifierFlags] & NSEventModifierFlagCommand) && [window firstResponder] == view) {
			                                      [view keyUp:event];
		                                      }
		                                      return event;
	                                      }];
	[window makeFirstResponder:view];
}
#endif

void iron_init(iron_window_options_t *win) {
	init_key_translation();
#ifdef IRON_EXTERNAL_LOOP
	adopt_host_window();
	engine_started = true;
	gpu_init(win->depth_bits, true);
#else
	@autoreleasepool {
		myapp = [IronApplication sharedApplication];
		[myapp finishLaunching];
		[[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateAllWindows];
		NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
		add_menubar();

	}

	create_window(win);
	gpu_init(win->depth_bits, true);
#endif
}

// The engine view's size in pixels (the view fills the window, unless a native app lays it out)
int iron_window_width() {
	float scale = [[view window] backingScaleFactor];
	return [view bounds].size.width * scale;
}

int iron_window_height() {
	float scale = [[view window] backingScaleFactor];
	return [view bounds].size.height * scale;
}

void iron_load_url(const char *url) {
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
}

void iron_internal_shutdown(void) {
	iron_internal_shutdown_callback();
}

#ifndef IRON_EXTERNAL_LOOP // The native app has the entry point
int main(int argc, char **argv) {
	return kickstart(argc, argv);
}
#endif

@implementation IronApplication

- (void)terminate:(id)sender {
	iron_stop();
}

@end

@implementation IronAppDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
	if (windows[0].close_callback != NULL) {
		if (windows[0].close_callback(windows[0].close_callback_data)) {
			return YES;
		}
		else {
			return NO;
		}
	}
	return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
	iron_stop();
}

void iron_internal_call_resize_callback(int width, int height) {
	iron_delay_idle_sleep(); // Drawn at the new size
	if (windows[0].resize_callback != NULL) {
		windows[0].resize_callback(width, height, windows[0].resize_callback_data);
	}
}

- (void)windowDidResize:(NSNotification *)notification {
	NSWindow *window = [notification object];
	NSSize    size   = [[window contentView] frame].size;
	[view resize:size];

	float scale = [window backingScaleFactor];
	int   w     = size.width * scale;
	int   h     = size.height * scale;

	gpu_resize(w, h);
	iron_internal_call_resize_callback(w, h);
}

- (void)windowWillMiniaturize:(NSNotification *)notification {
	iron_internal_background_callback();
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
	iron_internal_foreground_callback();
}

- (void)windowDidResignMain:(NSNotification *)notification {
}

- (void)windowDidResignKey:(NSNotification *)notification {
	iron_internal_background_callback();
	shift    = false;
	ctrl     = false;
	alt      = false;
	cmd      = false;
	capslock = false;
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
	iron_internal_foreground_callback();
}

- (void)windowDidBecomeMain:(NSNotification *)notification {
}

@end

int iron_window_x() {
	NSWindow *window = windows[0].handle;
	return [window frame].origin.x * [window backingScaleFactor];
}

int iron_window_y() {
	NSWindow *window = windows[0].handle;
	NSRect    frame  = [window frame];
	float     top    = [[NSScreen mainScreen] frame].size.height - (frame.origin.y + frame.size.height);
	return top * [window backingScaleFactor];
}

void iron_window_resize(int width, int height) {}
void iron_window_move(int x, int y) {}
void iron_window_change_mode(iron_window_mode_t mode) {}
void iron_window_destroy() {}
void iron_window_show() {}
void iron_window_hide() {}
void iron_window_create(iron_window_options_t *win) {}

void iron_window_set_title(const char *title) {
	NSWindow *window = windows[0].handle;
	[window setTitle:[NSString stringWithUTF8String:title]];
}

void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {
	windows[0].resize_callback      = callback;
	windows[0].resize_callback_data = data;
}

iron_window_mode_t iron_window_get_mode() {
	return IRON_WINDOW_MODE_WINDOW;
}

int iron_window_display() {
	return 0;
}


extern void (*iron_save_and_quit)(bool);

bool _save_and_quit_callback_internal() {
	bool      save  = false;
	NSString *title = [window title];
	bool      dirty = [title rangeOfString:@"* - Material Studio"].location != NSNotFound;
	if (dirty) {
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setMessageText:@"Project has been modified, save changes?"];
		[alert addButtonWithTitle:@"Yes"];
		[alert addButtonWithTitle:@"Cancel"];
		[alert addButtonWithTitle:@"No"];
		[alert setAlertStyle:NSAlertStyleWarning];
		NSInteger res = [alert runModal];
		if (res == NSAlertFirstButtonReturn) {
			save = true;
		}
		else if (res == NSAlertThirdButtonReturn) {
			save = false;
		}
		else { // Cancel
			return false;
		}
	}
	iron_save_and_quit(save);
	return false;
}

volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

void iron_exec_async(const char *path, char *argv[]) {
	iron_exec_async_done   = 0;
	NSTask         *task   = [[NSTask alloc] init];
	NSString       *launch = [NSString stringWithUTF8String:path];
	NSMutableArray *args   = [NSMutableArray array];
	if (![launch containsString:@"/"]) {
		[args addObject:launch];
		launch                    = @"/usr/bin/env";
		NSMutableDictionary *env  = [NSMutableDictionary dictionaryWithDictionary:[[NSProcessInfo processInfo] environment]];
		NSString            *home = NSHomeDirectory();
		env[@"PATH"]              = [NSString stringWithFormat:@"%@/.local/bin:%@/bin:/usr/local/bin:%@", home, home, env[@"PATH"]];
		[task setEnvironment:env];
	}
	[task setLaunchPath:launch];
	int i = 1;
	while (argv[i] != NULL) {
		[args addObject:[NSString stringWithUTF8String:argv[i]]];
		i++;
	}
	[task setArguments:args];
	[task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
	if (iron_exec_async_output_file != NULL) {
		NSString *output_path = [NSString stringWithUTF8String:iron_exec_async_output_file];
		[[NSFileManager defaultManager] createFileAtPath:output_path contents:nil attributes:nil];
		NSFileHandle *file_handle = [NSFileHandle fileHandleForWritingAtPath:output_path];
		[task setStandardOutput:file_handle];
	}
	task.terminationHandler = ^(NSTask *t) {
		iron_exec_async_done = 1;
	};
	[task launch];
}
