import SwiftUI

#if os(iOS)
import UIKit

// iPad: the canvas fills the screen, the controls float over it on Liquid Glass.
// Top left: actions and viewport menus. Top right: library popovers. Left: tools, size, opacity.
// Bottom left: undo, redo. Bottom right: frame, views, view cube.
struct PadControls: View {
	var body: some View {
		ZStack {
			VStack {
				HStack(alignment: .top) {
					PadMenuBar()
					Spacer()
					PadPopoverBar()
				}
				Spacer()
			}
			HStack {
				PadToolColumn()
				Spacer()
			}
			VStack {
				Spacer()
				HStack(alignment: .bottom) {
					PadHistoryControls()
					Spacer()
					PadViewControls()
				}
			}
		}
		.padding(16)
		.background {
			// While a popover is open, a touch on the canvas only closes it (it does not paint)
			if PadPopovers.shared.shown != nil {
				Color.clear
					.contentShape(.rect)
					.ignoresSafeArea()
					.gesture(DragGesture(minimumDistance: 0).onChanged { _ in
						PadPopovers.shared.close()
					})
			}
		}
		.onDisappear {
			PadPopovers.shared.close() // An editor over the canvas took the screen: its popover went with the controls
		}
	}
}

// One popover of the controls at a time, by name. A tap on another control's button switches in one tap: the open
// popover lets the controls behind it take taps (padPopover), so the tap reaches the button, and the same tap closes
// the popover. Presenting while one is still closing does nothing, so the next one opens once the closing one has gone.
// A tap on the open popover's own button only closes it. PadControls catches taps on the canvas.
@Observable
final class PadPopovers {
	static let shared = PadPopovers()

	// Launch argument "-NativeShow Layers" opens that popover (screenshots from scripted runs)
	private(set) var shown: String? = UserDefaults.standard.string(forKey: "NativeShow")
	@ObservationIgnored private var closing: String? // Going away, until UIKit presents nothing any more
	@ObservationIgnored private var pending: String? // Opens once the closing one has gone

	// The button's tap: the outside tap may have closed its popover just before (closing), or closes it just after
	func toggle(_ name: String) {
		if shown == name {
			close()
		}
		else if let c = closing {
			pending = c == name ? nil : name // Its own button: it stays closed
		}
		else if shown != nil {
			pending = name
			startClosing()
		}
		else {
			shown = name
		}
	}

	func close() {
		pending = nil
		startClosing()
	}

	func isPresented(_ name: String) -> Binding<Bool> {
		Binding(get: { self.shown == name }, set: { if !$0 && self.shown == name { self.startClosing() } })
	}

	private func startClosing() {
		guard let name = shown else {
			return
		}
		closing = name
		shown   = nil
		waitUntilGone(name, polls: 0)
	}

	// The closing popover is gone once UIKit presents nothing any more (SwiftUI's onDisappear comes before its
	// dismissal ends, a popover opened then is dropped): the one asked for meanwhile opens then
	private func waitUntilGone(_ name: String, polls: Int) {
		DispatchQueue.main.asyncAfter(deadline: .now() + 1.0 / 60.0) { [weak self] in
			guard let self, self.closing == name else {
				return
			}
			if Self.presenting() && polls < 90 { // 1.5 s at most
				self.waitUntilGone(name, polls: polls + 1)
				return
			}
			self.closing = nil
			if self.shown == nil, let p = self.pending {
				self.pending = nil
				self.shown   = p
			}
		}
	}

	// A popover (or another presentation) is up or still going away
	private static func presenting() -> Bool {
		for scene in UIApplication.shared.connectedScenes {
			for window in (scene as? UIWindowScene)?.windows ?? [] where window.rootViewController?.presentedViewController != nil {
				return true
			}
		}
		return false
	}
}

