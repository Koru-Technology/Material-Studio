import SwiftUI

// What a tool does to the layer; the toolbar colors its tools by it
enum ToolGroup {
	case paint, place, remove, retouch, sample

	var color: Color {
		switch self {
		case .paint:   .blue
		case .place:   .purple
		case .remove:  .pink
		case .retouch: .orange
		case .sample:  .teal
		}
	}
}

// The brush settings a tool reads (make_paint.c): only these show for it
struct ToolControls: OptionSet {
	let rawValue: Int

	static let radius   = ToolControls(rawValue: 1)
	static let opacity  = ToolControls(rawValue: 2)
	static let hardness = ToolControls(rawValue: 4)
	static let symmetry = ToolControls(rawValue: 8)
	static let stroke: ToolControls = [.radius, .opacity, .hardness, .symmetry]
}

struct Tool: Identifiable {
	let id: Int32 // app_tool_t
	let name: String
	let symbol: String
	let group: ToolGroup
	let controls: ToolControls

	init(_ tool: app_tool_t, _ name: String, _ symbol: String, _ group: ToolGroup, _ controls: ToolControls) {
		id = tool.id
		self.name = name
		self.symbol = symbol
		self.group = group
		self.controls = controls
	}

	static func selected(_ id: Int32) -> Tool? {
		paintTools.first { $0.id == id }
	}
}

extension app_tool_t {
	var id: Int32 {
		Int32(rawValue)
	}
}

// The painting tools the toolbar offers
let paintTools: [Tool] = [
	Tool(APP_TOOL_BRUSH, "Brush", "pencil.and.scribble", .paint, .stroke),
	Tool(APP_TOOL_PARTICLE, "Particle", "wand.and.stars", .paint, .stroke),
	Tool(APP_TOOL_FILL, "Fill", "square.dashed.inset.filled", .paint, [.opacity]),
	Tool(APP_TOOL_DECAL, "Decal", "star.square.on.square", .place, [.radius, .opacity, .symmetry]),
	Tool(APP_TOOL_TEXT, "Text", "text.cursor", .place, [.radius, .opacity, .symmetry]),
	Tool(APP_TOOL_ERASER, "Eraser", "eraser.line.dashed", .remove, .stroke),
	// Hidden for now: nothing on the iPad sets its source (Alt + paint), and it reads only the layer painted
	// Tool(APP_TOOL_CLONE, "Clone", "square.on.square.dashed", .retouch, .stroke),
	Tool(APP_TOOL_BLUR, "Blur", "circle.dotted.and.circle", .retouch, .stroke),
	Tool(APP_TOOL_PICKER, "Picker", "eyedropper", .sample, []),
]

// MARK: - Tool properties (Mac inspector)

// The brush and symmetry sections of the tool, as the iPad's controls on the right edge show them
struct ToolPropertiesSections: View {
	private var store = AppStore.shared

	var body: some View {
		let tool     = Tool.selected(store.tool)
		let controls = tool?.controls ?? []
		Group {
			if !controls.subtracting(.symmetry).isEmpty {
				Section(tool?.name ?? "Tool") {
					if controls.contains(.radius) {
						slider("Radius", store.brushRadius, 0.01...2, radiusText) { app_set_brush_radius($0) }
					}
					if controls.contains(.opacity) {
						slider("Opacity", store.brushOpacity, 0...1, percent) { app_set_brush_opacity($0) }
					}
					if controls.contains(.hardness) {
						slider("Hardness", store.brushHardness, 0...1, percent) { app_set_brush_hardness($0) }
					}
				}
			}
			if controls.contains(.symmetry) {
				Section("Symmetry") {
					HStack {
						ForEach(0..<3, id: \.self) { axis in
							Toggle(symmetryAxes[axis].name, isOn: symmetryBinding(axis))
								.toggleStyle(.button)
						}
					}
				}
			}
		}
	}

	private func slider(_ title: String, _ value: Float, _ range: ClosedRange<Float>, _ format: @escaping (Float) -> String,
	                    set: @escaping (Float) -> Void) -> some View {
		LabeledContent {
			TipSlider(title: title, value: value, range: range, format: format, set: set)
		} label: {
			Text(title)
		}
	}
}

// The mirror axes, colored as the view cube's faces
let symmetryAxes: [(name: String, color: Color)] = [
	("X", Color(red: 0.95, green: 0.45, blue: 0.43)),
	("Y", Color(red: 0.50, green: 0.85, blue: 0.48)),
	("Z", Color(red: 0.50, green: 0.68, blue: 0.98)),
]

@MainActor func symmetryBinding(_ axis: Int) -> Binding<Bool> {
	Binding(get: { AppStore.shared.symmetry[axis] }, set: { on in
		var s   = AppStore.shared.symmetry
		s[axis] = on
		AppStore.shared.perform { app_set_symmetry(s[0], s[1], s[2]) }
	})
}
