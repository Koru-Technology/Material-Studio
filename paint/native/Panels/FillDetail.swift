import SwiftUI

// A fill layer's or fill mask's settings, as Painter's fill layers: the projection, then a source for each channel
// (a color or value, an image, a pattern or a material). Edits show live in the viewport.
struct FillSections: View {
	let layer: LayerRow

	private var layers = LayersStore.shared
	private var panel  = LayersPanelState.shared

	init(layer: LayerRow) {
		self.layer = layer
	}

	static let channelNames = ["Base Color", "Opacity", "Normal", "Roughness", "Metallic", "Height", "Occlusion", "Emission"]
	// The layer channel (app_layer_channel_t) each fill channel paints, its eye
	static let paintChannels: [app_layer_channel_t] = [APP_LAYER_CHANNEL_BASE, APP_LAYER_CHANNEL_OPACITY, APP_LAYER_CHANNEL_NORMAL, APP_LAYER_CHANNEL_ROUGHNESS,
	                                                    APP_LAYER_CHANNEL_METALLIC, APP_LAYER_CHANNEL_HEIGHT, APP_LAYER_CHANNEL_OCCLUSION, APP_LAYER_CHANNEL_EMISSION]

	var body: some View {
		let p = layer.props
		projectionSection(p.fill)
		if p.fill.isMask {
			Section("Mask") {
				FillSourceEditor(layer: layer, channel: 0, title: "Mask")
			}
		}
		else {
			Section {
				if !layers.materialNames.isEmpty {
					Menu {
						ForEach(Array(layers.materialNames.enumerated()), id: \.offset) { i, name in
							Button(name) {
								AppStore.shared.perform { _ = app_layer_set_fill_material(layer.id, -1, Int32(i)) }
							}
						}
					} label: {
						Text("Fill From Material")
					}
				}
				DisclosureGroup(isExpanded: panel.expansion("fill.channels")) {
					ForEach(0..<min(p.sources.count, Self.channelNames.count), id: \.self) { c in
						DisclosureGroup(isExpanded: panel.expansion("fill.channel.\(c)")) {
							FillSourceEditor(layer: layer, channel: Int32(c), title: Self.channelNames[c])
							if c == Int(APP_FILL_CHANNEL_NORMAL.rawValue) {
								channelToggle("Normal Blend", APP_LAYER_CHANNEL_NORMAL_BLEND)
							}
							else if c == Int(APP_FILL_CHANNEL_HEIGHT.rawValue) {
								channelToggle("Height Blend", APP_LAYER_CHANNEL_HEIGHT_BLEND)
							}
						} label: {
							HStack(spacing: 8) {
								FillSourceLabel(title: Self.channelNames[c], source: p.sources[c], channel: c)
									.opacity(p.paints(Self.paintChannels[c]) ? 1 : 0.5)
								ChannelEye(layer: layer, channel: Self.paintChannels[c])
							}
						}
					}
					HStack(spacing: 8) { // No source of its own: a material's subsurface
						Text("Subsurface")
							.opacity(p.paints(APP_LAYER_CHANNEL_SUBSURFACE) ? 1 : 0.5)
						Spacer()
						ChannelEye(layer: layer, channel: APP_LAYER_CHANNEL_SUBSURFACE)
					}
				} label: {
					Text("Per Channel Settings")
				}
			} header: {
				Text("Channels")
			}
		}
	}

	private func channelToggle(_ title: String, _ channel: app_layer_channel_t) -> some View {
		Toggle(title, isOn: Binding(get: { layer.props.paints(channel) }, set: { on in
			AppStore.shared.perform { _ = app_layer_set_channel(layer.id, Int32(channel.rawValue), on) }
		}))
	}

