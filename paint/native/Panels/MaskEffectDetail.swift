import SwiftUI

// A mask's effects in its settings, top first as they apply: a tap opens an effect's settings (pushed in the iPad
// popover, a popover of its own in the Mac inspector), a drag reorders them
struct MaskEffectsSection: View {
	let layer: LayerRow

	@Environment(\.openMaskEffect) private var openEffect
	@State private var shown: Int32? // Mac: the effect whose settings show in a popover

	init(layer: LayerRow) {
		self.layer = layer
	}

	var body: some View {
		Section {
			ForEach(Array(layer.effects.enumerated()), id: \.offset) { i, effect in
				row(Int32(i), effect)
			}
			.onMove(perform: move)
			addMenu
		} header: {
			Text("Effects")
		}
	}

	private func row(_ index: Int32, _ effect: MaskEffect) -> some View {
		HStack(spacing: 8) {
			Button {
				open(index)
			} label: {
				HStack(spacing: 10) {
					Image(systemName: maskEffectSymbol(effect.type))
						.foregroundStyle(.secondary)
						.frame(width: 24)
					VStack(alignment: .leading, spacing: 1) {
						Text(effect.name)
						Text(summary(effect))
							.font(.caption)
							.foregroundStyle(.secondary)
					}
					Spacer(minLength: 4)
				}
				.opacity(effect.visible ? 1 : 0.5)
				.contentShape(.rect)
			}
			.buttonStyle(.borderless)
			.tint(Color.primary)
			Button {
				AppStore.shared.perform { app_mask_set_effect_visible(layer.id, index, !effect.visible) }
			} label: {
				Image(systemName: effect.visible ? "eye" : "eye.slash")
					.frame(width: 28, height: 28)
					.contentShape(.rect)
			}
			.buttonStyle(.borderless)
			.tint(Color.primary)
			.accessibilityLabel(effect.visible ? "Hide" : "Show")
#if os(iOS)
			Image(systemName: "chevron.right")
				.font(.footnote.weight(.semibold))
				.foregroundStyle(.tertiary)
#endif
		}
		.swipeActions(edge: .trailing) {
			if !effect.isPaint {
				Button(role: .destructive) {
					AppStore.shared.perform { app_mask_remove_effect(layer.id, index) }
				} label: {
					Label("Delete", systemImage: "trash")
				}
			}
		}
		.contextMenu {
			Button(role: .destructive) {
				AppStore.shared.perform { app_mask_remove_effect(layer.id, index) }
			} label: {
				Label("Delete", systemImage: "trash")
			}
			.disabled(effect.isPaint)
		}
		.popover(isPresented: Binding(get: { shown == index }, set: { if !$0, shown == index { shown = nil } })) {
			MaskEffectDetailView(layerID: layer.id, index: index)
				.frame(width: 320, height: 440)
		}
	}

	// A row dropped before row `to`: its index once moved
	private func move(_ from: IndexSet, _ to: Int) {
		guard let source = from.first, from.count == 1, to != source, to != source + 1 else {
			return
		}
		let target = to > source ? to - 1 : to
		AppStore.shared.perform { app_mask_move_effect(layer.id, Int32(source), Int32(target)) }
	}

	private var addMenu: some View {
		Menu {
			MaskEffectAddItems { type in
				AppStore.shared.perform { _ = app_mask_add_effect(layer.id, type) }
			}
		} label: {
			Label("Add Effect", systemImage: "plus")
		}
	}

	private func open(_ index: Int32) {
		if let openEffect {
			openEffect(layer.id, index)
		}
		else {
			shown = index
		}
	}

	// Its source layer when it reads one, its blending, its opacity
	private func summary(_ e: MaskEffect) -> String {
		var parts: [String] = []
		if let source = e.settings.first(where: { $0.kind == APP_MASK_PARAM_LAYER && $0.shown(in: e) }),
		   let layer = LayersStore.shared.layer(Int32(e.params[Int(source.index)])) {
			parts.append(layer.name)
		}
		if !e.isFilter {
			let names = LayersStore.maskBlendingNames
			let b     = Int(e.blending)
			parts.append(names.indices.contains(b) ? names[b] : names[0])
		}
		parts.append(percent(e.opacity))
		return parts.joined(separator: " · ")
	}
}