extension View {
	// A popover of the controls (PadPopovers): the controls behind it stay usable
	func padPopover<Content: View>(_ name: String, arrowEdge: Edge? = nil, @ViewBuilder content: @escaping () -> Content) -> some View {
		popover(isPresented: PadPopovers.shared.isPresented(name), arrowEdge: arrowEdge) {
			content()
				.presentationCompactAdaptation(.popover)
				.presentationBackgroundInteraction(.enabled)
		}
	}
}

// MARK: - Menus

let viewportModes: [(Int32, String)] = [
	(APP_VIEWPORT_LIT, "Lit"), (APP_VIEWPORT_BASE_COLOR, "Base Color"), (APP_VIEWPORT_NORMAL_MAP, "Normal"),
	(APP_VIEWPORT_OCCLUSION, "Occlusion"), (APP_VIEWPORT_ROUGHNESS, "Roughness"), (APP_VIEWPORT_METALLIC, "Metallic"),
	(APP_VIEWPORT_OPACITY, "Opacity"), (APP_VIEWPORT_HEIGHT, "Height"), (APP_VIEWPORT_EMISSION, "Emission"),
	(APP_VIEWPORT_SUBSURFACE, "Subsurface"), (APP_VIEWPORT_TEXCOORD, "TexCoord"), (APP_VIEWPORT_OBJECT_NORMAL, "Object Normal"),
	(APP_VIEWPORT_WORLD_NORMAL, "World Normal"), (APP_VIEWPORT_MATERIAL_ID, "Material ID"), (APP_VIEWPORT_OBJECT_ID, "Object ID"),
	(APP_VIEWPORT_MASK, "Mask"), (APP_VIEWPORT_PATH_TRACE, "Path Traced"),
].map { (Int32($0.0.rawValue), $0.1) }

// Lit shows the light, path tracing its rays, the channel views a layered square
func viewportModeSymbol(_ mode: Int32) -> String {
	switch mode {
	case Int32(APP_VIEWPORT_LIT.rawValue): "sun.max"
	case Int32(APP_VIEWPORT_PATH_TRACE.rawValue): "sparkles"
	default: "square.3.layers.3d.top.filled"
	}
}

func run(_ command: app_command_t) {
	AppStore.shared.perform { app_command(Int32(command.rawValue)) }
}

struct PadMenuBar: View {
	private var store = AppStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 8) {
			HStack(spacing: 8) {
				Menu {
					Section {
						Button("New Project…", systemImage: "doc.badge.plus") { run(APP_COMMAND_NEW_PROJECT) }
						Button("Open…", systemImage: "folder") { run(APP_COMMAND_OPEN) }
						Button("Projects…", systemImage: "square.grid.2x2") { run(APP_COMMAND_OPEN_RECENT) }
						Button("Save", systemImage: "square.and.arrow.down") { run(APP_COMMAND_SAVE) }
						Button("Save As…", systemImage: "square.and.arrow.down.on.square") { run(APP_COMMAND_SAVE_AS) }
					}
					Menu("Import", systemImage: "square.and.arrow.down.on.square") {
						Button("Mesh…", systemImage: "cube") { run(APP_COMMAND_IMPORT_MESH) }
						Button("Texture…", systemImage: "photo") { run(APP_COMMAND_IMPORT_TEXTURE) }
						Button("Material…", systemImage: "circle.circle") { run(APP_COMMAND_IMPORT_MATERIAL) }
						Button("Brush…", systemImage: "paintbrush") { run(APP_COMMAND_IMPORT_BRUSH) }
						Button("Font…", systemImage: "textformat") { run(APP_COMMAND_IMPORT_FONT) }
						Button("Swatches…", systemImage: "swatchpalette") { run(APP_COMMAND_IMPORT_SWATCHES) }
					}
					Menu("Export", systemImage: "square.and.arrow.up") {
						Button("Textures…", systemImage: "photo.stack") { run(APP_COMMAND_EXPORT_TEXTURES) }
						Button("Mesh…", systemImage: "cube") { run(APP_COMMAND_EXPORT_MESH) }
						Button("Swatches…", systemImage: "swatchpalette") { run(APP_COMMAND_EXPORT_SWATCHES) }
					}
					Section {
						Button("Reimport Mesh", systemImage: "arrow.triangle.2.circlepath") { run(APP_COMMAND_REIMPORT_MESH) }
						Button("Reimport Textures", systemImage: "arrow.triangle.2.circlepath") { run(APP_COMMAND_REIMPORT_TEXTURES) }
						Button("Bake Material…", systemImage: "flame") { run(APP_COMMAND_BAKE_MATERIAL) }
						Button("Bake Mesh Maps…", systemImage: "flame") { run(APP_COMMAND_BAKE_MESH_MAPS) }
						Button("Delete Unused Assets", systemImage: "trash") { run(APP_COMMAND_DELETE_UNUSED) }
					}
					Section {
						Button("Capture Screenshot", systemImage: "camera") { run(APP_COMMAND_SCREENSHOT) }
						Button("Settings…", systemImage: "gear") { run(APP_COMMAND_PREFERENCES) }
					}
				} label: {
					Image(systemName: "ellipsis")
						.frame(width: 44, height: 44)
				}
				.glassEffect(.regular.interactive(), in: .circle)
				.accessibilityLabel("Actions")
			}
		}
		.foregroundStyle(.primary)
	}
}

