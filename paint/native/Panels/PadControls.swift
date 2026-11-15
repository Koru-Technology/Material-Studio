import SwiftUI
import simd

#if os(iOS)
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
	}
}

// MARK: - Menus

let viewportModes: [(Int32, String)] = [
	(0, "Lit"), (1, "Base Color"), (2, "Normal"), (3, "Occlusion"), (4, "Roughness"), (5, "Metallic"), (6, "Opacity"), (7, "Height"),
	(8, "Emission"), (9, "Subsurface"), (10, "TexCoord"), (11, "Object Normal"), (12, "World Normal"), (13, "Material ID"),
	(14, "Object ID"), (15, "Mask"), (16, "Path Traced"),
]

// Lit shows the light, path tracing its rays, the channel views a layered square
func viewportModeSymbol(_ mode: Int32) -> String {
	switch mode {
	case 0: "sun.max"
	case 16: "sparkles"
	default: "square.3.layers.3d.top.filled"
	}
}

private func run(_ command: app_command_t) {
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

				Menu {
					Picker("Viewport", selection: Binding(get: { store.viewportMode }, set: { m in store.perform { app_set_viewport_mode(m) } })) {
						ForEach(viewportModes, id: \.0) { mode in
							Text(mode.1).tag(mode.0)
						}
					}
				} label: {
					Image(systemName: viewportModeSymbol(store.viewportMode))
						.frame(width: 44, height: 44)
				}
				.glassEffect(.regular.interactive(), in: .circle)
				.accessibilityLabel("Viewport: \(viewportModes.first { $0.0 == store.viewportMode }?.1 ?? "Lit")")
			}
		}
		.foregroundStyle(.primary)
	}
}

struct PadPopoverBar: View {
	// Launch argument "-NativeShow Layers" opens that popover (screenshots from scripted runs)
	@State private var shown: String? = UserDefaults.standard.string(forKey: "NativeShow")

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
			shown = shown == name ? nil : name
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
		.glassEffect(shown == name ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .circle)
		.foregroundStyle(.primary)
		.accessibilityLabel(name)
		.popover(isPresented: Binding(get: { shown == name }, set: { if !$0, shown == name { shown = nil } })) {
			content()
				.frame(width: 380, height: 560)
				.presentationCompactAdaptation(.popover)
		}
	}
}

// MARK: - Tools

struct PadToolColumn: View {
	private var store = AppStore.shared
	@State private var propertiesShown = false

	var body: some View {
		GlassEffectContainer(spacing: 10) {
			VStack(spacing: 10) {
				VStack(spacing: 2) {
					ForEach(paintTools) { tool in
						Button {
							if store.tool == tool.id {
								propertiesShown.toggle() // A second tap on the tool opens its properties
							}
							else {
								store.perform { app_set_tool(tool.id) }
							}
						} label: {
							Image(systemName: tool.symbol)
								.font(.system(size: 18))
								.frame(width: 44, height: 40)
								.background(store.tool == tool.id ? Color.accentColor.opacity(0.35) : .clear, in: .rect(cornerRadius: 10))
						}
						.accessibilityLabel(tool.name)
						.popover(isPresented: Binding(get: { propertiesShown && store.tool == tool.id }, set: { propertiesShown = $0 }),
						         arrowEdge: .leading) {
							ToolPropertiesView()
								.frame(width: 320)
								.presentationCompactAdaptation(.popover)
						}
					}
				}
				.padding(4)
				.glassEffect(.regular, in: .rect(cornerRadius: 16))

				HStack(spacing: 8) {
					VerticalSlider(value: store.brushRadius, range: 0.01...2, symbol: "circle.fill") { v in
						app_set_brush_radius(v)
					}
					VerticalSlider(value: store.brushOpacity, range: 0...1, symbol: "circle.lefthalf.filled") { v in
						app_set_brush_opacity(v)
					}
				}
				.frame(height: 220)
			}
		}
		.foregroundStyle(.primary)
	}
}

// Undo and redo side by side in the bottom left corner
struct PadHistoryControls: View {
	private var store = AppStore.shared

