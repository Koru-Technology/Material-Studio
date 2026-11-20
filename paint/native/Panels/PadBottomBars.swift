import SwiftUI

#if os(iOS)
// Under the Materials panel: the selected material as a new fill layer on top of the stack
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

// A fill layer selected, on the right: the effects of its mask in the order they apply, read top to bottom: the paint
// first, the top of the stack last. A tap opens an effect's settings to the left, a long press drags it up or down (the
// others make room, one undo step when let go). The + at the bottom adds one on top of the stack.
struct FillEffectsColumn: View {
	let layer: LayerRow

	private var layers   = LayersStore.shared
	private var popovers = PadPopovers.shared
	@State private var dragging: Int?     // The effect held, its index in the stack (0 the top)
	@State private var dragY: CGFloat = 0 // How far it moved

	private static let slot: CGFloat = 50 // An effect's height and the space after it

	init(layer: LayerRow) {
		self.layer = layer
	}

	var body: some View {
		let mask    = layers.layer(layer.props.maskID)
		let effects = mask?.effects ?? []                // Top of the stack first
		let order   = Array(effects.indices.reversed()) // Top to bottom on screen: stack indices, the paint first
		GlassEffectContainer(spacing: 6) {
			VStack(spacing: 6) {
				Text("Mask")
					.font(.caption.weight(.semibold))
					.foregroundStyle(.secondary)
					.padding(.top, 4)
				if let mask {
					ForEach(Array(order.enumerated()), id: \.element) { pos, i in
						let effect = effects[i]
						effectButton(mask: mask, index: i, effect: effect)
							.scaleEffect(dragging == i ? 1.12 : 1)
							.shadow(color: .black.opacity(dragging == i ? 0.35 : 0), radius: 8, y: 4)
							.offset(y: dragging == i ? dragY : shift(pos, order: order, effects: effects))
							.zIndex(dragging == i ? 1 : 0)
							.gesture(reorderGesture(mask: mask, index: i, order: order, effects: effects), including: effect.isPaint ? .subviews : .all)
					}
				}
				addButton(mask: mask, at: 0) // Last: on top of the stack, applied last
			}
			.padding(.horizontal, 6)
			.padding(.vertical, 8)
			.animation(.snappy(duration: 0.2), value: target(order: order, effects: effects))
			.animation(.snappy(duration: 0.15), value: dragging)
		}
		.glassEffect(.regular, in: .capsule)
		.foregroundStyle(.primary)
	}

	// Held, then dragged: the effect follows the finger, the others make room, let go it takes that place
	private func reorderGesture(mask: LayerRow, index: Int, order: [Int], effects: [MaskEffect]) -> some Gesture {
		LongPressGesture(minimumDuration: 0.25)
			.sequenced(before: DragGesture(minimumDistance: 0, coordinateSpace: .global))
			.onChanged { value in
				guard case .second(true, let drag) = value else {
					return
				}
				if dragging != index {
					popovers.close()
					dragging = index
				}
				dragY = drag?.translation.height ?? 0
			}
			.onEnded { _ in
				let to = target(order: order, effects: effects)
				dragging = nil
				dragY    = 0
				if let to, order[to] != index {
					let from  = Int32(index)
					let stack = Int32(order[to]) // The stack index of the effect whose place it takes
					AppStore.shared.perform { _ = app_mask_move_effect(mask.id, from, stack) }
				}
			}
	}

	// The place on screen the dragged effect would take; never above the paint, which is first
	private func target(order: [Int], effects: [MaskEffect]) -> Int? {
		guard let dragging, let from = order.firstIndex(of: dragging) else {
			return nil
		}
		let first = order.first.map { effects[$0].isPaint } == true ? 1 : 0
		return min(max(from + Int((dragY / Self.slot).rounded()), first), order.count - 1)
	}

