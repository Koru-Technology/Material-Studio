import SwiftUI

#if os(iOS)
// While the Materials panel is open: the selected material as a new fill layer on top of the stack
struct ApplyMaterialBar: View {
	private var materials = MaterialsStore.shared

	var body: some View {
		Button {
			if let id = materials.selectedMaterial?.id {
				AppStore.shared.perform { _ = app_material_new_top_layer(id) }
			}
		} label: {
			Label("Apply Material as New Layer", systemImage: "square.stack.3d.up.fill")
				.font(.body.weight(.medium))
				.padding(.horizontal, 18)
				.frame(height: 44)
		}
		.glassEffect(.regular.interactive(), in: .capsule)
		.foregroundStyle(.primary)
		.disabled(materials.selectedMaterial == nil)
	}
}

// A fill layer selected: the effects of its mask in the order they apply, left to right. A tap opens an effect's settings,
// a + between them (and at the ends) adds one there and opens its settings.
struct FillEffectsBar: View {
	let layer: LayerRow

	private var layers   = LayersStore.shared
	private var popovers = PadPopovers.shared

	init(layer: LayerRow) {
		self.layer = layer
	}

	var body: some View {
		let mask    = layers.layer(layer.props.maskID)
		let effects = mask?.effects ?? [] // Top first
		GlassEffectContainer(spacing: 6) {
			HStack(spacing: 6) {
				Text("Mask")
					.font(.subheadline.weight(.semibold))
					.foregroundStyle(.secondary)
					.padding(.leading, 6)
				if effects.last?.isPaint != true { // Nothing goes below the paint, always applied first
					addButton(mask: mask, at: effects.count)
				}
				ForEach(Array(effects.indices.reversed()), id: \.self) { i in
					if let mask {
						effectButton(mask: mask, index: i, effect: effects[i])
					}
					addButton(mask: mask, at: i) // Applied right after effect i: above it
				}
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 6)
		}
		.glassEffect(.regular, in: .capsule)
		.foregroundStyle(.primary)
	}

	private func effectButton(mask: LayerRow, index: Int, effect: MaskEffect) -> some View {
		let name = "Effect.\(mask.id).\(index)"
		return Button {
			popovers.toggle(name)
		} label: {
			Image(systemName: maskEffectSymbol(effect.type))
				.font(.title3)
				.frame(width: 44, height: 44)
				.opacity(effect.visible ? 1 : 0.4)
		}
		.background(popovers.shown == name ? Color.accentColor.opacity(0.35) : Color.white.opacity(0.08),
		            in: .rect(cornerRadius: 10, style: .continuous))
		.accessibilityLabel(effect.name)
		.padPopover(name) {
			NavigationStack {
				MaskEffectDetailView(layerID: mask.id, index: Int32(index))
			}
			.frame(width: 340, height: 480)
		}
	}

	private func addButton(mask: LayerRow?, at index: Int) -> some View {
		let name = "EffectAdd.\(layer.id).\(index)"
		return Button {
			popovers.toggle(name)
		} label: {
			Image(systemName: "plus")
				.font(.caption.weight(.bold))
				.frame(width: 22, height: 22)
				.background(popovers.shown == name ? Color.accentColor : Color.white.opacity(0.15), in: .circle)
				.frame(width: 26, height: 44)
				.contentShape(.rect)
		}
		.accessibilityLabel("Add Effect")
		.padPopover(name) {
			NavigationStack {
				List {
					MaskEffectAddItems { type in
						add(type, mask: mask, at: index)
					}
				}
				.listStyle(.insetGrouped)
				.contentMargins(.top, 8, for: .scrollContent)
				.navigationTitle("New Mask Effect")
				.navigationBarTitleDisplayMode(.inline)
			}
			.frame(width: 280, height: 480)
		}
	}

	// Into the mask at index, then its settings open
	private func add(_ type: Int32, mask: LayerRow?, at index: Int) {
		var maskID = mask?.id ?? -1
		var at     = Int32(-1)
		AppStore.shared.perform {
			if maskID >= 0 {
				at = app_mask_insert_effect(maskID, type, Int32(index))
			}
			else { // An older fill layer without a mask: one is made
				maskID = app_layer_add_effect(layer.id, type)
				at     = maskID >= 0 ? 0 : -1
			}
		}
		popovers.close()
		if at >= 0 {
			let name = "Effect.\(maskID).\(at)"
			DispatchQueue.main.async { // Once the new effect's button is there
				PadPopovers.shared.toggle(name)
			}
		}
	}
}
#endif
