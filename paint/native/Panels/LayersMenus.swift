import SwiftUI

// The "+" menu: the layer kinds of the engine's layers tab (tab_layers_button_new_menu)
struct NewLayerMenu: View {
	private var store  = AppStore.shared
	private var layers = LayersStore.shared

	var body: some View {
		Menu {
			Section {
				item("Paint Layer", "paintbrush", APP_LAYER_NEW_PAINT)
				item("Fill Layer", "drop.fill", APP_LAYER_NEW_FILL)
				item("Color Layer", "paintpalette", APP_LAYER_NEW_COLOR)
				item("Decal", "seal", APP_LAYER_NEW_DECAL)
			}
			Section {
				item("Text", "textformat", APP_LAYER_NEW_TEXT)
				item("Path", "scribble", APP_LAYER_NEW_PATH)
				item("Curve", "point.topleft.down.to.point.bottomright.curvepath", APP_LAYER_NEW_CURVE)
			}
			Section {
				item("Group", "folder.badge.plus", APP_LAYER_NEW_GROUP)
				item("Filter", "camera.filters", APP_LAYER_NEW_FILTER)
			}
			Section("Mask") {
				item("Black Mask", "circle.fill", APP_LAYER_NEW_BLACK_MASK)
				item("White Mask", "circle", APP_LAYER_NEW_WHITE_MASK)
				item("Height Blend Mask", "mountain.2", APP_LAYER_NEW_HEIGHT_MASK)
				item("Fill Mask", "circle.lefthalf.filled", APP_LAYER_NEW_FILL_MASK)
				item("ID Mask…", "square.grid.3x3.square", APP_LAYER_NEW_ID_MASK)
			}
		} label: {
			Label("New Layer", systemImage: "plus")
		}
		.accessibilityLabel("New Layer")
	}

	private func item(_ title: String, _ symbol: String, _ kind: app_layer_new_t) -> some View {
		let k = Int32(kind.rawValue)
		return Button {
			store.perform { _ = app_layer_new(k) }
		} label: {
			Label(title, systemImage: symbol)
		}
		.disabled(!layers.canNew.contains(k))
	}
}

// A row's menu, the items and their enabled state as the engine's layer menu (tab_layers_draw_layer_context_menu_draw)
struct LayerContextMenu: View {
	let layer: LayerRow

	private var store  = AppStore.shared
	private var layers = LayersStore.shared
	@Environment(\.renameLayer) private var rename

	init(layer: LayerRow) {
		self.layer = layer
	}

	private var p: LayerProps {
		layer.props
	}

	private var isLayer: Bool {
		layer.kind == .layer
	}

