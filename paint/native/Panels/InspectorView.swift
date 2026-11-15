import SwiftUI

struct Tool: Identifiable {
	let id: Int32
	let name: String
	let symbol: String
}

// tool_type_t values, the painting tools the toolbar offers
let paintTools: [Tool] = [
	Tool(id: 0, name: "Brush", symbol: "paintbrush.pointed"),
	Tool(id: 1, name: "Eraser", symbol: "eraser"),
	Tool(id: 2, name: "Fill", symbol: "drop"),
	Tool(id: 7, name: "Particle", symbol: "sparkles"),
	Tool(id: 9, name: "Picker", symbol: "eyedropper"),
	Tool(id: 5, name: "Clone", symbol: "square.on.square.dashed"),
	Tool(id: 6, name: "Blur", symbol: "drop.halffull"),
	Tool(id: 3, name: "Decal", symbol: "seal"),
	Tool(id: 4, name: "Text", symbol: "textformat"),
]

struct InspectorView: View {
	private var store = AppStore.shared

	var body: some View {
		Form {
			Section("Tool") {
				Picker("Tool", selection: Binding(get: { store.tool }, set: { t in store.perform { app_set_tool(t) } })) {
					ForEach(paintTools) { tool in
						Label(tool.name, systemImage: tool.symbol).tag(tool.id)
					}
				}
				slider("Radius", value: store.brushRadius, range: 0.01...2) { app_set_brush_radius($0) }
				slider("Opacity", value: store.brushOpacity, range: 0...1) { app_set_brush_opacity($0) }
				slider("Hardness", value: store.brushHardness, range: 0...1) { app_set_brush_hardness($0) }
			}
			if let layer = LayersStore.shared.selectedLayer {
				Section("Layer") {
					LabeledContent("Name", value: layer.name)
					Slider(value: Binding(get: { Double(layer.opacity) }, set: { v in
						store.perform { _ = app_layer_set_opacity(layer.id, Float(v)) }
					}), in: 0...1) {
						Text("Opacity")
					} onEditingChanged: { editing in
						if editing {
							app_layer_opacity_begin(layer.id)
						}
						else {
							app_edit_end()
						}
					}
				}
			}
		}
		.formStyle(.grouped)
	}

	private func slider(_ title: String, value: Float, range: ClosedRange<Double>, set: @escaping (Float) -> Void) -> some View {
		Slider(value: Binding(get: { Double(value) }, set: { v in store.perform { set(Float(v)) } }), in: range) {
			Text(title)
		}
	}
}
