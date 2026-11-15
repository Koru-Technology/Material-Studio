import SwiftUI

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
