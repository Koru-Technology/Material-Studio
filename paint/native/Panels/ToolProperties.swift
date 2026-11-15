import SwiftUI

struct Tool: Identifiable {
	let id: Int32 // app_tool_t
	let name: String
	let symbol: String

	init(_ tool: app_tool_t, _ name: String, _ symbol: String) {
		id = tool.id
		self.name = name
		self.symbol = symbol
	}
}

extension app_tool_t {
	var id: Int32 {
		Int32(rawValue)
	}
}

// The painting tools the toolbar offers
let paintTools: [Tool] = [
	Tool(APP_TOOL_BRUSH, "Brush", "paintbrush.pointed"),
	Tool(APP_TOOL_ERASER, "Eraser", "eraser"),
	Tool(APP_TOOL_FILL, "Fill", "drop"),
	Tool(APP_TOOL_PARTICLE, "Particle", "sparkles"),
	Tool(APP_TOOL_PICKER, "Picker", "eyedropper"),
	Tool(APP_TOOL_CLONE, "Clone", "square.on.square.dashed"),
	Tool(APP_TOOL_BLUR, "Blur", "drop.halffull"),
	Tool(APP_TOOL_DECAL, "Decal", "seal"),
	Tool(APP_TOOL_TEXT, "Text", "textformat"),
]

// MARK: - Tool properties (iPad popover, Mac inspector)

struct ToolPropertiesView: View {
	var body: some View {
		Form {
			ToolPropertiesSections()
		}
		.formStyle(.grouped)
	}
}

// The brush and symmetry sections, in the iPad popover's form and the Mac inspector's
struct ToolPropertiesSections: View {
	private var store = AppStore.shared

	var body: some View {
		Group {
			Section(paintTools.first { $0.id == store.tool }?.name ?? "Tool") {
				slider("Radius", store.brushRadius, 0.01...2, radiusText) { app_set_brush_radius($0) }
				slider("Opacity", store.brushOpacity, 0...1, percent) { app_set_brush_opacity($0) }
				if [APP_TOOL_BRUSH, APP_TOOL_ERASER, APP_TOOL_CLONE, APP_TOOL_PARTICLE].map(\.id).contains(store.tool) {
					slider("Hardness", store.brushHardness, 0...1, percent) { app_set_brush_hardness($0) }
				}
				if [APP_TOOL_ERASER, APP_TOOL_CLONE].map(\.id).contains(store.tool) {
					slider("Angle", store.brushAngle, 0...360, { "\(Int($0.rounded()))°" }) { app_set_brush_angle($0) }
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
