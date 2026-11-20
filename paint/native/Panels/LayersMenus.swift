import SwiftUI

// The "+" menu: the layer kinds
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

// A row's menu, the items and their enabled state as app_layer_props reports them
struct LayerContextMenu: View {
	let layer: LayerRow

	private var store  = AppStore.shared
	private var layers = LayersStore.shared

	init(layer: LayerRow) {
		self.layer = layer
	}

	private var p: LayerProps {
		layer.props
	}

	var body: some View {
		Section {
			Button {
				renameLayer(layer)
			} label: {
				Label("Rename…", systemImage: "pencil")
			}
			Button {
				store.perform { app_layer_duplicate(layer.id) }
			} label: {
				Label("Duplicate", systemImage: "plus.square.on.square")
			}
			Button {
				store.perform { app_layer_set_visible(layer.id, !layer.visible) }
			} label: {
				Label(layer.visible ? "Hide" : "Show", systemImage: layer.visible ? "eye.slash" : "eye")
			}
		}

		Section {
			Button {
				store.perform { app_layer_merge_down(layer.id) }
			} label: {
				Label("Merge Down", systemImage: "arrow.down.to.line")
			}
			.disabled(!p.canMergeDown)
			if p.showsMergeGroup {
				Button {
					store.perform { app_layer_merge_group(layer.id) }
				} label: {
					Label("Merge Group", systemImage: "square.stack.3d.down.forward")
				}
				.disabled(!p.canMergeGroup)
			}
			if p.showsToFill {
				Button {
					store.perform { app_layer_to_fill(layer.id) }
				} label: {
					Label("To Fill Layer", systemImage: "drop.fill")
				}
			}
			if p.showsToPaint {
				Button {
					store.perform { app_layer_to_paint(layer.id) }
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
				} label: {
					Label("Add Mask", systemImage: "circle.lefthalf.filled")
				}
			}
			if p.showsAddEffect {
				Menu {
					MaskEffectAddItems { type in
						store.perform { app_layer_add_effect(layer.id, type) >= 0 }
					}
				} label: {
					Label("Add Effect", systemImage: "wand.and.stars")
				}
			}
			if p.showsInvert {
				Button {
					store.perform { app_layer_invert_mask(layer.id) }
				} label: {
					Label("Invert", systemImage: "circle.righthalf.filled")
				}
			}
			if p.showsApply {
				Button {
					store.perform { app_layer_apply_mask(layer.id) }
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
			if p.showsFillPaint {
				Button {
					store.perform { app_layer_fill_paint(layer.id) }
				} label: {
					Label("Fill Paint", systemImage: "circle.fill")
				}
			}
			if p.showsClear {
				Button(role: .destructive) {
					store.perform { app_layer_clear(layer.id) }
				} label: {
					Label(layer.kind == .mask ? "Clear Paint" : "Clear", systemImage: "eraser")
				}
			}
			Button(role: .destructive) {
				store.perform { app_layer_delete(layer.id) }
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
						store.perform { app_layer_set_group(layer.id, group.id) }
					}
				}
			} label: {
				Label("Move to Group", systemImage: "folder")
			}
		}
		if layer.kind != .mask && layer.kind != .filter, let parent = layers.layer(layer.parentID), parent.kind == .group {
			Button {
				store.perform { app_layer_set_group(layer.id, -1) }
			} label: {
				Label("Remove from Group", systemImage: "folder.badge.minus")
			}
		}
	}

	private func maskItem(_ title: String, _ kind: app_layer_new_t) -> some View {
		Button(title) {
			store.perform { app_layer_add_mask(layer.id, Int32(kind.rawValue)) }
		}
	}
}

// Rename (or edit the text of) a layer, for the row menu and the settings
@MainActor func renameLayer(_ layer: LayerRow) {
	let text = layer.props.isText
	RootAlert.shared.text(text ? "Edit Text" : "Rename Layer", action: text ? "Done" : "Rename", initial: layer.name,
	                      placeholder: text ? "Text" : "Name") { name in
		AppStore.shared.perform { _ = app_layer_set_name(layer.id, name) }
	}
}
