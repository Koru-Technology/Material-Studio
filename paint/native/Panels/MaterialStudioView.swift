import SwiftUI

// The Material Studio: a full screen editor of a material's few controls next to a large live preview. The engine
// draws the background and the preview, in the place left for it here (StudioPreviewArea), and turns it with a drag.
// Studio materials show presets, layers, pattern, colors, surface and mask; other materials their Variables.
struct MaterialStudioView: View {
	private var store = StudioStore.shared

	var body: some View {
		GeometryReader { geo in
			let landscape = geo.size.width >= geo.size.height
			VStack(spacing: 12) {
				StudioHeader()
				if landscape {
					HStack(spacing: 16) {
						StudioControls()
							.frame(width: (geo.size.width - 16) * 0.45)
						StudioPreviewArea()
					}
				}
				else {
					VStack(spacing: 12) {
						StudioPreviewArea()
							.frame(height: geo.size.height * 0.4)
						StudioControls()
					}
				}
			}
		}
		.padding(16)
		.environment(\.colorScheme, .dark)
	}
}

extension View {
	// On the engine canvas: the studio's preview place is given to the engine in canvas pixels
	func studioCanvas() -> some View {
		onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { frame in
			StudioStore.shared.canvasMoved(frame)
		}
	}
}

// The material's name, then Save, Edit Nodes and Cancel
private struct StudioHeader: View {
	private var store = StudioStore.shared
	@State private var name = ""
	@FocusState private var focused: Bool

	var body: some View {
		HStack(spacing: 10) {
			TextField("Name", text: $name)
				.font(.largeTitle.weight(.bold))
				.textFieldStyle(.plain)
				.focused($focused)
				.onSubmit(commit)
				.onChange(of: focused) { _, now in
					if !now {
						commit()
					}
				}
				.onAppear {
					name = store.name
				}
				.onChange(of: store.name) { _, new in
					if !focused {
						name = new
					}
				}
#if os(iOS)
				.submitLabel(.done)
#endif
			Button {
				AppStore.shared.perform { app_studio_save() }
			} label: {
				Label("Save", systemImage: "checkmark")
			}
			.buttonStyle(.borderedProminent)
			.keyboardShortcut(.defaultAction)
			Button {
				AppStore.shared.perform { app_studio_edit_nodes() }
			} label: {
				Label("Edit Nodes", systemImage: "point.3.connected.trianglepath.dotted")
			}
			.buttonStyle(.bordered)
			Button {
				AppStore.shared.perform { app_studio_cancel() }
			} label: {
				Label("Cancel", systemImage: "xmark")
			}
			.buttonStyle(.bordered)
			.keyboardShortcut(.cancelAction)
		}
		.controlSize(.large)
	}

	private func commit() {
		let trimmed = name.trimmingCharacters(in: .whitespaces)
		if trimmed.isEmpty || trimmed == store.name {
			name = store.name
			return
		}
		let id = store.materialID
		AppStore.shared.perform { _ = app_material_rename(id, trimmed) }
		name = store.name // Made unique by the engine
	}
}

// Left empty: the engine draws the preview here. Its place goes to the engine whenever it moves.
private struct StudioPreviewArea: View {
	private var store = StudioStore.shared
	@Environment(\.displayScale) private var displayScale

	var body: some View {
		Color.clear
			.onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { frame in
				store.previewMoved(frame, scale: displayScale)
			}
			.overlay(alignment: .bottomLeading) {
				Button {
					AppStore.shared.perform { app_studio_toggle_sphere() }
				} label: {
					Label(store.sphere ? "Show Cube" : "Show Sphere", systemImage: store.sphere ? "cube" : "circle")
						.labelStyle(.iconOnly)
						.font(.title3)
						.frame(width: 44, height: 44)
						.contentShape(.circle) // The whole circle: a tap beside the icon's lines would turn the preview
				}
				.buttonStyle(.plain)
				.glassEffect(.regular.interactive(), in: .circle)
				.padding(12)
			}
	}
}

private struct StudioControls: View {
	private var store = StudioStore.shared

