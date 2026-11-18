import SwiftUI

// A layer's settings: pushed in the iPad layers popover
struct LayerDetailView: View {
	let layerID: Int32

	private var layers = LayersStore.shared

	init(layerID: Int32) {
		self.layerID = layerID
	}

	var body: some View {
		Form {
			LayerDetailSections(layerID: layerID, showsName: true)
		}
		.formStyle(.grouped)
		.navigationTitle(layers.layer(layerID)?.name ?? "Layer")
#if os(iOS)
		.navigationBarTitleDisplayMode(.inline)
#endif
	}
}

// The settings as Form sections, in the popover and in the Mac inspector's Layer section
struct LayerDetailSections: View {
	let layerID: Int32
	var showsName = true

	private var store  = AppStore.shared
	private var layers = LayersStore.shared

	init(layerID: Int32, showsName: Bool = true) {
		self.layerID = layerID
		self.showsName = showsName
	}

	var body: some View {
		if let layer = layers.layer(layerID) {
			content(layer)
		}
	}

	@ViewBuilder
	private func content(_ layer: LayerRow) -> some View {
		let p = layer.props
		Section("Layer") {
			if showsName {
				LayerNameField(layer: layer)
			}
			LayerSlider("Opacity", value: layer.opacity, range: 0...1, format: { "\(Int(($0 * 100).rounded()))%" },
			            begin: { app_layer_opacity_begin(layer.id) },
			            set: { _ = app_layer_set_opacity(layer.id, $0) },
			            end: { app_drag_end() })
			if p.showsRaise {
				LayerSlider("Raise", value: p.raise, range: -1...1, format: { "\(Int(($0 * 100).rounded()))%" },
				            begin: { app_layer_raise_begin(layer.id) },
				            set: { _ = app_layer_set_raise(layer.id, abs($0) < 0.02 ? 0 : $0) }, // Snaps to flat
				            end: { app_drag_end() })
			}
			if p.showsBlending {
				Picker("Blending", selection: Binding(get: { p.blending }, set: { v in
					store.perform { _ = app_layer_set_blending(layer.id, v) }
				})) {
					ForEach(Array(LayersStore.blendingNames.enumerated()), id: \.offset) { i, name in
						Text(name).tag(Int32(i))
					}
				}
			}
			if p.showsChannels && p.hasUV1 {
				Picker("UV Map", selection: Binding(get: { p.uvMap }, set: { v in
					store.perform { _ = app_layer_set_uv_map(layer.id, v) }
				})) {
					Text("uv0").tag(Int32(0))
					Text("uv1").tag(Int32(1))
				}
			}
		}

		if layer.isFill && !p.sources.isEmpty {
			FillSections(layer: layer) // FillDetail.swift
		}

		if layer.kind == .mask {
			MaskEffectsSection(layer: layer) // MaskEffectDetail.swift
		}

		if p.showsChannels && !layer.isFill && layer.kind != .mask { // A fill's channels show with their sources, a mask has none
			Section("Channels") {
				LayerChannels(layer: layer)
			}
		}
	}
}

// One undo step per drag: begin when the drag starts, set while it moves, end when it ends
struct LayerSlider: View {
	let title: String
	let value: Float
	let range: ClosedRange<Float>
	let format: (Float) -> String
	let begin: () -> Void
	let set: (Float) -> Void
	let end: () -> Void

	init(_ title: String, value: Float, range: ClosedRange<Float>, format: @escaping (Float) -> String,
	     begin: @escaping () -> Void, set: @escaping (Float) -> Void, end: @escaping () -> Void) {
		self.title = title
		self.value = value
		self.range = range
		self.format = format
		self.begin = begin
		self.set = set
		self.end = end
	}

	var body: some View {
		VStack(alignment: .leading, spacing: 2) {
			HStack {
				Text(title)
				Spacer()
				Text(format(value))
					.foregroundStyle(.secondary)
					.monospacedDigit()
			}
			TipSlider(title: title, value: value, range: range, format: format, editingChanged: { editing in
				editing ? begin() : end()
			}, set: set)
		}
	}
}

// Name, committed on return or when the field loses focus. A text layer's name is its painted text.
private struct LayerNameField: View {
	let layer: LayerRow

	@State private var name = ""
	@FocusState private var focused: Bool

	var body: some View {
		TextField(layer.props.isText ? "Text" : "Name", text: $name)
			.focused($focused)
			.onSubmit(commit)
			.onChange(of: focused) { _, now in
				if !now {
					commit()
				}
			}
			.onAppear {
				name = layer.name
			}
			.onChange(of: layer.name) { _, new in
				if !focused {
					name = new
				}
			}
			.onChange(of: layer.id) {
				name = layer.name
			}
#if os(iOS)
			.submitLabel(.done)
#endif
	}

	private func commit() {
		let trimmed = name.trimmingCharacters(in: .whitespaces)
		if trimmed.isEmpty || trimmed == layer.name {
			name = layer.name
			return
		}
		AppStore.shared.perform { _ = app_layer_set_name(layer.id, trimmed) }
		name = LayersStore.shared.layer(layer.id)?.name ?? trimmed // Made unique by the engine
	}
}

// The channels a layer paints, two to a row
private struct LayerChannels: View {
	let layer: LayerRow

	private static let channels: [(String, app_layer_channel_t)] = [
		("Base Color", APP_LAYER_CHANNEL_BASE),
		("Opacity", APP_LAYER_CHANNEL_OPACITY),
		("Normal", APP_LAYER_CHANNEL_NORMAL),
		("Normal Blend", APP_LAYER_CHANNEL_NORMAL_BLEND),
		("Roughness", APP_LAYER_CHANNEL_ROUGHNESS),
		("Metallic", APP_LAYER_CHANNEL_METALLIC),
		("Height", APP_LAYER_CHANNEL_HEIGHT),
		("Height Blend", APP_LAYER_CHANNEL_HEIGHT_BLEND),
		("Emission", APP_LAYER_CHANNEL_EMISSION),
		("Subsurface", APP_LAYER_CHANNEL_SUBSURFACE),
		("Occlusion", APP_LAYER_CHANNEL_OCCLUSION),
	]

	var body: some View {
		LazyVGrid(columns: [GridItem(.flexible(), spacing: 6), GridItem(.flexible(), spacing: 6)], spacing: 6) {
			ForEach(Self.channels, id: \.0) { title, channel in
				Toggle(isOn: Binding(get: { layer.props.paints(channel) }, set: { on in
					AppStore.shared.perform { _ = app_layer_set_channel(layer.id, Int32(channel.rawValue), on) }
				})) {
					Text(title)
						.font(.callout)
						.lineLimit(1)
						.minimumScaleFactor(0.8)
						.frame(maxWidth: .infinity)
				}
				.toggleStyle(.button)
				.buttonStyle(.bordered)
			}
		}
		.padding(.vertical, 2)
	}
}
