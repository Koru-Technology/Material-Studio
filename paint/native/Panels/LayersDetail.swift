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
			            end: { app_edit_end() })
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

		if layer.isFill && p.fillMaterial >= 0 {
			fillSection(layer)
		}

		if layer.kind == .mask {
			heightBlendSection(layer)
		}

		if p.showsChannels {
			Section("Channels") {
				LayerChannels(layer: layer)
			}
		}
	}

	@ViewBuilder
	private func fillSection(_ layer: LayerRow) -> some View {
		let p = layer.props
		Section(layer.kind == .mask ? "Fill Mask" : "Fill") {
			Picker("Material", selection: Binding(get: { p.fillMaterial }, set: { v in
				store.perform { _ = app_layer_set_fill_material(layer.id, v) }
			})) {
				ForEach(Array(layers.materialNames.enumerated()), id: \.offset) { i, name in
					Text(name).tag(Int32(i))
				}
			}
			Picker("Projection", selection: Binding(get: { p.uvType }, set: { v in
				store.perform { _ = app_layer_set_uv_type(layer.id, v) }
			})) {
				Text("UV Map").tag(Int32(0))
				Text("Triplanar").tag(Int32(1))
				Text("Project").tag(Int32(2))
			}
			valueSlider(layer, "UV Scale", p.scale, 0...5, APP_LAYER_VALUE_SCALE) { String(format: "%.2f", $0) }
			valueSlider(layer, "Angle", p.angle, 0...360, APP_LAYER_VALUE_ANGLE) { "\(Int($0.rounded()))°" }
		}
	}

	@ViewBuilder
	private func heightBlendSection(_ layer: LayerRow) -> some View {
		let p = layer.props
		Section {
			Toggle("Height Blend", isOn: Binding(get: { p.heightBlend }, set: { on in
				store.perform { _ = app_layer_set_height_blend(layer.id, on) }
			}))
			if p.heightBlend {
				valueSlider(layer, "Reach", p.heightBlendReach, 0...0.1, APP_LAYER_VALUE_HEIGHT_BLEND_REACH) { String(format: "%.3f", $0) }
				valueSlider(layer, "Influence", p.heightBlendInfluence, 0...4, APP_LAYER_VALUE_HEIGHT_BLEND_INFLUENCE) { String(format: "%.2f", $0) }
				valueSlider(layer, "Sharpness", p.heightBlendSharpness, 1...50, APP_LAYER_VALUE_HEIGHT_BLEND_SHARPNESS) { String(format: "%.1f", $0) }
				valueSlider(layer, "Offset", p.heightBlendOffset, -1...1, APP_LAYER_VALUE_HEIGHT_BLEND_OFFSET) { String(format: "%.2f", $0) }
				Toggle("Compare With Layers Below", isOn: Binding(get: { p.heightBlendBelow }, set: { on in
					store.perform { _ = app_layer_set_height_blend_below(layer.id, on) }
				}))
			}
		} header: {
			Text("Height Blend")
		} footer: {
			Text("The mask edge follows the height of the layer, tall details like fur strands reach past it.")
		}
	}

	private func valueSlider(_ layer: LayerRow, _ title: String, _ value: Float, _ range: ClosedRange<Float>, _ kind: app_layer_value_t,
	                         format: @escaping (Float) -> String) -> some View {
		let k = Int32(kind.rawValue)
		return LayerSlider(title, value: value, range: range, format: format,
		                   begin: { app_layer_value_begin(layer.id, k) },
		                   set: { _ = app_layer_set_value(layer.id, k, $0) },
		                   end: { app_layer_value_end() })
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
			Slider(value: Binding(get: { value }, set: { v in AppStore.shared.perform { set(v) } }), in: range) {
				Text(title)
			} onEditingChanged: { editing in
				editing ? begin() : end()
			}
			.labelsHidden()
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