	var body: some View {
		Form {
			if store.variables {
				ForEach(store.sections) { section in
					Section(section.name) {
						StudioControlRows(controls: section.controls)
					}
				}
			}
			else {
				StudioPresets()
				StudioLayers()
				if let layer = store.selectedLayer {
					Section {
						StudioPatternGrid()
						StudioControlRows(controls: store.section(0)?.controls ?? [])
					} header: {
						Text(layer.id == 0 ? "Base Pattern" : "Pattern")
					}
					Section("Colors") {
						StudioRamp()
						StudioControlRows(controls: store.section(1)?.controls ?? [])
					}
					ForEach(store.sections.filter { $0.id >= 2 }) { section in
						Section(section.name) {
							StudioControlRows(controls: section.controls)
						}
					}
				}
			}
		}
		.formStyle(.grouped)
		.scrollContentBackground(.hidden)
	}
}

private struct StudioPresets: View {
	private var store = StudioStore.shared
	private let columns = [GridItem(.adaptive(minimum: 110), spacing: 8)]

	var body: some View {
		Section("Presets") {
			LazyVGrid(columns: columns, spacing: 8) {
				ForEach(Array(store.presets.enumerated()), id: \.offset) { i, name in
					Button {
						AppStore.shared.perform { app_studio_apply_preset(Int32(i)) }
					} label: {
						Text(name)
							.lineLimit(1)
							.frame(maxWidth: .infinity)
					}
					.buttonStyle(.bordered)
				}
			}
			.padding(.vertical, 4)
		}
	}
}

// Top first, the base last. Tap picks one, swipe deletes, drag moves (the base stays at the bottom).
private struct StudioLayers: View {
	private var store = StudioStore.shared

	var body: some View {
		Section("Layers") {
			ForEach(store.layers) { layer in
				Button {
					AppStore.shared.perform { app_studio_layer_select(layer.id) }
				} label: {
					HStack(spacing: 12) {
						Group {
							if let tile = store.layerTiles[layer.id] {
								Image(decorative: tile, scale: 1)
									.resizable()
							}
							else {
								Rectangle()
									.fill(.fill.tertiary)
							}
						}
						.frame(width: 44, height: 44)
						.clipShape(.rect(cornerRadius: 8))
						VStack(alignment: .leading, spacing: 2) {
							Text(layer.name)
								.foregroundStyle(.primary)
							Text(layer.subtitle)
								.font(.caption)
								.foregroundStyle(.secondary)
						}
						Spacer()
						if layer.selected {
							Image(systemName: "checkmark")
								.foregroundStyle(.tint)
						}
					}
					.contentShape(.rect)
				}
				.buttonStyle(.plain)
				.listRowBackground(layer.selected ? Color.accentColor.opacity(0.18) : nil)
				.moveDisabled(layer.id == 0)
				.swipeActions(edge: .trailing) {
					if layer.id > 0 {
						Button(role: .destructive) {
							AppStore.shared.perform { app_studio_layer_remove(layer.id) }
						} label: {
							Label("Delete", systemImage: "trash")
						}
					}
				}
				.contextMenu {
					if layer.id > 0 {
						Button(role: .destructive) {
							AppStore.shared.perform { app_studio_layer_remove(layer.id) }
						} label: {
							Label("Delete Layer", systemImage: "trash")
						}
					}
				}
			}
			.onMove(perform: move)
			Menu {
				ForEach(Array(store.layerPresets.enumerated()), id: \.offset) { i, name in
					Button(name) {
						AppStore.shared.perform { _ = app_studio_layer_add(Int32(i)) }
					}
				}
				Divider()
				Button("Empty Layer") {
					AppStore.shared.perform { _ = app_studio_layer_add(-1) }
				}
			} label: {
				Label("Add Layer", systemImage: "plus")
			}
			.disabled(store.layers.count >= store.layerMax)
		}
	}

	// Rows show top first: row d is layer count - 1 - d
	private func move(_ from: IndexSet, _ to: Int) {
		guard let d = from.first else {
			return
		}
		let count = store.layers.count
		let row   = to > d ? to - 1 : to
		let k     = Int32(count - 1 - d)
		let dest  = Int32(count - 1 - row)
		AppStore.shared.perform { app_studio_layer_move(k, dest) }
	}
}

// The patterns, None first, the layer's framed
private struct StudioPatternGrid: View {
	private var store = StudioStore.shared
	private let columns = [GridItem(.adaptive(minimum: 64, maximum: 96), spacing: 8, alignment: .top)]