	var body: some View {
		HStack(spacing: 2) {
			Button {
				store.perform { app_undo() }
			} label: {
				Image(systemName: "arrow.uturn.backward").frame(width: 44, height: 40)
			}
			.disabled(!store.canUndo)
			.accessibilityLabel("Undo")
			Button {
				store.perform { app_redo() }
			} label: {
				Image(systemName: "arrow.uturn.forward").frame(width: 44, height: 40)
			}
			.disabled(!store.canRedo)
			.accessibilityLabel("Redo")
		}
		.padding(4)
		.glassEffect(.regular, in: .capsule)
		.foregroundStyle(.primary)
	}
}

// A slider standing up, dragged anywhere on its track; the brush size and opacity
struct VerticalSlider: View {
	let value: Float
	let range: ClosedRange<Float>
	let symbol: String
	let set: (Float) -> Void

	@State private var start: Float?

	var body: some View {
		GeometryReader { geo in
			let f = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
			ZStack(alignment: .bottom) {
				Capsule().fill(.clear)
				Capsule()
					.fill(Color.accentColor.opacity(0.55))
					.frame(height: max(12, geo.size.height * f))
				Image(systemName: symbol)
					.font(.system(size: 10))
					.padding(.bottom, 8)
			}
			.contentShape(Capsule())
			.gesture(DragGesture(minimumDistance: 0)
				.onChanged { g in
					let base   = start ?? value
					start      = base
					let delta  = Float(-g.translation.height / geo.size.height) * (range.upperBound - range.lowerBound)
					let v      = min(range.upperBound, max(range.lowerBound, base + delta))
					AppStore.shared.perform { set(v) }
				}
				.onEnded { _ in start = nil })
		}
		.frame(width: 18)
		.padding(6)
		.glassEffect(.regular.interactive(), in: .capsule)
	}
}

// MARK: - View cube and frame

struct PadViewControls: View {
	private var store = AppStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 10) {
			HStack(alignment: .bottom, spacing: 10) {
				VStack(spacing: 10) {
					Button {
						run(APP_COMMAND_VIEW_FRAME)
					} label: {
						Image(systemName: "viewfinder")
							.frame(width: 44, height: 44)
					}
					.glassEffect(.regular.interactive(), in: .circle)
					.accessibilityLabel("Frame")
					// 3D view, then the 2D view alone, then both side by side (ui_base_view_mode: 0, 1, 2)
					Button {
						store.perform { app_cycle_view_mode() }
					} label: {
						Image(systemName: store.viewMode == 0 ? "cube" : store.viewMode == 1 ? "photo" : "rectangle.split.2x1")
							.frame(width: 44, height: 44)
					}
					.glassEffect(.regular.interactive(), in: .circle)
					.accessibilityLabel("Views")
				}
				.foregroundStyle(.primary)
				ViewCube()
					.frame(width: 96, height: 96)
					.glassEffect(.regular.interactive(), in: .circle)
			}
		}
	}
}

// The axes cube turns with the camera: tap a face to look at it, drag to orbit
struct ViewCube: View {
	private var store = AppStore.shared
	@State private var last: CGSize = .zero

	private static let faces: [(normal: SIMD3<Float>, label: String, color: Color)] = [
		(SIMD3(1, 0, 0), "Right", Color(red: 0.85, green: 0.54, blue: 0.53)),
		(SIMD3(-1, 0, 0), "Left", Color(red: 0.72, green: 0.44, blue: 0.42)),
		(SIMD3(0, 1, 0), "Back", Color(red: 0.57, green: 0.79, blue: 0.55)),
		(SIMD3(0, -1, 0), "Front", Color(red: 0.45, green: 0.66, blue: 0.44)),
		(SIMD3(0, 0, 1), "Top", Color(red: 0.55, green: 0.68, blue: 0.88)),
		(SIMD3(0, 0, -1), "Bottom", Color(red: 0.44, green: 0.56, blue: 0.76)),
	]

	// Face corners in view space (the camera's inverse rotation), the cube's half size 1
	private func projected(_ face: Int, _ inv: simd_quatf) -> (corners: [SIMD3<Float>], facing: Float) {
		let n         = Self.faces[face].normal
		let axis      = n.x != 0 ? 0 : n.y != 0 ? 1 : 2
		let u         = SIMD3<Float>(axis == 1 ? 1 : 0, axis == 2 ? 1 : 0, axis == 0 ? 1 : 0)
		let v         = simd_cross(n, u)
		let corners   = [n - u - v, n + u - v, n + u + v, n - u + v].map { inv.act($0) }
		return (corners, inv.act(n).z)
	}

