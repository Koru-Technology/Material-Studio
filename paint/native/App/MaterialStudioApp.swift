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
		.commands {
			EditCommands()
		}
#else
		WindowGroup {
			PadRootView()
		}
#endif
	}
}

#if os(macOS)
// Sidebar (layers), canvas, inspector (tool, brush and the selected layer's settings)
struct MacRootView: View {
	@State private var inspectorShown = true

	var body: some View {
		// The panels call the engine: they show once it started, as the iPad controls
		NavigationSplitView {
			Group {
				if AppStore.shared.ready {
					LayersView()
				}
			}
			.navigationSplitViewColumnWidth(min: 200, ideal: 260)
		} detail: {
			EngineCanvas()
				.ignoresSafeArea()
				.inspector(isPresented: $inspectorShown) {
					Group {
						if AppStore.shared.ready {
							InspectorView()
						}
					}
					.inspectorColumnWidth(min: 240, ideal: 280)
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

struct EditCommands: Commands {
	private var store = AppStore.shared

	var body: some Commands {
		CommandGroup(replacing: .undoRedo) {
			Button(store.undoName.isEmpty ? "Undo" : "Undo \(store.undoName)") {
				store.perform { app_undo() }
			}
			.keyboardShortcut("z")
			.disabled(!store.canUndo)
			Button("Redo") {
				store.perform { app_redo() }
			}
			.keyboardShortcut("z", modifiers: [.command, .shift])
			.disabled(!store.canRedo)
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
		ZStack {
			EngineCanvas()
				.ignoresSafeArea()
			if AppStore.shared.ready && !AppStore.shared.engineModal {
				PadControls()
					.transition(.opacity)
			}
		}
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
