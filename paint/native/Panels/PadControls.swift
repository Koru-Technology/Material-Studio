import SwiftUI

#if os(iOS)
import UIKit

// iPad: the canvas fills the screen, the controls float over it on Liquid Glass.
// Top left: actions and viewport menus. Top right: the layer painted, library popovers. Left: tools, size, opacity.
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
				if IDPickStore.shared.active { // Picking ID Selection parts: their list in place of the brush sliders
					IDPartsPanel()
						.transition(.opacity)
				}
				else {
					PadBrushControls()
				}
			}
			.animation(.easeOut(duration: 0.15), value: IDPickStore.shared.active)
			VStack {
				Spacer()
				HStack(alignment: .bottom) {
					PadHistoryControls()
					Spacer()
					PadViewControls()
				}
			}
			VStack {
				Spacer()
				bottomBar
			}
			.animation(.easeOut(duration: 0.15), value: bottomBarKind)
		}
		.padding(16)
		.padPanelHost()
		.background {
			// While a panel is open, a touch on the canvas only closes it (it does not paint)
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
			PadPopovers.shared.close() // An editor over the canvas took the screen: its panel went with the controls
		}
	}
}

// Bottom middle, one at a time: a curve's buttons, Apply Material while the Materials panel is open, the Decal tool's
// image, a fill layer's effects
extension PadControls {
	private enum BottomBar: Equatable {
		case none, curve, applyMaterial, decal, effects(Int32)
	}

	private var bottomBarKind: BottomBar {
		if CurveStore.shared.editing {
			return .curve
		}
		if PadPopovers.shared.shown == "Materials" {
			return .applyMaterial
		}
		if AppStore.shared.tool == APP_TOOL_DECAL.id {
			return .decal
		}
		if let layer = LayersStore.shared.selectedRowLayer, layer.isFill, layer.kind == .layer {
			return .effects(layer.id)
		}
		return .none
	}

	@ViewBuilder
	private var bottomBar: some View {
		switch bottomBarKind {
		case .curve:
			CurveButtons()
				.transition(.opacity)
		case .applyMaterial:
			ApplyMaterialBar()
				.transition(.opacity)
		case .decal:
			DecalBar()
				.transition(.opacity)
		case .effects(let id):
			if let layer = LayersStore.shared.layer(id) {
				FillEffectsBar(layer: layer)
					.transition(.opacity)
			}
		case .none:
			EmptyView()
		}
	}
}

// One panel of the controls at a time, by name: the libraries, the camera, the viewport modes, a tool's properties.
// They are drawn here over the controls rather than presented as system popovers: a system popover takes the first tap
// outside it to close, and a popover asked to open while another one closes is dropped. Here a tap on another button
// switches at once (no animation), only opening from none and closing to none animate. A tap on the canvas closes.
@Observable
final class PadPopovers {
	static let shared = PadPopovers()

	// Launch argument "-NativeShow Layers" opens that panel (screenshots from scripted runs)
	private(set) var shown: String? = UserDefaults.standard.string(forKey: "NativeShow")

	func toggle(_ name: String) {
		if shown == name {
			close()
		}
		else if shown != nil {
			var t                = Transaction()
			t.disablesAnimations = true
			withTransaction(t) {
				shown = name
			}
		}
		else {
			withAnimation(.snappy(duration: 0.22)) {
				shown = name
			}
		}
	}

	func close() {
		guard shown != nil else {
			return
		}
		withAnimation(.easeOut(duration: 0.16)) {
			shown = nil
		}
	}
}

// A button's panel: where the button is, the side it opens to, what it shows
struct PadPanel {
	let anchor: Anchor<CGRect>
	let edge: Edge? // .leading: right of the button, nil: below it, or above it in the lower half of the screen
	let content: AnyView
}

struct PadPanelKey: PreferenceKey {
	static var defaultValue: [String: PadPanel] = [:]

	static func reduce(value: inout [String: PadPanel], nextValue: () -> [String: PadPanel]) {
		value.merge(nextValue()) { _, new in new }
	}
}

extension View {
	// The button opens the panel named name (PadPopovers.toggle), PadPanelHost draws it
	func padPopover<Content: View>(_ name: String, arrowEdge: Edge? = nil, @ViewBuilder content: @escaping () -> Content) -> some View {
		anchorPreference(key: PadPanelKey.self, value: .bounds) { anchor in
			[name: PadPanel(anchor: anchor, edge: arrowEdge, content: AnyView(content()))]
		}
	}

	// Draws the open panel next to its button
	func padPanelHost() -> some View {
		overlayPreferenceValue(PadPanelKey.self) { panels in
			PadPanelHost(panels: panels)
		}
	}
}

struct PadPanelHost: View {
	let panels: [String: PadPanel]
	private var popovers = PadPopovers.shared

	init(panels: [String: PadPanel]) {
		self.panels = panels
	}

	private let gap: CGFloat    = 10
	private let margin: CGFloat = 8

