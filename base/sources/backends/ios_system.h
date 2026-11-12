#import <CoreMotion/CMMotionManager.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

// Takes the typed text while a text field is edited, the canvas view keeps the hardware keys otherwise
// (a key input view as first responder brings up the on-screen keyboard)
@interface KeyInputView : UIView <UIKeyInput>
@end

@interface MyView : UIView <UIPencilInteractionDelegate, UIPointerInteractionDelegate, UIGestureRecognizerDelegate> {
@private
	id<MTLDevice>       device;
	id<MTLCommandQueue> queue;
	id<MTLLibrary>      library;
}
- (void)show_keyboard;
- (void)hide_keyboard;
- (CAMetalLayer *)metal_layer;
- (id<MTLDevice>)metal_device;
- (id<MTLCommandQueue>)metal_queue;
- (BOOL)hasText;
- (void)insertText:(NSString *)text;
- (void)deleteBackward;
@end

@interface MyViewController : UIViewController <UIDocumentPickerDelegate, UIDropInteractionDelegate> {
}
- (void)loadView;
- (void)setVisible:(BOOL)value;
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures;
@end

@interface IronSceneDelegate : UIResponder <UIWindowSceneDelegate> {
}
@property(strong, nonatomic) UIWindow *window;
@end