	@ViewBuilder
	private func projectionSection(_ f: FillInfo) -> some View {
		Section {
			Picker("Projection", selection: Binding(get: { f.projection }, set: { v in
				AppStore.shared.perform { _ = app_layer_set_fill_projection(layer.id, v) }
			})) {
				Text("UV").tag(Int32(APP_FILL_PROJECTION_UV.rawValue))
				Text("Tri-planar").tag(Int32(APP_FILL_PROJECTION_TRIPLANAR.rawValue))
				Text("Planar").tag(Int32(APP_FILL_PROJECTION_PLANAR.rawValue))
				Text("Decal").tag(Int32(APP_FILL_PROJECTION_DECAL.rawValue))
			}
			FillPairSlider(layer: layer, title: "Scale", x: f.scaleX, y: f.scaleY, kinds: (APP_LAYER_VALUE_FILL_SCALE, APP_LAYER_VALUE_FILL_SCALE_X, APP_LAYER_VALUE_FILL_SCALE_Y),
			               range: 0.05...16)
			FillPairSlider(layer: layer, title: "Offset", x: f.offsetX, y: f.offsetY,
			               kinds: (APP_LAYER_VALUE_FILL_OFFSET, APP_LAYER_VALUE_FILL_OFFSET_X, APP_LAYER_VALUE_FILL_OFFSET_Y), range: -1...1)
			FillValueSlider(layer: layer, title: "Rotation", value: f.angle, kind: APP_LAYER_VALUE_FILL_ANGLE.rawValue, range: 0...360) { "\(Int($0.rounded()))°" }
			if f.projection == Int32(APP_FILL_PROJECTION_TRIPLANAR.rawValue) {
				FillValueSlider(layer: layer, title: "Hardness", value: f.hardness, kind: APP_LAYER_VALUE_FILL_HARDNESS.rawValue, range: 0...1) { "\(Int(($0 * 100).rounded()))%" }
			}
		} header: {
			Text(layer.kind == .mask ? "Fill Mask" : "Fill")
		}
	}
}

// Summary of a fill for the layer list: its material, else what its base color comes from
enum FillSummary {
	static func text(_ p: LayerProps, materials: [String]) -> String {
		guard let base = p.sources.first else {
			return "Fill"
		}
		switch base.kind {
		case Int32(APP_FILL_SOURCE_MATERIAL.rawValue):
			return base.material >= 0 && Int(base.material) < materials.count ? materials[Int(base.material)] : "Fill"
		case Int32(APP_FILL_SOURCE_PROCEDURAL.rawValue):
			let names = LayersStore.patternNames
			return base.pattern >= 0 && Int(base.pattern) < names.count ? names[Int(base.pattern)] : "Pattern"
		case Int32(APP_FILL_SOURCE_TEXTURE.rawValue):
			return "Image"
		default:
			return "Fill"
		}
	}
}

private func fillIsColor(_ channel: Int) -> Bool {
	channel == Int(APP_FILL_CHANNEL_BASE.rawValue) || channel == Int(APP_FILL_CHANNEL_NORMAL.rawValue)
}

private func fillCGColor(_ c: [Float]) -> CGColor {
	CGColor(srgbRed: CGFloat(c[0]), green: CGFloat(c[1]), blue: CGFloat(c[2]), alpha: 1)
}

private func fillRGB(_ c: CGColor) -> [Float] {
	let s = c.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil) ?? c
	let comps = s.components ?? [0, 0, 0, 1]
	if comps.count >= 3 {
		return [Float(comps[0]), Float(comps[1]), Float(comps[2])]
	}
	return [Float(comps[0]), Float(comps[0]), Float(comps[0])]
}

// A channel's row: its name, a swatch of its color or what it comes from
private struct FillSourceLabel: View {
	let title: String
	let source: FillSource
	let channel: Int

	var body: some View {
		HStack {
			Text(title)
			Spacer()
			switch source.kind {
			case Int32(APP_FILL_SOURCE_COLOR.rawValue):
				if fillIsColor(channel) {
					RoundedRectangle(cornerRadius: 4)
						.fill(Color(cgColor: fillCGColor(source.color)))
						.frame(width: 22, height: 16)
						.overlay(RoundedRectangle(cornerRadius: 4).stroke(.secondary.opacity(0.4)))
				}
				else {
					Text(String(format: "%.2f", source.color[0]))
						.foregroundStyle(.secondary)
						.monospacedDigit()
				}
			case Int32(APP_FILL_SOURCE_TEXTURE.rawValue):
				Image(systemName: "photo").foregroundStyle(.secondary)
			case Int32(APP_FILL_SOURCE_PROCEDURAL.rawValue):
				Image(systemName: "square.grid.3x3").foregroundStyle(.secondary)
			default: // A material: no mark
				EmptyView()
			}
		}
	}
}

// One channel's source: its kind, then what that kind takes
struct FillSourceEditor: View {
	let layer: LayerRow
	let channel: Int32
	let title: String

	private var layers = LayersStore.shared

	init(layer: LayerRow, channel: Int32, title: String) {
		self.layer = layer
		self.channel = channel
		self.title = title
	}

	private var isColor: Bool {
		fillIsColor(Int(channel)) || layer.props.fill.isMask
	}