	var body: some View {
		let r   = store.cameraRotation
		let inv = simd_quatf(ix: r.x, iy: r.y, iz: r.z, r: r.w).inverse
		Canvas { ctx, size in
			let half  = Float(size.width / 2)
			let scale = half * 0.48
			for i in Self.faces.indices {
				let (corners, facing) = projected(i, inv)
				if facing <= 0.05 {
					continue
				}
				var path = Path()
				for (k, c) in corners.enumerated() {
					let p = CGPoint(x: CGFloat(half + c.x * scale), y: CGFloat(half - c.y * scale))
					k == 0 ? path.move(to: p) : path.addLine(to: p)
				}
				path.closeSubpath()
				ctx.fill(path, with: .color(Self.faces[i].color.opacity(Double(0.45 + 0.55 * facing))))
				ctx.stroke(path, with: .color(.black.opacity(0.25)), lineWidth: 1)
				if facing > 0.5 {
					let c = corners.reduce(SIMD3<Float>(), +) / 4
					ctx.draw(Text(Self.faces[i].label).font(.system(size: 10, weight: .semibold)).foregroundStyle(.black.opacity(0.7)),
					         at: CGPoint(x: CGFloat(half + c.x * scale), y: CGFloat(half - c.y * scale)))
				}
			}
		}
		.contentShape(Circle())
		.gesture(DragGesture(minimumDistance: 4)
			.onChanged { g in
				let dx = Float(g.translation.width - last.width) * 0.02
				let dy = -Float(g.translation.height - last.height) * 0.02 // Drag up turns the view up
				last   = g.translation
				store.perform { app_view_orbit(dx, dy) }
			}
			.onEnded { _ in last = .zero })
		.onTapGesture { location in
			// The face nearest the viewer under the tap
			let half  = Float(48)
			let scale = half * 0.48
			let p     = SIMD2<Float>(Float(location.x) - half, half - Float(location.y)) / scale
			var best  = -1
			var depth = -Float.infinity
			for i in Self.faces.indices {
				let (corners, facing) = projected(i, inv)
				if facing <= 0.05 {
					continue
				}
				if contains(corners.map { SIMD2($0.x, $0.y) }, p) && facing > depth {
					best  = i
					depth = facing
				}
			}
			if best >= 0 {
				store.perform { app_view_face(Int32(best)) }
			}
		}
		.accessibilityLabel("View cube")
	}

	private func contains(_ poly: [SIMD2<Float>], _ p: SIMD2<Float>) -> Bool {
		var inside = false
		var j      = poly.count - 1
		for i in poly.indices {
			if (poly[i].y > p.y) != (poly[j].y > p.y),
			   p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x {
				inside.toggle()
			}
			j = i
		}
		return inside
	}
}
#endif

// MARK: - Tool properties (iPad popover, Mac inspector)

struct ToolPropertiesView: View {
	private var store = AppStore.shared

	var body: some View {
		Form {
			Section(paintTools.first { $0.id == store.tool }?.name ?? "Tool") {
				slider("Radius", store.brushRadius, 0.01...2) { app_set_brush_radius($0) }
				slider("Opacity", store.brushOpacity, 0...1) { app_set_brush_opacity($0) }
				if [0, 1, 5, 7].contains(store.tool) { // Brush, eraser, clone, particle
					slider("Hardness", store.brushHardness, 0...1) { app_set_brush_hardness($0) }
				}
				if [1, 5].contains(store.tool) { // Eraser, clone
					slider("Angle", store.brushAngle, 0...360) { app_set_brush_angle($0) }
				}
			}
			Section("Symmetry") {
				HStack {
					ForEach(0..<3, id: \.self) { axis in
						Toggle(["X", "Y", "Z"][axis], isOn: Binding(get: { store.symmetry[axis] }, set: { on in
							var s  = store.symmetry
							s[axis] = on
							store.perform { app_set_symmetry(s[0], s[1], s[2]) }
						}))
						.toggleStyle(.button)
					}
				}
			}
		}
		.formStyle(.grouped)
	}

	private func slider(_ title: String, _ value: Float, _ range: ClosedRange<Float>, set: @escaping (Float) -> Void) -> some View {
		LabeledContent {
			Slider(value: Binding(get: { value }, set: { v in store.perform { set(v) } }), in: range)
		} label: {
			Text(title)
		}
	}
}