func maskEffectSymbol(_ type: Int32) -> String {
	switch app_mask_effect_type_t(UInt32(max(type, 0))) {
	case APP_MASK_EFFECT_PAINT: "paintbrush"
	case APP_MASK_EFFECT_FILL: "square.fill"
	case APP_MASK_EFFECT_LEVELS: "slider.vertical.3"
	case APP_MASK_EFFECT_INVERT: "circle.righthalf.filled"
	case APP_MASK_EFFECT_EDGE_WEAR: "cube.transparent"
	case APP_MASK_EFFECT_DIRT: "aqi.medium"
	case APP_MASK_EFFECT_POSITION: "square.bottomhalf.filled"
	case APP_MASK_EFFECT_DIRECTIONAL: "sun.max"
	case APP_MASK_EFFECT_ID_SELECTION: "square.grid.3x3.square"
	case APP_MASK_EFFECT_NOISE: "cloud"
	case APP_MASK_EFFECT_LIGHT: "lightbulb"
	case APP_MASK_EFFECT_THICKNESS: "rectangle.compress.vertical"
	case APP_MASK_EFFECT_HISTOGRAM: "chart.bar"
	case APP_MASK_EFFECT_BLUR: "drop"
	case APP_MASK_EFFECT_ANCHOR: "link"
	case APP_MASK_EFFECT_MASK_BUILDER: "square.stack.3d.up"
	case APP_MASK_EFFECT_AO: "circle.dotted"
	case APP_MASK_EFFECT_HEIGHT_BLEND: "mountain.2"
	default: "wand.and.stars"
	}
}

// One effect's settings, drawn from its type's settings table (app_mask_effect_param)
struct MaskEffectDetailView: View {
	let layerID: Int32
	let index: Int32 // Top first, as listed

	private var layers = LayersStore.shared

	init(layerID: Int32, index: Int32) {
		self.layerID = layerID
		self.index = index
	}

	private var effect: MaskEffect? {
		guard let effects = layers.layer(layerID)?.effects, effects.indices.contains(Int(index)) else {
			return nil
		}
		return effects[Int(index)]
	}

	var body: some View {
		Form {
			if let effect {
				MaskEffectSections(layerID: layerID, index: index, effect: effect)
			}
		}
		.formStyle(.grouped)
		.navigationTitle(effect?.name ?? "Effect")
#if os(iOS)
		.navigationBarTitleDisplayMode(.inline)
#endif
	}
}

private struct MaskEffectSections: View {
	let layerID: Int32
	let index: Int32
	let effect: MaskEffect

	private var layers = LayersStore.shared

	init(layerID: Int32, index: Int32, effect: MaskEffect) {
		self.layerID = layerID
		self.index = index
		self.effect = effect
	}