	var body: some View {
		GeometryReader { geo in
			ZStack(alignment: .topLeading) {
				Color.clear
					.allowsHitTesting(false)
				if let name = popovers.shown, let panel = panels[name] {
					let button = geo[panel.anchor]
					let below  = button.midY < geo.size.height / 2
					PadPanelContent(content: panel.content)
						.background(Color(uiColor: PadPanelContent.background))
						.clipShape(.rect(cornerRadius: 24, style: .continuous))
						.overlay {
							RoundedRectangle(cornerRadius: 24, style: .continuous)
								.strokeBorder(.white.opacity(0.12), lineWidth: 1)
								.allowsHitTesting(false)
						}
						.shadow(color: .black.opacity(0.3), radius: 24, y: 8)
						.alignmentGuide(.leading) { d in
							let x = panel.edge == .leading ? button.maxX + gap
							      : panel.edge == .trailing ? button.minX - gap - d.width : button.midX - d.width / 2
							return -min(max(x, margin), geo.size.width - d.width - margin)
						}
						.alignmentGuide(.top) { d in
							let side = panel.edge == .leading || panel.edge == .trailing
							let y    = side ? button.midY - d.height / 2 : below ? button.maxY + gap : button.minY - gap - d.height
							return -min(max(y, margin), geo.size.height - d.height - margin)
						}
						.transition(.opacity.combined(with: .scale(scale: 0.96, anchor: panel.edge == .leading ? .leading
						                                                             : panel.edge == .trailing ? .trailing : below ? .top : .bottom)))
						.id(name)
				}
			}
		}
		.ignoresSafeArea(.keyboard) // The panel stays where it is when the keyboard comes up
	}
}

// A panel's content with the colors of a popover: in the elevated interface level the system backgrounds are gray, not
// black (lists, forms and navigation bars use them)
struct PadPanelContent: UIViewControllerRepresentable {
	let content: AnyView

	static let background = UIColor.systemBackground.resolvedColor(with: UITraitCollection(traitsFrom: [
		UITraitCollection(userInterfaceStyle: .dark), UITraitCollection(userInterfaceLevel: .elevated),
	]))

	func makeUIViewController(context: Context) -> UIHostingController<AnyView> {
		let host                                = UIHostingController(rootView: content)
		host.sizingOptions                      = [.intrinsicContentSize]
		// The panel stays put under the keyboard (PadPanelHost): its content must not avoid it either. Avoiding it in here
		// laid the panel and its navigation stack out forever once the keyboard came up (a text field or a text alert
		// with a panel open), freezing the app.
		host.safeAreaRegions                    = []
		host.traitOverrides.userInterfaceLevel  = .elevated
		host.view.backgroundColor               = .clear
		return host
	}

	func updateUIViewController(_ host: UIHostingController<AnyView>, context: Context) {
		host.rootView = content
	}

	func sizeThatFits(_ proposal: ProposedViewSize, uiViewController host: UIHostingController<AnyView>, context: Context) -> CGSize? {
		host.sizeThatFits(in: CGSize(width: proposal.width ?? .infinity, height: proposal.height ?? .infinity))
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
	@State private var exporting = false // The export sheet (tiled export), the engine's export box otherwise
	@State private var settings  = false // Project settings

	var body: some View {
		GlassEffectContainer(spacing: 8) {
			HStack(spacing: 8) {
				Menu {
					Section {
						Button("New Project…", systemImage: "doc.badge.plus") { AppStore.shared.newProjectShown = true }
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
						Button("Textures…", systemImage: "photo.stack") {
							exporting = true
						}
						Button("Mesh…", systemImage: "cube") { run(APP_COMMAND_EXPORT_MESH) }
						Button("Swatches…", systemImage: "swatchpalette") { run(APP_COMMAND_EXPORT_SWATCHES) }
					}
					Section {
						Button("Reimport Mesh", systemImage: "arrow.triangle.2.circlepath") { run(APP_COMMAND_REIMPORT_MESH) }
						Button("Reimport Textures", systemImage: "arrow.triangle.2.circlepath") { run(APP_COMMAND_REIMPORT_TEXTURES) }
						Button("Bake Material…", systemImage: "flame") { run(APP_COMMAND_BAKE_MATERIAL) }
						Button("Bake Mesh Maps…", systemImage: "flame") { AppStore.shared.bakeShown = true }
						Button("Delete Unused Assets", systemImage: "trash") { run(APP_COMMAND_DELETE_UNUSED) }
					}
					Section {
						Button("Capture Screenshot", systemImage: "camera") { run(APP_COMMAND_SCREENSHOT) }
						Button("Project Settings…", systemImage: "slider.horizontal.3") { settings = true }
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
		.sheet(isPresented: $exporting) {
			ExportSheet()
		}
		.sheet(isPresented: $settings) {
			ProjectSettingsSheet()
		}
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
				layersItem
				item("Brushes", "paintbrush.pointed") { BrushesPopover() }
				item("Materials", "circle.circle", image: MaterialsStore.shared.selectedMaterial.flatMap { MaterialsStore.shared.thumbnail(for: $0) }) {
					MaterialsPopover()
				}
				item("Color", "paintpalette") { ColorPopover() } // Next to the material: the color applies to it
			}
		}
	}

	// The layer painted (the layer of a selected mask): its thumbnail and name
	private var layersItem: some View {
		let layer = LayersStore.shared.selectedRowLayer
		return Button {
			popovers.toggle("Layers")
		} label: {
			HStack(spacing: 8) {
				if let layer, layer.kind != .group {
					LayerThumbnail(id: layer.id, mask: false, size: 30)
				}
				else {
					Image(systemName: "square.3.layers.3d")
						.frame(width: 30, height: 30)
				}
				Text(layer?.name ?? "Layers")
					.font(.body.weight(.medium))
					.lineLimit(1)
					.truncationMode(.middle)
					.frame(maxWidth: 160, alignment: .leading)
					.fixedSize() // As wide as the name, up to 160
			}
			.padding(.leading, 7)
			.padding(.trailing, 14)
			.frame(height: 44)
		}
		.glassEffect(popovers.shown == "Layers" ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .capsule)
		.foregroundStyle(.primary)
		.accessibilityLabel("Layers: \(layer?.name ?? "")")
		.padPopover("Layers") {
			LayersView()
				.frame(width: 380, height: 560)
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