	var body: some View {
		if Int(channel) < layer.props.sources.count {
			let s = layer.props.sources[Int(channel)]
			Picker("Source", selection: Binding(get: { s.kind }, set: { v in
				AppStore.shared.perform { _ = app_layer_set_fill_kind(layer.id, channel, v) }
			})) {
				Text(isColor ? "Color" : "Value").tag(Int32(APP_FILL_SOURCE_COLOR.rawValue))
				Text("Image").tag(Int32(APP_FILL_SOURCE_TEXTURE.rawValue))
				Text("Pattern").tag(Int32(APP_FILL_SOURCE_PROCEDURAL.rawValue))
				Text("Material").tag(Int32(APP_FILL_SOURCE_MATERIAL.rawValue))
			}
			switch s.kind {
			case Int32(APP_FILL_SOURCE_TEXTURE.rawValue):
				texture(s)
			case Int32(APP_FILL_SOURCE_PROCEDURAL.rawValue):
				pattern(s)
			case Int32(APP_FILL_SOURCE_MATERIAL.rawValue):
				material(s)
			default:
				value(s, which: 0, title: isColor ? "Color" : title)
			}
		}
	}

	// A color picker, or a slider for a value channel
	@ViewBuilder
	private func value(_ s: FillSource, which: Int32, title: String) -> some View {
		let c = which == 0 ? s.color : s.color2
		if isColor && !(channel == Int32(APP_FILL_CHANNEL_NORMAL.rawValue) && s.kind == Int32(APP_FILL_SOURCE_PROCEDURAL.rawValue)) {
			ColorPicker(title, selection: Binding(get: { fillCGColor(c) }, set: { v in
				let rgb = fillRGB(v)
				AppStore.shared.perform { _ = app_layer_set_fill_color(layer.id, channel, which, rgb[0], rgb[1], rgb[2]) }
			}), supportsOpacity: false)
		}
		else {
			let kind = Int32(APP_LAYER_VALUE_FILL_SOURCE.rawValue) + channel * 32 + (which == 0 ? Int32(APP_FILL_VALUE.rawValue) : Int32(APP_FILL_VALUE2.rawValue))
			let range: ClosedRange<Float> = channel == Int32(APP_FILL_CHANNEL_NORMAL.rawValue) ? 0...4 : 0...1
			FillValueSlider(layer: layer, title: title, value: c[0], kind: UInt32(kind), range: range) { String(format: "%.2f", $0) }
		}
	}

	@ViewBuilder
	private func texture(_ s: FillSource) -> some View {
		Picker("Image", selection: Binding(get: { s.image }, set: { v in
			AppStore.shared.perform { _ = app_layer_set_fill_image(layer.id, channel, v) }
		})) {
			Text("None").tag(Int32(-1))
			ForEach(Array(layers.imageNames.enumerated()), id: \.offset) { i, name in
				Text(name).tag(Int32(i))
			}
		}
		if !isColor {
			Picker("Image Channel", selection: Binding(get: { s.imageChannel }, set: { v in
				AppStore.shared.perform { _ = app_layer_set_fill_image_channel(layer.id, channel, v) }
			})) {
				Text("Red").tag(Int32(0))
				Text("Green").tag(Int32(1))
				Text("Blue").tag(Int32(2))
				Text("Alpha").tag(Int32(3))
				Text("Luminance").tag(Int32(4))
			}
		}
	}

	@ViewBuilder
	private func pattern(_ s: FillSource) -> some View {
		Picker("Pattern", selection: Binding(get: { s.pattern }, set: { v in
			AppStore.shared.perform { _ = app_layer_set_fill_pattern(layer.id, channel, v) }
		})) {
			ForEach(Array(LayersStore.patternNames.enumerated()), id: \.offset) { i, name in
				Text(name).tag(Int32(i))
			}
		}
		if channel == Int32(APP_FILL_CHANNEL_NORMAL.rawValue) && !layer.props.fill.isMask {
			value(s, which: 0, title: "Strength")
		}
		else {
			value(s, which: 0, title: "Low")
			value(s, which: 1, title: "High")
		}
		ForEach(0..<Int(app_fill_param_count(s.pattern)), id: \.self) { i in
			let kind = Int32(APP_LAYER_VALUE_FILL_SOURCE.rawValue) + channel * 32 + Int32(APP_FILL_VALUE_PARAM.rawValue) + Int32(i)
			let lo = app_fill_param_min(s.pattern, Int32(i))
			let hi = max(app_fill_param_max(s.pattern, Int32(i)), lo + 0.001)
			FillValueSlider(layer: layer, title: String(cString: app_fill_param_name(s.pattern, Int32(i))), value: i < s.params.count ? s.params[i] : 0,
			                kind: UInt32(kind), range: lo...hi) { v in hi > 10 ? String(format: "%.0f", v) : String(format: "%.2f", v) }
		}
	}

