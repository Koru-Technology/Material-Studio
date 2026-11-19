import SwiftUI
#if os(macOS)
import AppKit
typealias PlatformView = NSView
#else
import UIKit
typealias PlatformView = UIView
#endif

// Drives the engine: one frame per display refresh from a display link in the common run loop modes,
// so the canvas keeps drawing while menus, sliders and sheets track input. The link pauses while the engine sleeps
// (nothing changed for a moment, no work under way) and resumes when the engine wakes (apple_host.h).
final class FrameDriver: NSObject {
	static let shared = FrameDriver()

	private(set) var isStarted = false
	private(set) var isStopped = false
	private(set) var inTick    = false // In a frame: app_* calls wait for its end (app_api.h)
	private var link: CADisplayLink?
	private weak var view: PlatformView?
	private var pickerRequested: CFTimeInterval = 0 // A file picker was asked for and is not up yet, 0 for none
	private var fallback: Timer?
	private var idleTimer: Timer? // While paused: time driven work (an autosave, the session journal) wakes the engine
	private var paused = false
	private var waitTimer: Timer?
	private var lastTick: CFTimeInterval = 0
	private var afterFrames: [() -> Void] = []
	private var afterFramesTicks = 0

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
		self.view = view
		iron_apple_start()
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
		iron_apple_set_wake_callback {
			if Thread.isMainThread {
				MainActor.assumeIsolated { FrameDriver.shared.resume() }
			}
			else {
				DispatchQueue.main.async { FrameDriver.shared.resume() }
			}
		}
#if os(macOS)
		// The display link stops while the window is hidden (minimized, covered): keep frames going at a low rate,
		// scripts and background work still run. iOS draws nothing in the background.
		let fallback = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated {
				guard let self, !self.paused, CACurrentMediaTime() - self.lastTick > 0.1 else {
					return
				}
				self.tick()
			}
		}
		RunLoop.main.add(fallback, forMode: .common)
		self.fallback = fallback
#endif
		return true
	}

	// The engine slept: no frames until it wakes, a slow timer checks for time driven work meanwhile
	private func pauseIfAsleep() {
		guard !paused, afterFrames.isEmpty, iron_apple_asleep() else {
			return
		}
		paused         = true
		link?.isPaused = true
		let timer = Timer(timeInterval: 1, repeats: true) { _ in
			MainActor.assumeIsolated {
				_ = iron_apple_idle_check() // Calls the wake callback when it wakes the engine
			}
		}
		timer.tolerance = 0.5
		RunLoop.main.add(timer, forMode: .common)
		idleTimer = timer
	}

	private func resume() {
		guard paused, !isStopped else {
			return
		}
		paused         = false
		link?.isPaused = false
		idleTimer?.invalidate()
		idleTimer = nil
	}

	@objc private func displayTick(_ link: CADisplayLink) {
		tick()
	}

	private func tick() {
		// A frame can open a nested modal loop (alerts, file panels) that keeps the timers firing
		if inTick || isStopped || pickerShown {
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
		if !afterFrames.isEmpty {
			afterFramesTicks += 1
			if afterFramesTicks >= 2 {
				let actions = afterFrames
				afterFrames      = []
				afterFramesTicks = 0
				for action in actions {
					action()
				}
			}
		}
		pauseIfAsleep()
	}

	// A system file picker (the engine's, the folder picker, SwiftUI's fileImporter) runs in another process and often
	// comes up empty: its scene's setup times out while that process hangs resolving the app's Documents folder. With
	// the canvas drawing every frame it failed every time (0 of 6 opens), without frames it worked half the time (3 of 6,
	// 2026-11-19). No frames from when it is asked for, through its setup (it is presented once its remote side is
	// ready), until it is gone. The engine takes the picked files once it closes.
	func pickerWillShow() {
		pickerRequested = CACurrentMediaTime()
	}

	private var pickerShown: Bool {
#if os(iOS)
		if iron_apple_take_picker_request() {
			pickerWillShow()
		}
		var top = view?.window?.rootViewController
		while let presented = top?.presentedViewController {
			if presented is UIDocumentPickerViewController {
				pickerRequested = 0 // Up: shown until dismissed
				return true
			}
			top = presented
		}
		if pickerRequested > 0 && CACurrentMediaTime() - pickerRequested < 10 { // Not up after 10 s: it failed
			return true
		}
		pickerRequested = 0
#endif
		return false
	}

	// Runs an action between frames once the next one has drawn: what the UI just changed reaches the screen before a
	// long engine call (a tool change parses materials, 50-100 ms) holds the main thread
	func afterNextFrame(_ action: @escaping () -> Void) {
		afterFrames.append(action)
		resume()
	}

	// The engine stopped (a script quit, the user quit and saved): nothing draws or takes input any more
	private func stop() {
		isStopped = true
		link?.invalidate()
		link = nil
		fallback?.invalidate()
		fallback = nil
		idleTimer?.invalidate()
		idleTimer = nil
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
