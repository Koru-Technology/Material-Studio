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
			FileCommands()
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
	@Bindable private var app         = AppStore.shared

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
				.studioCanvas()
				.overlay {
					if StudioStore.shared.open {
						MaterialStudioView()
							.transition(.opacity)
					}
				}
				.animation(.easeOut(duration: 0.2), value: StudioStore.shared.open) // As the engine fades the preview
				.overlay(alignment: .top) {
					HistoryToastView()
				}
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
		.sheet(isPresented: $app.newProjectShown) {
			NewProjectSheet()
		}
	}
}

struct FileCommands: Commands {
	var body: some Commands {
		CommandGroup(replacing: .newItem) {
			Button("New Project…") {
				AppStore.shared.newProjectShown = true
			}
			.keyboardShortcut("n")
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
			Button(store.redoName.isEmpty ? "Redo" : "Redo \(store.redoName)") {
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
	@Bindable private var app = AppStore.shared

	var body: some View {
		ZStack {
			EngineCanvas()
				.ignoresSafeArea()
				.studioCanvas()
			if StudioStore.shared.open {
				MaterialStudioView()
					.transition(.opacity)
			}
			if AppStore.shared.ready && !AppStore.shared.engineModal && !ProjectsStore.shared.shown {
				PadControls()
					.transition(.opacity)
			}
			if ProjectsStore.shared.shown {
				ProjectsView()
					.transition(.opacity)
			}
			if !ProjectsStore.shared.shown {
				HistoryToastView()
			}
		}
			.animation(.easeOut(duration: 0.2), value: StudioStore.shared.open) // As the engine fades the preview
			.statusBarHidden()
			.persistentSystemOverlays(.hidden)
			.defersSystemGestures(on: .all)
			.sheet(isPresented: $app.newProjectShown) {
				NewProjectSheet()
			}
			.onOpenURL { url in
				iron_apple_open_url(url.absoluteString)
			}
			.onChange(of: scenePhase) { old, new in
				FrameDriver.shared.scenePhaseChanged(from: old, to: new)
			}
	}
}
#endif