// Lit, path traced or a channel: a popover list, the 17 modes overflow a menu, which does not scroll
struct ViewportButton: View {
	private var store = AppStore.shared

	var body: some View {
		Button {
			PadPopovers.shared.toggle("Viewport")
		} label: {
			Image(systemName: viewportModeSymbol(store.viewportMode))
				.frame(width: 44, height: 44)
		}
		.glassEffect(.regular.interactive(), in: .circle)
		.accessibilityLabel("Viewport: \(viewportModes.first { $0.0 == store.viewportMode }?.1 ?? "Lit")")
		.padPopover("Viewport") {
			ViewportModeList {
				PadPopovers.shared.close()
			}
			.frame(width: 240, height: 520)
		}
	}
}

struct ViewportModeList: View {
	private var store = AppStore.shared
	let picked: () -> Void

	init(picked: @escaping () -> Void) {
		self.picked = picked
	}

	var body: some View {
		List(viewportModes, id: \.0) { mode in
			Button {
				store.perform { app_set_viewport_mode(mode.0) }
				picked()
			} label: {
				HStack {
					Text(mode.1)
						.foregroundStyle(Color.primary)
					Spacer()
					if store.viewportMode == mode.0 {
						Image(systemName: "checkmark")
							.foregroundStyle(Color.accentColor)
					}
				}
			}
		}
		.listStyle(.plain)
		.scrollContentBackground(.hidden)
	}
}

struct PadPopoverBar: View {
	private var popovers = PadPopovers.shared

	var body: some View {
		GlassEffectContainer(spacing: 8) {
			HStack(spacing: 8) {
				item("Brushes", "paintbrush.pointed") { BrushesPopover() }
				item("Materials", "circle.circle", image: MaterialsStore.shared.selectedMaterial.flatMap { MaterialsStore.shared.thumbnail(for: $0) }) {
					MaterialsPopover()
				}
				item("Layers", "square.3.layers.3d") { LayersView() }
				item("Color", "paintpalette") { ColorPopover() }
			}
		}
	}

	// image: the button shows it instead of the symbol (the current material's sphere)
	private func item<Content: View>(_ name: String, _ symbol: String, image: CGImage? = nil,
	                                 @ViewBuilder content: @escaping () -> Content) -> some View {
		Button {
			popovers.toggle(name)
		} label: {
			Group {
				if let image {
					Image(decorative: image, scale: 1)
						.resizable()
						.scaledToFit()
						.frame(width: 34, height: 34)
				}
				else {
					Image(systemName: symbol)
				}
			}
			.frame(width: 44, height: 44)
		}
		.glassEffect(popovers.shown == name ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .circle)
		.foregroundStyle(.primary)
		.accessibilityLabel(name)
		.padPopover(name) {
			content()
				.frame(width: 380, height: 560)
		}
	}
}
#endif
