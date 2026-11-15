import SwiftUI

// The engine's Metal view (the paint canvas, 3D viewport, 2D view and node editor), laid out by SwiftUI.
// The engine starts once the view is in a window with a size.
#if os(macOS)
struct EngineCanvas: NSViewRepresentable {
	func makeNSView(context: Context) -> NSView {
		let view = Unmanaged<NSView>.fromOpaque(iron_apple_engine_view()).takeUnretainedValue()
		FrameDriver.shared.start(view: view)
		return view
	}

	func updateNSView(_ view: NSView, context: Context) {}
}
#else
struct EngineCanvas: UIViewControllerRepresentable {
	func makeUIViewController(context: Context) -> UIViewController {
		let controller = Unmanaged<UIViewController>.fromOpaque(iron_apple_engine_view_controller()).takeUnretainedValue()
		FrameDriver.shared.start(view: controller.view)
		return controller
	}

	func updateUIViewController(_ controller: UIViewController, context: Context) {}
}
#endif