	var body: some View {
		LazyVGrid(columns: columns, spacing: 8) {
			ForEach(Array(store.patterns.enumerated()), id: \.offset) { i, name in
				let thumb = i < store.patternThumbs.count ? store.patternThumbs[i] : nil
				MaterialTile(image: thumb, name: name, selected: Int32(i) == store.pattern, loading: thumb == nil)
					.onTapGesture {
						AppStore.shared.perform { app_studio_pattern_set(Int32(i)) }
					}
			}
		}
		.padding(.vertical, 4)
	}
}

// The layer's colors: a gradient from the recesses to the surface with a handle for each stop, dragged to move it,
// a color well for each, add, remove, smooth or hard steps. A plain layer has one color.
private struct StudioRamp: View {
	private var store = StudioStore.shared

	var body: some View {
		if store.rampPlain {
			if let stop = store.stops.last {
				StudioColorWell(title: "Color", color: stop.color) { c in
					app_studio_ramp_set_color(stop.id, c.x, c.y, c.z)
				}
			}
		}
		else {
			StudioRampBar(stops: store.stops, hard: store.rampHard)
				.padding(.vertical, 6)
			HStack(spacing: 10) {
				ForEach(store.stops) { stop in
					StudioColorWell(title: "", color: stop.color) { c in
						app_studio_ramp_select(stop.id)
						app_studio_ramp_set_color(stop.id, c.x, c.y, c.z)
					}
					.overlay(alignment: .bottom) {
						if stop.selected {
							Capsule()
								.fill(.tint)
								.frame(width: 14, height: 3)
								.offset(y: 7)
						}
					}
				}
				Spacer(minLength: 0)
			}
			HStack {
				Button {
					AppStore.shared.perform { app_studio_ramp_add() }
				} label: {
					Label("Add Stop", systemImage: "plus")
				}
				.disabled(store.stops.count >= store.rampMax)
				Button {
					AppStore.shared.perform { app_studio_ramp_remove() }
				} label: {
					Label("Remove Stop", systemImage: "minus")
				}
				.disabled(store.stops.count <= 1)
				Spacer()
				Picker("Blend", selection: Binding(get: { store.rampHard }, set: { hard in
					AppStore.shared.perform { app_studio_ramp_set_hard(hard) }
				})) {
					Text("Smooth").tag(false)
					Text("Hard Steps").tag(true)
				}
				.pickerStyle(.segmented)
				.labelsHidden()
				.fixedSize()
			}
			.labelStyle(.iconOnly)
			.buttonStyle(.bordered)
			if let stop = store.stops.first(where: { $0.selected }) {
				LayerSlider("Stop Position", value: stop.pos, range: 0...1, format: percent,
				            begin: { app_studio_edit_begin() },
				            set: { app_studio_ramp_set_pos(stop.id, $0) },
				            end: { app_studio_edit_end() })
			}
		}
	}
}

// The gradient, a handle on each stop: a touch picks the nearest one and drags it
private struct StudioRampBar: View {
	let stops: [StudioStop]
	let hard: Bool
	@State private var dragged: Int32?

	var body: some View {
		GeometryReader { geo in
			let w = geo.size.width
			ZStack(alignment: .leading) {
				RoundedRectangle(cornerRadius: 8)
					.fill(LinearGradient(stops: gradientStops, startPoint: .leading, endPoint: .trailing))
					.overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator))
				ForEach(stops) { stop in
					Circle()
						.fill(stop.color.color)
						.overlay(Circle().strokeBorder(stop.selected ? Color.white : Color.black.opacity(0.6), lineWidth: stop.selected ? 3 : 1.5))
						.frame(width: 22, height: 22)
						.position(x: CGFloat(stop.pos) * w, y: geo.size.height / 2)
				}
			}
			.contentShape(.rect)
			.gesture(DragGesture(minimumDistance: 0)
				.onChanged { g in
					let t = Float(min(max(g.location.x / max(w, 1), 0), 1))
					if dragged == nil {
						guard let nearest = stops.min(by: { abs($0.pos - t) < abs($1.pos - t) }) else {
							return
						}
						dragged = nearest.id
						AppStore.shared.perform {
							app_studio_ramp_select(nearest.id)
							app_studio_edit_begin()
						}
						return // A tap picks it, it moves once the finger does
					}
					if let id = dragged, g.translation != .zero {
						AppStore.shared.perform { app_studio_ramp_set_pos(id, t) }
					}
				}
				.onEnded { _ in
					dragged = nil
					AppStore.shared.perform { app_studio_edit_end() }
				})
		}
		.frame(height: 30)
	}

	private var gradientStops: [Gradient.Stop] {
		guard !stops.isEmpty else {
			return [.init(color: .gray, location: 0), .init(color: .gray, location: 1)]
		}
		if !hard {
			return stops.map { .init(color: $0.color.color, location: CGFloat($0.pos)) }
		}
		var out: [Gradient.Stop] = []
		for (i, s) in stops.enumerated() { // Each color holds until the next stop
			let end = i + 1 < stops.count ? stops[i + 1].pos : 1
			out.append(.init(color: s.color.color, location: CGFloat(s.pos)))
			out.append(.init(color: s.color.color, location: CGFloat(end)))
		}
		return out
	}
}