	// The others slide over to open the gap
	private func shift(_ pos: Int, order: [Int], effects: [MaskEffect]) -> CGFloat {
		guard let dragging, let from = order.firstIndex(of: dragging), let to = target(order: order, effects: effects) else {
			return 0
		}
		if from < to && pos > from && pos <= to {
			return -Self.slot
		}
		if to < from && pos >= to && pos < from {
			return Self.slot
		}
		return 0
	}

	private func effectButton(mask: LayerRow, index: Int, effect: MaskEffect) -> some View {
		let name = "Effect.\(mask.id).\(index)"
		return Image(systemName: maskEffectSymbol(effect.type))
			.font(.title3)
			.frame(width: 44, height: 44)
			.opacity(effect.visible ? 1 : 0.4)
			.background(popovers.shown == name ? Color.accentColor.opacity(0.35) : Color.white.opacity(dragging == index ? 0.2 : 0.08),
			            in: .rect(cornerRadius: 10, style: .continuous))
			.overlay(alignment: .topLeading) {
				if !effect.visible { // Hidden
					Image(systemName: "eye.slash.fill")
						.font(.system(size: 9, weight: .bold))
						.foregroundStyle(.white)
						.padding(3)
						.background(.black.opacity(0.6), in: .circle)
						.offset(x: -4, y: -4)
						.allowsHitTesting(false)
				}
			}
			.contentShape(.rect)
			.onTapGesture {
				popovers.toggle(name)
			}
			.accessibilityLabel(effect.name)
			.accessibilityAddTraits(.isButton)
			.padPopover(name, arrowEdge: .trailing) { // To the left of the column
				EffectPopover(maskID: mask.id, index: Int32(index))
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
				.frame(width: 44, height: 30)
				.contentShape(.rect)
		}
		.accessibilityLabel("Add Effect")
		.padPopover(name, arrowEdge: .trailing) {
			VStack(spacing: 0) { // A title without a navigation bar's height: the effects right under it
				Text("New Mask Effect")
					.font(.headline)
					.padding(.top, 16)
					.padding(.bottom, 2)
				List {
					MaskEffectAddItems { type in
						add(type, mask: mask, at: index)
					}
				}
				.listStyle(.insetGrouped)
				.listSectionSpacing(.compact)
				.contentMargins(.top, 0, for: .scrollContent)
				.environment(\.defaultMinListHeaderHeight, 0)
				.scrollContentBackground(.hidden)
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

// An effect's settings from the column, Hide or Show and Delete under them (the paint is never deleted)
private struct EffectPopover: View {
	let maskID: Int32
	let index: Int32

	private var layers = LayersStore.shared

	init(maskID: Int32, index: Int32) {
		self.maskID = maskID
		self.index  = index
	}

	private var effect: MaskEffect? {
		guard let effects = layers.layer(maskID)?.effects, effects.indices.contains(Int(index)) else {
			return nil
		}
		return effects[Int(index)]
	}

	var body: some View {
		NavigationStack {
			MaskEffectDetailView(layerID: maskID, index: index)
				.safeAreaInset(edge: .bottom) {
					if let effect {
						HStack(spacing: 10) {
							Button {
								AppStore.shared.perform { _ = app_mask_set_effect_visible(maskID, index, !effect.visible) }
							} label: {
								Label(effect.visible ? "Hide" : "Show", systemImage: effect.visible ? "eye.slash" : "eye")
									.frame(maxWidth: .infinity)
							}
							if !effect.isPaint {
								Button(role: .destructive) {
									PadPopovers.shared.close()
									AppStore.shared.perform { _ = app_mask_remove_effect(maskID, index) }
								} label: {
									Label("Delete", systemImage: "trash")
										.frame(maxWidth: .infinity)
								}
							}
						}
						.buttonStyle(.bordered)
						.controlSize(.large)
						.padding(.horizontal, 16)
						.padding(.vertical, 12)
						.background(Color(uiColor: PadPanelContent.background)) // The settings scroll under it, not through it
					}
				}
		}
	}
}
#endif
