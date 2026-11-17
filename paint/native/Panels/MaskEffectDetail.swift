import SwiftUI

// A mask's effects in its settings, top first as they apply: a tap opens an effect's settings (pushed in the iPad
// popover, a popover of its own in the Mac inspector)
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
			addMenu
		} header: {
			Text("Effects")
		} footer: {
			Text("Brush strokes paint the Paint effect.")
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
			Button {
				AppStore.shared.perform { app_mask_move_effect(layer.id, index, index - 1) }
			} label: {
				Label("Move Up", systemImage: "arrow.up")
			}
			.disabled(index == 0)
			Button {
				AppStore.shared.perform { app_mask_move_effect(layer.id, index, index + 1) }
			} label: {
				Label("Move Down", systemImage: "arrow.down")
			}
			.disabled(Int(index) == layer.effects.count - 1)
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

	private func summary(_ e: MaskEffect) -> String {
		if e.isFilter {
			return percent(e.opacity)
		}
		let names = LayersStore.maskBlendingNames
		let b     = Int(e.blending)
		return "\(names.indices.contains(b) ? names[b] : names[0]) · \(percent(e.opacity))"
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
		} footer: {
			if effect.isPaint {
				Text("Brush strokes paint this effect.")
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
			} footer: {
				if effect.needsIDMap {
					Text("This project has no ID map yet: picking asks to import one.")
				}
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
			} footer: {
				if effect.needsAO {
					Text(effect.type == Int32(APP_MASK_EFFECT_DIRT.rawValue) ? "No AO map: cavity only" : "No AO map")
				}
				else if effect.needsThickness {
					Text("No thickness map")
				}
				else if effect.type == Int32(APP_MASK_EFFECT_ANCHOR.rawValue) {
					Text("Uses another layer's mask, height, opacity or occlusion")
				}
				else if effect.type == Int32(APP_MASK_EFFECT_MASK_BUILDER.rawValue) {
					Text("Micro details from a layer follow its normal and height: the cracks and ridges of its material")
				}
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
			Picker(p.name, selection: Binding(get: { anchors.contains { $0.id == Int32(value) } ? Int32(value) : -1 }, set: { v in
				AppStore.shared.perform { _ = app_mask_set_effect_value(layerID, index, p.index, Float(v)) }
			})) {
				Text(p.options.first ?? "None").tag(Int32(-1)) // Micro Details From: "Normal Map"
				ForEach(anchors, id: \.id) { layer in
					Text(layer.name).tag(layer.id)
				}
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
	private var anchorLayers: [(id: Int32, name: String)] {
		(0..<app_mask_anchor_layer_count(layerID)).map { (app_mask_anchor_layer_id(layerID, $0), String(cString: app_mask_anchor_layer_name(layerID, $0))) }
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
