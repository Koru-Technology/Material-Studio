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
	private var fallback: Timer?
	private var waitTimer: Timer?
	private var lastTick: CFTimeInterval = 0

	// Starts the engine once the view is in a window and has a size (SwiftUI adds it after creating it)
	func start(view: PlatformView) {
		if isStarted || waitTimer != nil {
			return
		}
		if startIfReady(view) {
			return
		}
		let since = CACurrentMediaTime()
		var logged = since
		waitTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated {
				guard let self else {
					return
				}
				if self.startIfReady(view) {
					self.waitTimer?.invalidate()
					self.waitTimer = nil
				}
				else if CACurrentMediaTime() - logged > 5 {
					logged = CACurrentMediaTime()
					fputs("[native] waiting \(Int(logged - since)) s for the canvas: window \(view.window != nil), size \(view.bounds.size)\n", stderr)
				}
			}
		}
	}

	private func startIfReady(_ view: PlatformView) -> Bool {
		guard view.window != nil, view.bounds.width > 0, view.bounds.height > 0 else {
			return false
		}
		isStarted = true
		iron_apple_start()
		app_set_native_chrome(true) // The panels and menus are native, the engine draws its views only
#if os(iOS)
		// The menu and popover buttons float 16 + 44 points down the top of the screen: the node editor's menubar goes below
		app_set_overlay_top_inset(Int32((16 + 44 + 8) * view.contentScaleFactor))
#endif
#if os(macOS)
		let link = view.displayLink(target: self, selector: #selector(displayTick))
#else
		let link                     = CADisplayLink(target: self, selector: #selector(displayTick))
		link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 120, preferred: 120)
#endif
		link.add(to: .main, forMode: .common)
		self.link = link
		// The display link stops while the window is hidden (minimized, covered, screen locked):
		// keep frames going at a low rate, scripts and background work still run, as with the engine's own loop
		let fallback = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated {
				guard let self, CACurrentMediaTime() - self.lastTick > 0.1 else {
					return
				}
				self.tick()
			}
		}
		RunLoop.main.add(fallback, forMode: .common)
		self.fallback = fallback
		return true
	}

	@objc private func displayTick(_ link: CADisplayLink) {
		tick()
	}

	private func tick() {
		// A frame can open a nested modal loop (alerts, file panels) that keeps the timers firing
		if inTick || isStopped {
			return
		}
		lastTick = CACurrentMediaTime()
		inTick   = true
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
		fallback?.invalidate()
		fallback = nil
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
