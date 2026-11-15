import SwiftUI

@main
struct MaterialStudioApp: App {
#if os(macOS)
	@NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
#endif

	var body: some Scene {
#if os(macOS)
		Window("Material Studio", id: "main") {
			MacRootView()
		}
		.defaultSize(width: 1600, height: 1000)
#else
		WindowGroup {
			PadRootView()
		}
#endif
	}
}

#if os(macOS)
// Sidebar, canvas, inspector: the panels are placeholders until the app API lands
struct MacRootView: View {
	@State private var inspectorShown = true

	var body: some View {
		NavigationSplitView {
			List {
				Section("Layers") {
					Text("Layers")
				}
			}
			.navigationSplitViewColumnWidth(min: 200, ideal: 240)
		} detail: {
			EngineCanvas()
				.ignoresSafeArea()
				.inspector(isPresented: $inspectorShown) {
					Form {
						Section("Tool") {
							Text("Tool properties")
						}
					}
					.inspectorColumnWidth(min: 220, ideal: 260)
				}
				.toolbar {
					ToolbarItem(placement: .primaryAction) {
						Button {
							inspectorShown.toggle()
						} label: {
							Label("Inspector", systemImage: "sidebar.trailing")
						}
					}
				}
		}
	}
}

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
#else
struct PadRootView: View {
	@Environment(\.scenePhase) private var scenePhase

	var body: some View {
		EngineCanvas()
			.ignoresSafeArea()
			.statusBarHidden()
			.persistentSystemOverlays(.hidden)
			.defersSystemGestures(on: .all)
			.onOpenURL { url in
				iron_apple_open_url(url.absoluteString)
			}
			.onChange(of: scenePhase) { old, new in
				FrameDriver.shared.scenePhaseChanged(from: old, to: new)
			}
	}
}
#endif