	var body: some View {
		let settings = effect.settings.filter { $0.shown(in: effect) }
		let own      = settings.filter { !$0.isNoise }
		let noise    = settings.filter(\.isNoise)
		Section {
			slider("Opacity", field: -1, value: effect.opacity, range: 0...1)
			if !effect.isFilter {
				Picker("Blending", selection: Binding(get: { effect.blending }, set: { v in
					AppStore.shared.perform { _ = app_mask_set_effect_blending(layerID, index, v) }
				})) {
					ForEach(Array(LayersStore.maskBlendingNames.enumerated()), id: \.offset) { i, name in
						Text(name).tag(Int32(i))
					}
				}
			}
		}

		if effect.type == Int32(APP_MASK_EFFECT_ID_SELECTION.rawValue) {
			let parts = max(Int(effect.params[0]), 0)
			Section {
				LabeledContent("Parts", value: parts == 1 ? "1 part" : "\(parts) parts")
				Button("Pick Parts…", systemImage: "hand.tap") {
					AppStore.shared.perform { _ = app_mask_effect_pick_ids(layerID, index) }
				}
			} header: {
				Text("Settings")
			}
		}

		if !own.isEmpty {
			Section {
				ForEach(own) { setting($0) }
				if effect.needsAO || effect.needsThickness {
					Button("Bake Mesh Maps…", systemImage: "flame") {
						AppStore.shared.perform { app_command(Int32(APP_COMMAND_BAKE_MESH_MAPS.rawValue)) }
					}
				}
			} header: {
				Text("Settings")
			}
		}

		if !noise.isEmpty {
			Section("Noise") {
				ForEach(noise) { setting($0) }
			}
		}

		if !settings.isEmpty {
			Section {
				Button("Reset", systemImage: "arrow.counterclockwise") {
					AppStore.shared.perform { _ = app_mask_reset_effect(layerID, index) }
				}
			}
		}
	}

	@ViewBuilder
	private func setting(_ p: MaskParam) -> some View {
		let value = effect.params[Int(p.index)]
		switch p.kind {
		case APP_MASK_PARAM_CHOICE where p.options.count > 4: // Too many for a segmented control
			Picker(p.name, selection: Binding(get: { Int32(value.rounded()) }, set: { v in
				AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(v)) }
			})) {
				ForEach(Array(p.options.enumerated()), id: \.offset) { i, name in
					Text(name).tag(Int32(i))
				}
			}
		case APP_MASK_PARAM_CHOICE:
			VStack(alignment: .leading, spacing: 6) {
				Text(p.name)
				Picker(p.name, selection: Binding(get: { Int32(value.rounded()) }, set: { v in
					AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(v)) }
				})) {
					ForEach(Array(p.options.enumerated()), id: \.offset) { i, name in
						Text(name).tag(Int32(i))
					}
				}
				.pickerStyle(.segmented)
				.labelsHidden()
			}
		case APP_MASK_PARAM_LAYER:
			let anchors = anchorLayers
			LayerChoicePicker(title: p.name, none: p.options.first ?? "None", ids: anchors,
			                  selection: anchors.contains(Int32(value)) ? Int32(value) : -1) { v in
				AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(v)) }
			}
		case APP_MASK_PARAM_IMAGE:
			let images = layers.imageNames
			Picker(p.name, selection: Binding(get: { images.indices.contains(Int(value)) ? Int32(value) : -1 }, set: { v in
				AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(v)) }
			})) {
				Text("None").tag(Int32(-1))
				ForEach(Array(images.enumerated()), id: \.offset) { i, name in
					Text(name).tag(Int32(i))
				}
			}
			Button("Import Image…", systemImage: "photo.badge.plus") {
				AppStore.shared.perform { app_command(Int32(APP_COMMAND_IMPORT_TEXTURE.rawValue)) }
			}
		case APP_MASK_PARAM_SEED:
			HStack {
				Text(p.name)
				Spacer()
				Text("\(Int(value))")
					.foregroundStyle(.secondary)
					.monospacedDigit()
				Button("Shuffle", systemImage: "shuffle") {
					var seed = Int.random(in: 0...998) // Any other than the current one
					if seed >= Int(value) {
						seed += 1
					}
					AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(seed)) }
				}
				.buttonStyle(.bordered)
			}
		default:
			slider(p.name, field: p.index, value: value, range: p.range)
		}
	}

	// Layers an anchor can reference, top first
	private var anchorLayers: [Int32] {
		(0..<app_mask_anchor_layer_count(layerID)).map { app_mask_anchor_layer_id(layerID, $0) }
	}

	private func slider(_ title: String, field: Int32, value: Float, range: ClosedRange<Float>) -> some View {
		let unit = range.lowerBound >= 0 && range.upperBound <= 1 // 0..1 shows as a percentage
		let format: (Float) -> String = { unit ? percent($0) : String(format: "%.2f", $0) }
		return LayerSlider(title, value: value, range: range, format: format,
		                   begin: { app_mask_effect_value_begin(layerID, index, field) },
		                   set: { _ = app_mask_set_effect_value(layerID, index, field, $0) },
		                   end: { app_drag_end() })
	}
}

