import SwiftUI
#if os(macOS)
import AppKit
typealias PlatformView = NSView
#else
import UIKit
typealias PlatformView = UIView
#endif

// Drives the engine: one frame per display refresh from a display link in the common run loop modes,
// so the canvas keeps drawing while menus, sliders and sheets track input.
final class FrameDriver: NSObject {
	static let shared = FrameDriver()

	private(set) var isStarted = false
	private(set) var isStopped = false
	private var inTick         = false
	private var link: CADisplayLink?

	// Starts the engine once the view is in a window and has a size (SwiftUI adds it after creating it)
	func start(view: PlatformView) {
		if isStarted {
			return
		}
		guard view.window != nil, view.bounds.width > 0, view.bounds.height > 0 else {
			DispatchQueue.main.async { self.start(view: view) }
			return
		}
		isStarted = true
		iron_apple_start()
#if os(macOS)
		let link = view.displayLink(target: self, selector: #selector(tick))
#else
		let link                     = CADisplayLink(target: self, selector: #selector(tick))
		link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 120, preferred: 120)
#endif
		link.add(to: .main, forMode: .common)
		self.link = link
	}

	@objc private func tick(_ link: CADisplayLink) {
		// A frame can open a nested modal loop (alerts, file panels) that keeps the display link firing
		if inTick || isStopped {
			return
		}
		inTick = true
		let running = iron_external_tick()
		inTick = false
		if !running {
			stop()
			return
		}
		AppStore.shared.refresh()
	}

	// The engine stopped (a script quit, the user quit and saved): nothing draws or takes input any more
	private func stop() {
		isStopped = true
		link?.invalidate()
		link = nil
		iron_external_shutdown()
#if os(macOS)
		NSApp.terminate(nil)
#else
		exit(0)
#endif
	}

#if os(iOS)
	func scenePhaseChanged(from old: ScenePhase, to new: ScenePhase) {
		guard isStarted, !isStopped else {
			return
		}
		if old == .active && new != .active {
			iron_apple_resign_active()
		}
		if new == .background {
			iron_apple_enter_background()
		}
		else if old == .background {
			iron_apple_enter_foreground()
		}
	}
#endif
}
