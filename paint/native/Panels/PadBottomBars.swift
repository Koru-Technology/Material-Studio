import SwiftUI
import UniformTypeIdentifiers

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

// A fill layer selected: the effects of its mask in the order they apply, left to right, the paint first. A tap opens an
// effect's settings, a long press drags it to another place (one undo step when let go). The + adds one on top.
struct FillEffectsBar: View {
	let layer: LayerRow

	private var layers   = LayersStore.shared
	private var popovers = PadPopovers.shared
	private var reorder  = EffectReorder.shared

	init(layer: LayerRow) {
		self.layer = layer
	}

	var body: some View {
		let mask    = layers.layer(layer.props.maskID)
		let effects = mask?.effects ?? [] // Top first
		let shown   = reorder.order(for: mask?.id ?? -1) ?? Array(effects.indices.reversed()) // Left to right
		GlassEffectContainer(spacing: 6) {
			HStack(spacing: 6) {
				Text("Mask")
					.font(.subheadline.weight(.semibold))
					.foregroundStyle(.secondary)
					.padding(.leading, 6)
				if let mask {
					ForEach(shown, id: \.self) { i in
						if effects.indices.contains(i) {
							let effect = effects[i]
							effectButton(mask: mask, index: i, effect: effect)
								.onDrag {
									reorder.begin(mask: mask.id, index: effect.isPaint ? nil : i, order: Array(effects.indices.reversed())) // The paint stays first
									return NSItemProvider(object: "effect" as NSString)
								} preview: {
									Image(systemName: maskEffectSymbol(effect.type))
										.font(.title3)
										.frame(width: 44, height: 44)
										.background(.regularMaterial, in: .rect(cornerRadius: 10, style: .continuous))
								}
								.onDrop(of: [.text], delegate: EffectDropDelegate(index: i, isPaint: effect.isPaint))
						}
					}
				}
				addButton(mask: mask, at: 0) // On top: applied last
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 6)
			.animation(.snappy(duration: 0.2), value: shown)
		}
		.glassEffect(.regular, in: .capsule)
		.foregroundStyle(.primary)
		.onChange(of: effects) {
			reorder.reset() // Changed under the drag (an undo)
		}
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
#endif

// An effect of the bar dragged to another place: the bar shows the order it would take, let go the mask takes it
@MainActor
@Observable
final class EffectReorder {
	static let shared = EffectReorder()

	private var mask: Int32 = -1
	private var dragged: Int?       // Its index in the stack, top first
	private var shown: [Int]?        // Stack indices left to right as the bar shows them
	@ObservationIgnored private var leave: Task<Void, Never>?

	func order(for mask: Int32) -> [Int]? {
		mask == self.mask ? shown : nil
	}

	func begin(mask: Int32, index: Int?, order: [Int]) {
		leave?.cancel()
		self.mask = mask
		dragged   = index
		shown     = index != nil ? order : nil
	}

	// Over effect `index`: the dragged one takes its place. Never the paint's, the first
	func hover(_ index: Int, isPaint: Bool) {
		leave?.cancel()
		guard let dragged, var order = shown, index != dragged, !isPaint, let from = order.firstIndex(of: dragged),
		      let to = order.firstIndex(of: index) else {
			return
		}
		order.remove(at: from)
		order.insert(dragged, at: to)
		shown = order
	}

	// Off the effects: a moment later (another one may take the finger) the order goes back, unless let go
	func exited() {
		leave?.cancel()
		leave = Task { @MainActor [weak self] in
			try? await Task.sleep(for: .milliseconds(400))
			if let self, !Task.isCancelled {
				self.reset()
			}
		}
	}

	func drop() -> Bool {
		defer {
			reset()
		}
		guard let dragged, let order = shown, let pos = order.firstIndex(of: dragged) else {
			return false
		}
		let to   = order.count - 1 - pos // Left to right is bottom to top
		let mask = self.mask
		if to != dragged {
			AppStore.shared.perform { _ = app_mask_move_effect(mask, Int32(dragged), Int32(to)) }
		}
		return true
	}

	func reset() {
		leave?.cancel()
		dragged = nil
		shown   = nil
		mask    = -1
	}
}

struct EffectDropDelegate: DropDelegate {
	let index: Int
	let isPaint: Bool

	func dropEntered(info: DropInfo) {
		EffectReorder.shared.hover(index, isPaint: isPaint)
	}

	func dropUpdated(info: DropInfo) -> DropProposal? {
		DropProposal(operation: .move)
	}

	func dropExited(info: DropInfo) {
		EffectReorder.shared.exited()
	}

	func performDrop(info: DropInfo) -> Bool {
		EffectReorder.shared.drop()
	}
}