	var body: some View {
		Section {
			Button {
				rename(layer)
			} label: {
				Label("Rename…", systemImage: "pencil")
			}
			Button {
				act { app_layer_duplicate(layer.id) }
			} label: {
				Label("Duplicate", systemImage: "plus.square.on.square")
			}
			Button {
				act { app_layer_set_visible(layer.id, !layer.visible) }
			} label: {
				Label(layer.visible ? "Hide" : "Show", systemImage: layer.visible ? "eye.slash" : "eye")
			}
		}

		Section {
			Button {
				act { app_layer_merge_down(layer.id) }
			} label: {
				Label("Merge Down", systemImage: "arrow.down.to.line")
			}
			.disabled(!p.canMergeDown)
			if p.showsMergeGroup {
				Button {
					act { app_layer_merge_group(layer.id) }
				} label: {
					Label("Merge Group", systemImage: "square.stack.3d.down.forward")
				}
				.disabled(!p.canMergeGroup)
			}
			if p.showsToFill {
				Button {
					act { app_layer_to_fill(layer.id) }
				} label: {
					Label(isLayer ? "To Fill Layer" : "To Fill Mask", systemImage: "drop.fill")
				}
			}
			if p.showsToPaint {
				Button {
					act { app_layer_to_paint(layer.id) }
				} label: {
					Label(layer.kind == .mask ? "To Paint Mask" : "To Paint Layer", systemImage: "paintbrush")
				}
			}
		}

		Section {
			if p.showsAddMask {
				Menu {
					maskItem("Black Mask", APP_LAYER_NEW_BLACK_MASK)
					maskItem("White Mask", APP_LAYER_NEW_WHITE_MASK)
					maskItem("Height Blend Mask", APP_LAYER_NEW_HEIGHT_MASK)
					maskItem("Fill Mask", APP_LAYER_NEW_FILL_MASK)
				} label: {
					Label("Add Mask", systemImage: "circle.lefthalf.filled")
				}
			}
			if p.showsInvert {
				Button {
					act { app_layer_invert_mask(layer.id) }
				} label: {
					Label("Invert", systemImage: "circle.righthalf.filled")
				}
			}
			if p.showsApply {
				Button {
					act { app_layer_apply_mask(layer.id) }
				} label: {
					Label("Apply", systemImage: "checkmark.circle")
				}
			}
			groupItems
		}

		Section {
			if p.showsSaveSmartMaterial {
				Button {
					store.perform { app_layer_save_smart_material(layer.id) }
				} label: {
					Label("Save as Smart Material…", systemImage: "square.and.arrow.down")
				}
			}
			Button {
				store.perform { app_layer_export(layer.id) }
			} label: {
				Label("Export…", systemImage: "square.and.arrow.up")
			}
		}

		Section {
			if p.showsClear {
				Button(role: .destructive) {
					act { app_layer_clear(layer.id) }
				} label: {
					Label("Clear", systemImage: "eraser")
				}
			}
			Button(role: .destructive) {
				act { app_layer_delete(layer.id) }
			} label: {
				Label("Delete", systemImage: "trash")
			}
			.disabled(!p.canDelete)
		}
	}

	// Into a group, or out of the one it is in
	@ViewBuilder
	private var groupItems: some View {
		let groups = layers.groups(for: layer)
		if !groups.isEmpty {
			Menu {
				ForEach(groups) { group in
					Button(group.name) {
						act { app_layer_set_group(layer.id, group.id) }
					}
				}
			} label: {
				Label("Move to Group", systemImage: "folder")
			}
		}
		if layer.kind != .mask && layer.kind != .filter, let parent = layers.layer(layer.parentID), parent.kind == .group {
			Button {
				act { app_layer_set_group(layer.id, -1) }
			} label: {
				Label("Remove from Group", systemImage: "folder.badge.minus")
			}
		}
	}

	private func maskItem(_ title: String, _ kind: app_layer_new_t) -> some View {
		Button(title) {
			act { app_layer_add_mask(layer.id, Int32(kind.rawValue)) }
		}
		.disabled(kind == APP_LAYER_NEW_FILL_MASK && !layers.canNew.contains(Int32(APP_LAYER_NEW_FILL_MASK.rawValue)))
	}

	private func act(_ action: @escaping () -> Bool) {
		store.perform { _ = action() }
	}
}

// Rename alert, for the row menu
extension View {
	func layerRename(_ layer: Binding<LayerRow?>) -> some View {
		modifier(LayerRenameAlert(layer: layer))
	}
}

private struct LayerRenameAlert: ViewModifier {
	@Binding var layer: LayerRow?
	@State private var name = ""

	func body(content: Content) -> some View {
		content
			.alert(layer?.props.isText == true ? "Edit Text" : "Rename Layer", isPresented: Binding(get: { layer != nil }, set: { if !$0 { layer = nil } })) {
				TextField("Name", text: $name)
				Button("Cancel", role: .cancel) {
					layer = nil
				}
				Button("Rename") {
					if let id = layer?.id {
						AppStore.shared.perform { _ = app_layer_set_name(id, name) }
					}
					layer = nil
				}
				.keyboardShortcut(.defaultAction)
			}
			.onChange(of: layer?.id) {
				name = layer?.name ?? ""
			}
	}
}
