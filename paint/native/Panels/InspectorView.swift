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
			}
			ToolPropertiesSections() // As the iPad's tool popover
			if let layer = LayersStore.shared.selectedLayer {
				// Layer, fill, height blend and channels sections (LayersDetail.swift)
				LayerDetailSections(layerID: layer.id)
			}
		}
		.formStyle(.grouped)
	}
}
