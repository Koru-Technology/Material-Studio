import SwiftUI

@main
struct MaterialStudioApp: App {
#if os(macOS)
	@NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
#endif

	var body: some Scene {
#if os(macOS)
		Window("Material Studio", id: "main") {
			RootView()
		}
		.defaultSize(width: 1600, height: 1000)
#else
		WindowGroup {
			RootView()
		}
#endif
	}
}

struct RootView: View {
#if os(iOS)
	@Environment(\.scenePhase) private var scenePhase
#endif

	var body: some View {
		EngineCanvas()
			.ignoresSafeArea()
#if os(iOS)
			.statusBarHidden()
			.persistentSystemOverlays(.hidden)
			.defersSystemGestures(on: .all)
			.onOpenURL { url in
				iron_apple_open_url(url.absoluteString)
			}
			.onChange(of: scenePhase) { old, new in
				FrameDriver.shared.scenePhaseChanged(from: old, to: new)
			}
#endif
	}
}

#if os(macOS)
final class AppDelegate: NSObject, NSApplicationDelegate {
	func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
		if FrameDriver.shared.isStopped {
			return .terminateNow
		}
		// Asks to save, then the engine stops and the frame driver quits
		return iron_apple_should_terminate() ? .terminateNow : .terminateCancel
	}

	func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
		true
	}
}
#endif