	@ViewBuilder
	private func material(_ s: FillSource) -> some View {
		Picker("Material", selection: Binding(get: { s.material }, set: { v in
			AppStore.shared.perform { _ = app_layer_set_fill_material(layer.id, channel, v) }
		})) {
			Text("None").tag(Int32(-1))
			ForEach(Array(layers.materialNames.enumerated()), id: \.offset) { i, name in
				Text(name).tag(Int32(i))
			}
		}
	}
}

// A layer channel shown or hidden, as a layer's eye
private struct ChannelEye: View {
	let layer: LayerRow
	let channel: app_layer_channel_t

	var body: some View {
		let on = layer.props.paints(channel)
		Button {
			AppStore.shared.perform { _ = app_layer_set_channel(layer.id, Int32(channel.rawValue), !on) }
		} label: {
			Image(systemName: on ? "eye" : "eye.slash")
				.frame(width: 28, height: 28)
				.contentShape(.rect)
		}
		.buttonStyle(.borderless)
		.tint(Color.primary)
		.accessibilityLabel(on ? "Hide" : "Show")
	}
}

// An X and Y pair (a fill's scale, offset): one slider setting both while linked, one each when not. Values set apart
// show unlinked; linking sets Y to X.
private struct FillPairSlider: View {
	let layer: LayerRow
	let title: String
	let x: Float
	let y: Float
	let kinds: (both: app_layer_value_t, x: app_layer_value_t, y: app_layer_value_t)
	let range: ClosedRange<Float>

	private var panel = LayersPanelState.shared

	init(layer: LayerRow, title: String, x: Float, y: Float, kinds: (app_layer_value_t, app_layer_value_t, app_layer_value_t), range: ClosedRange<Float>) {
		self.layer = layer
		self.title = title
		self.x = x
		self.y = y
		self.kinds = kinds
		self.range = range
	}

	private var key: String {
		"\(layer.id).\(title)"
	}

	private var linked: Bool {
		!panel.unlinked.contains(key) && x == y
	}

	var body: some View {
		let format: (Float) -> String = { String(format: "%.2f", $0) }
		HStack(spacing: 8) {
			VStack(spacing: 10) {
				if linked {
					FillValueSlider(layer: layer, title: title, value: x, kind: kinds.both.rawValue, range: range, format: format)
				}
				else {
					FillValueSlider(layer: layer, title: "\(title) X", value: x, kind: kinds.x.rawValue, range: range, format: format)
					FillValueSlider(layer: layer, title: "\(title) Y", value: y, kind: kinds.y.rawValue, range: range, format: format)
				}
			}
			Button {
				if linked {
					panel.unlinked.insert(key)
				}
				else {
					panel.unlinked.remove(key)
					let id = layer.id
					let v  = x
					AppStore.shared.perform { _ = app_layer_set_value(id, Int32(kinds.both.rawValue), v) } // Y to X, one undo step
				}
			} label: {
				Image(systemName: "link")
					.overlay { // Struck through while unlinked
						if !linked {
							Capsule()
								.frame(width: 1.5, height: 22)
								.rotationEffect(.degrees(45))
						}
					}
					.frame(width: 28, height: 28)
					.contentShape(.rect)
			}
			.buttonStyle(.borderless)
			.tint(linked ? Color.accentColor : Color.secondary)
			.accessibilityLabel(linked ? "Unlink \(title) X and Y" : "Link \(title) X and Y")
		}
	}
}

// A fill value dragged: one undo step for the whole gesture, shown live
struct FillValueSlider: View {
	let layer: LayerRow
	let title: String
	let value: Float
	let kind: UInt32
	let range: ClosedRange<Float>
	let format: (Float) -> String

	var body: some View {
		let k = Int32(kind)
		LayerSlider(title, value: value, range: range, format: format,
		            begin: { app_layer_value_begin(layer.id, k) },
		            set: { _ = app_layer_set_value(layer.id, k, $0) },
		            end: { app_drag_end() })
	}
}
