import SwiftUI

struct LayersView: View {
	private var store  = AppStore.shared
	private var layers = LayersStore.shared

	var body: some View {
		List(selection: Binding(get: { layers.selectedLayerID }, set: { id in
			if let id {
				store.perform { _ = app_layer_select(id) }
			}
		})) {
			ForEach(layers.layers) { layer in
				LayerRowView(layer: layer, thumbnail: layers.thumbnail(for: layer.id), version: layers.thumbnailsVersion)
					.tag(layer.id)
			}
		}
		.safeAreaInset(edge: .bottom) {
			HStack {
				Button {
					store.perform { _ = app_layer_add() }
				} label: {
					Label("New Layer", systemImage: "plus")
				}
				Button {
					store.perform { _ = app_layer_delete(layers.selectedLayerID) }
				} label: {
					Label("Delete Layer", systemImage: "trash")
				}
				.disabled(!app_layer_can_delete(layers.selectedLayerID))
				Spacer()
			}
			.labelStyle(.iconOnly)
			.buttonStyle(.borderless)
			.padding(8)
		}
	}
}

private struct LayerRowView: View {
	let layer: LayerRow
	let thumbnail: CGImage?
	let version: UInt32 // Redraws the row when the thumbnails change

	var body: some View {
		HStack(spacing: 8) {
			Group {
				if layer.kind == .group {
					Image(systemName: "folder")
						.font(.title3)
				}
				else if let thumbnail {
					Image(decorative: thumbnail, scale: 1)
						.resizable()
						.interpolation(.medium)
				}
				else {
					Color.secondary.opacity(0.2)
				}
			}
			.frame(width: 36, height: 36)
			.clipShape(.rect(cornerRadius: 4))
			Text(layer.name)
				.lineLimit(1)
			Spacer()
			Button {
				AppStore.shared.perform { _ = app_layer_set_visible(layer.id, !layer.visible) }
			} label: {
				Image(systemName: layer.visible ? "eye" : "eye.slash")
					.foregroundStyle(layer.visible ? .primary : .secondary)
			}
			.buttonStyle(.borderless)
		}
		.padding(.leading, CGFloat(layer.depth) * 16)
	}
}
