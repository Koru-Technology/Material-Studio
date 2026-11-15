import SwiftUI

// The paint color: the picked color (what the color picker tool picks) edited with a hue / saturation-brightness picker and the
// material sliders, applied to the selected material when a gesture ends, as the swatches and the picker do. The swatches of
// the project below: tap picks one, + adds the picked color, the menu deletes.
struct ColorPopover: View {
	private var store = AppStore.shared
	private var colors = ColorStore.shared

	var body: some View {
		ScrollView {
			VStack(alignment: .leading, spacing: 14) {
				ColorHeader(color: colors.picked)
				HSBPicker(base: colors.picked.base)
				valueSliders
				if !colors.canApply {
					applyNote
				}
				Divider()
				SwatchGrid()
			}
			.padding(16)
		}
		.frame(width: 340)
		.frame(minHeight: 300, idealHeight: 640)
#if os(iOS)
		.presentationCompactAdaptation(.popover)
#endif
	}

	private var valueSliders: some View {
		VStack(spacing: 8) {
			ColorValueSlider(title: "Opacity", which: APP_COLOR_OPACITY, value: colors.picked.opacity)
			ColorValueSlider(title: "Roughness", which: APP_COLOR_ROUGHNESS, value: colors.picked.roughness)
			ColorValueSlider(title: "Metallic", which: APP_COLOR_METALLIC, value: colors.picked.metallic)
			DisclosureGroup("More") {
				VStack(spacing: 8) {
					ColorValueSlider(title: "Occlusion", which: APP_COLOR_OCCLUSION, value: colors.picked.occlusion)
					ColorValueSlider(title: "Height", which: APP_COLOR_HEIGHT, value: colors.picked.height)
				}
				.padding(.top, 6)
			}
			.font(.subheadline)
		}
	}

	private var applyNote: some View {
		VStack(alignment: .leading, spacing: 6) {
			Label("The selected material is made of nodes, the color does not change it.", systemImage: "info.circle")
				.font(.footnote)
				.foregroundStyle(.secondary)
			Button {
				store.perform { app_color_new_material() }
			} label: {
				Label("New Material of This Color", systemImage: "circle.fill")
			}
			.buttonStyle(.bordered)
			.controlSize(.small)
		}
	}
}

// The picked color, its hex code, the system color picker and Add to Swatches
private struct ColorHeader: View {
	let color: PaintColor
	@State private var applyTask: Task<Void, Never>?

	var body: some View {
		HStack(spacing: 12) {
			RoundedRectangle(cornerRadius: 8)
				.fill(color.color)
				.frame(width: 44, height: 44)
				.overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator))
			VStack(alignment: .leading, spacing: 2) {
				Text("Color")
					.font(.headline)
				Text(color.hex)
					.font(.caption.monospaced())
					.foregroundStyle(.secondary)
					.textSelection(.enabled)
			}
			Spacer()
			ColorPicker("System Color Picker", selection: Binding(get: { color.color }, set: setFromSystem), supportsOpacity: false)
				.labelsHidden()
			Button {
				AppStore.shared.perform { _ = app_swatch_add() }
			} label: {
				Label("Add to Swatches", systemImage: "plus.square.on.square")
			}
			.labelStyle(.iconOnly)
			.buttonStyle(.bordered)
		}
	}

	// The system picker has no gesture end: the material follows once the color stays a moment
	private func setFromSystem(_ c: Color) {
		guard let rgb = c.rgb8 else {
			return
		}
		AppStore.shared.perform { app_color_set_base(rgb) }
		applyTask?.cancel()
		applyTask = Task { @MainActor in
			try? await Task.sleep(for: .milliseconds(250))
			if !Task.isCancelled {
				AppStore.shared.perform { app_color_apply() }
			}
		}
	}
}

private struct ColorValueSlider: View {
	let title: String
	let which: app_color_value_t
	let value: Float

	var body: some View {
		HStack(spacing: 10) {
			Text(title)
				.font(.subheadline)
				.frame(width: 76, alignment: .leading)
			Slider(value: Binding(get: { Double(value) }, set: { v in
				AppStore.shared.perform { app_color_set_value(Int32(which.rawValue), Float(v)) }
			}), in: 0...1) {
				Text(title)
			} onEditingChanged: { editing in
				if !editing {
					AppStore.shared.perform { app_color_apply() }
				}
			}
			Text("\(Int((value * 100).rounded()))%")
				.font(.caption.monospacedDigit())
				.foregroundStyle(.secondary)
				.frame(width: 38, alignment: .trailing)
		}
	}
}

// The project's swatches
private struct SwatchGrid: View {
	private var colors = ColorStore.shared

	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			Text("Swatches")
				.font(.headline)
			LazyVGrid(columns: [GridItem(.adaptive(minimum: 34, maximum: 44), spacing: 8)], spacing: 8) {
				ForEach(colors.swatches) { swatch in
					SwatchCell(color: swatch.color, selected: swatch.id == colors.selectedSwatch)
						.onTapGesture {
							AppStore.shared.perform { _ = app_swatch_pick(Int32(swatch.id)) }
						}
						.contextMenu {
							Button {
								AppStore.shared.perform { _ = app_swatch_pick(Int32(swatch.id)) }
							} label: {
								Label("Use", systemImage: "paintbrush.pointed")
							}
							if colors.swatches.count > 1 {
								Button(role: .destructive) {
									AppStore.shared.perform { _ = app_swatch_delete(Int32(swatch.id)) }
								} label: {
									Label("Delete", systemImage: "trash")
								}
							}
						}
						.accessibilityLabel(swatch.color.hex)
						.accessibilityAddTraits(.isButton)
				}
				Button {
					AppStore.shared.perform { _ = app_swatch_add() }
				} label: {
					Image(systemName: "plus")
						.frame(maxWidth: .infinity, minHeight: 34)
						.background(RoundedRectangle(cornerRadius: 7).strokeBorder(.secondary, style: StrokeStyle(lineWidth: 1, dash: [3])))
						.contentShape(.rect)
				}
				.buttonStyle(.plain)
				.accessibilityLabel("Add the color to the swatches")
			}
		}
	}
}

private struct SwatchCell: View {
	let color: PaintColor
	let selected: Bool

	var body: some View {
		RoundedRectangle(cornerRadius: 7)
			.fill(color.color)
			.frame(minHeight: 34)
			.aspectRatio(1, contentMode: .fit)
			.overlay(RoundedRectangle(cornerRadius: 7).strokeBorder(.separator))
			.padding(2)
			.overlay {
				if selected {
					RoundedRectangle(cornerRadius: 9)
						.strokeBorder(Color.accentColor, lineWidth: 2)
				}
			}
			.contentShape(.rect)
	}
}

private extension Color {
	// 0xRRGGBB in sRGB, clamped
	var rgb8: UInt32? {
		let r = resolve(in: EnvironmentValues())
		func byte(_ v: Float) -> UInt32 {
			UInt32((min(max(v, 0), 1) * 255).rounded())
		}
		return byte(r.red) << 16 | byte(r.green) << 8 | byte(r.blue)
	}
}
