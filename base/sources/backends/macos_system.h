
#import <MetalKit/MTKView.h>
#include <iron_global.h>

@interface BasicMTKView : MTKView {
@private
	id<MTLDevice>       device;
	id<MTLCommandQueue> commandQueue;
	id<MTLLibrary>      library;
}

- (CAMetalLayer *)metalLayer;
- (id<MTLDevice>)metalDevice;
- (id<MTLCommandQueue>)metalQueue;
- (void)keyDown:(NSEvent *)theEvent;
- (void)keyUp:(NSEvent *)theEvent;
- (void)mouseDown:(NSEvent *)theEvent;
- (void)mouseUp:(NSEvent *)theEvent;
- (void)mouseMoved:(NSEvent *)theEvent;
- (void)mouseDragged:(NSEvent *)theEvent;
- (void)rightMouseDown:(NSEvent *)theEvent;
- (void)rightMouseUp:(NSEvent *)theEvent;
- (void)rightMouseDragged:(NSEvent *)theEvent;
- (void)scrollWheel:(NSEvent *)theEvent;
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender;
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender;
- (BOOL)acceptsFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)resignFirstResponder;
- (id)initWithFrame:(NSRect)frameRect;
- (void)resize:(NSSize)size;

@end