// The system color picker on a color of the material, each change goes in at once and the edit is one undo step
private struct StudioColorWell: View {
	let title: String
	let color: SIMD3<Float>
	let set: (SIMD3<Float>) -> Void

	var body: some View {
		ColorPicker(selection: Binding(get: { color.color }, set: { c in
			guard let rgb = c.srgb else {
				return
			}
			StudioColorEdit.set { set(rgb) }
		}), supportsOpacity: false) {
			Text(title)
		}
		.labelsHidden(title.isEmpty)
	}
}

private extension View {
	@ViewBuilder func labelsHidden(_ hidden: Bool) -> some View {
		if hidden {
			labelsHidden()
		}
		else {
			self
		}
	}
}

// Sliders, switches, choices, buttons, colors and gradients, as the engine lists them
private struct StudioControlRows: View {
	let controls: [StudioControl]

	var body: some View {
		ForEach(controls) { c in
			row(c)
		}
	}

	@ViewBuilder private func row(_ c: StudioControl) -> some View {
		switch c.kind {
		case .slider:
			LayerSlider(c.name, value: c.value, range: c.range, format: format(c),
			            begin: { app_studio_edit_begin() },
			            set: { v in app_studio_control_set(c.key, c.whole ? v.rounded() : v) },
			            end: { app_studio_edit_end() })
		case .toggle:
			Toggle(c.name, isOn: Binding(get: { c.value != 0 }, set: { on in
				AppStore.shared.perform { app_studio_control_set(c.key, on ? 1 : 0) }
			}))
		case .choice:
			Picker(c.name, selection: Binding(get: { Int(c.value) }, set: { i in
				AppStore.shared.perform { app_studio_control_set(c.key, Float(i)) }
			})) {
				ForEach(Array(c.options.enumerated()), id: \.offset) { i, option in
					Text(option).tag(i)
				}
			}
			.modifier(ChoiceStyle(segmented: c.options.count <= 3))
		case .button:
			Button {
				AppStore.shared.perform { app_studio_control_press(c.key) }
			} label: {
				Label(c.name, systemImage: "shuffle")
			}
			.buttonStyle(.bordered)
		case .color:
			StudioColorWell(title: c.name, color: c.color) { rgb in
				app_studio_control_set_color(c.key, rgb.x, rgb.y, rgb.z)
			}
		case .gradient:
			VStack(alignment: .leading, spacing: 8) {
				Text(c.name)
				RoundedRectangle(cornerRadius: 6)
					.fill(LinearGradient(stops: c.stops.map { .init(color: $0.color.color, location: CGFloat($0.pos)) },
					                     startPoint: .leading, endPoint: .trailing))
					.frame(height: 20)
				HStack(spacing: 10) {
					ForEach(Array(c.stops.enumerated()), id: \.offset) { i, stop in
						StudioColorWell(title: "", color: stop.color) { rgb in
							app_studio_control_set_stop(c.key, Int32(i), rgb.x, rgb.y, rgb.z)
						}
					}
				}
			}
		}
	}

	private func format(_ c: StudioControl) -> (Float) -> String {
		if c.percent || (c.range.lowerBound == 0 && c.range.upperBound == 1) {
			return percent
		}
		if c.whole {
			return { "\(Int($0.rounded()))" }
		}
		let span = c.range.upperBound - c.range.lowerBound
		return { String(format: span > 20 ? "%.1f" : "%.2f", $0) }
	}
}

private struct ChoiceStyle: ViewModifier {
	let segmented: Bool

	func body(content: Content) -> some View {
		if segmented {
			content.pickerStyle(.segmented)
		}
		else {
			content.pickerStyle(.menu)
		}
	}
}