// The effect kinds a mask takes, for the Effects section and the layer menu
struct MaskEffectAddItems: View {
	let add: (Int32) -> Void

	var body: some View {
		Section("Sources") {
			ForEach(LayersStore.effectSources, id: \.self) { item($0) }
		}
		Section("Filters") {
			ForEach(LayersStore.effectFilters, id: \.self) { item($0) }
		}
		Section("Generators") {
			ForEach(LayersStore.effectGenerators, id: \.self) { item($0) }
		}
	}

	private func item(_ type: Int32) -> some View {
		Button {
			add(type)
		} label: {
			Label(LayersStore.effectTypeNames[Int(type)], systemImage: maskEffectSymbol(type))
		}
	}
}

// A layer setting: each layer as the layers panel shows it, its thumbnail and its mask's beside the name. iPad: a row
// that pushes the list, Mac: a menu.
struct LayerChoicePicker: View {
	let title: String
	let none: String // The -1 choice
	let ids: [Int32]
	let selection: Int32
	let set: (Int32) -> Void

	private var layers = LayersStore.shared

	init(title: String, none: String, ids: [Int32], selection: Int32, set: @escaping (Int32) -> Void) {
		self.title = title
		self.none = none
		self.ids = ids
		self.selection = selection
		self.set = set
	}

	var body: some View {
#if os(iOS)
		NavigationLink {
			LayerChoiceList(title: title, none: none, ids: ids, selection: selection, set: set)
		} label: {
			LabeledContent(title) {
				LayerChoiceLabel(id: selection, none: none, size: 28)
			}
		}
#else
		Picker(title, selection: Binding(get: { selection }, set: set)) {
			Text(none).tag(Int32(-1))
			ForEach(ids, id: \.self) { id in
				Label {
					Text(layers.layer(id)?.name ?? "")
				} icon: {
					if let image = layers.thumbnail(for: id) {
						Image(decorative: image, scale: CGFloat(image.width) / 16)
					}
				}
				.tag(id)
			}
		}
#endif
	}
}

// A layer's thumbnail, its mask's and its name; none for -1
struct LayerChoiceLabel: View {
	let id: Int32
	let none: String
	let size: CGFloat

	private var layers = LayersStore.shared

	init(id: Int32, none: String, size: CGFloat) {
		self.id = id
		self.none = none
		self.size = size
	}

	var body: some View {
		if let layer = layers.layer(id) {
			HStack(spacing: 6) {
				HStack(spacing: 3) {
					LayerThumbnail(id: layer.id, mask: layer.kind == .mask, size: size)
					if layer.props.maskID >= 0 {
						LayerThumbnail(id: layer.props.maskID, mask: true, size: size)
					}
				}
				Text(layer.name)
					.lineLimit(1)
					.truncationMode(.middle)
			}
		}
		else {
			Text(none)
		}
	}
}

#if os(iOS)
private struct LayerChoiceList: View {
	let title: String
	let none: String
	let ids: [Int32]
	let selection: Int32
	let set: (Int32) -> Void

	@Environment(\.dismiss) private var dismiss

	var body: some View {
		List {
			row(-1)
			ForEach(ids, id: \.self) { row($0) }
		}
		.navigationTitle(title)
		.navigationBarTitleDisplayMode(.inline)
	}

	private func row(_ id: Int32) -> some View {
		Button {
			if id != selection {
				set(id)
			}
			dismiss()
		} label: {
			HStack {
				LayerChoiceLabel(id: id, none: none, size: 36)
				Spacer(minLength: 4)
				if id == selection {
					Image(systemName: "checkmark")
						.foregroundStyle(Color.accentColor)
				}
			}
			.contentShape(.rect)
		}
		.tint(Color.primary)
	}
}
#endif
