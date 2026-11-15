import SwiftUI

// Saturation / brightness square over a hue bar, editing the picked color's base color. The color follows the drag,
// the material takes it when the drag ends (app_color_apply). Hue is kept here: a gray or black color has none.
struct HSBPicker: View {
	let base: UInt32 // The picked color, 0xAARRGGBB

	@State private var hue: Double = 0
	@State private var saturation: Double = 0
	@State private var brightness: Double = 1
	@State private var sent: UInt32? // The last color sent, not read back into hue / saturation / brightness

	var body: some View {
		VStack(spacing: 12) {
			GeometryReader { geo in
				let size = geo.size
				ZStack {
					Color(hue: hue, saturation: 1, brightness: 1)
					LinearGradient(colors: [.white, .white.opacity(0)], startPoint: .leading, endPoint: .trailing)
					LinearGradient(colors: [.black.opacity(0), .black], startPoint: .top, endPoint: .bottom)
				}
				.clipShape(.rect(cornerRadius: 10))
				.overlay(alignment: .topLeading) {
					knob(Color(hue: hue, saturation: saturation, brightness: brightness))
						.position(x: saturation * size.width, y: (1 - brightness) * size.height)
				}
				.contentShape(.rect)
				.gesture(DragGesture(minimumDistance: 0)
					.onChanged { g in
						saturation = clamp(g.location.x / max(size.width, 1))
						brightness = 1 - clamp(g.location.y / max(size.height, 1))
						send()
					}
					.onEnded { _ in
						apply()
					})
			}
			.frame(height: 190)
			.accessibilityElement()
			.accessibilityLabel("Saturation and brightness")
			.accessibilityValue("\(Int(saturation * 100))% saturation, \(Int(brightness * 100))% brightness")

			GeometryReader { geo in
				let width = geo.size.width
				Capsule()
					.fill(LinearGradient(colors: stride(from: 0.0, through: 1.0, by: 1.0 / 6).map { Color(hue: $0, saturation: 1, brightness: 1) },
					                     startPoint: .leading, endPoint: .trailing))
					.overlay(alignment: .leading) {
						knob(Color(hue: hue, saturation: 1, brightness: 1))
							.position(x: hue * width, y: geo.size.height / 2)
					}
					.contentShape(.rect)
					.gesture(DragGesture(minimumDistance: 0)
						.onChanged { g in
							hue = clamp(g.location.x / max(width, 1))
							send()
						}
						.onEnded { _ in
							apply()
						})
			}
			.frame(height: 24)
			.accessibilityElement()
			.accessibilityLabel("Hue")
			.accessibilityValue("\(Int(hue * 360)) degrees")
			.accessibilityAdjustableAction { direction in
				hue = clamp(hue + (direction == .increment ? 1.0 / 36 : -1.0 / 36))
				send()
				apply()
			}
		}
		.onAppear {
			read(base)
		}
		.onChange(of: base) { _, new in
			if new != sent {
				read(new)
			}
		}
	}

	private func knob(_ fill: Color) -> some View {
		Circle()
			.fill(fill)
			.frame(width: 22, height: 22)
			.overlay(Circle().strokeBorder(.white, lineWidth: 3))
			.shadow(color: .black.opacity(0.35), radius: 2)
			.allowsHitTesting(false)
	}

	private func clamp(_ v: Double) -> Double {
		min(max(v, 0), 1)
	}

	private func send() {
		let rgb = hsbToRGB(hue, saturation, brightness)
		sent = 0xff00_0000 | rgb
		AppStore.shared.perform { app_color_set_base(rgb) }
	}

	private func apply() {
		AppStore.shared.perform { app_color_apply() }
	}

	// A color set elsewhere (a swatch, the picker tool): hue stays when the color has none
	private func read(_ argb: UInt32) {
		let r = Double((argb >> 16) & 0xff) / 255
		let g = Double((argb >> 8) & 0xff) / 255
		let b = Double(argb & 0xff) / 255
		let maxV = max(r, g, b)
		let minV = min(r, g, b)
		let delta = maxV - minV
		brightness = maxV
		saturation = maxV > 0 ? delta / maxV : 0
		if delta > 0 {
			var h: Double
			if maxV == r {
				h = (g - b) / delta
			}
			else if maxV == g {
				h = (b - r) / delta + 2
			}
			else {
				h = (r - g) / delta + 4
			}
			h /= 6
			hue = h < 0 ? h + 1 : h
		}
		sent = nil
	}
}

// 0xRRGGBB
private func hsbToRGB(_ h: Double, _ s: Double, _ v: Double) -> UInt32 {
	let i = Int((h * 6).rounded(.down)) % 6
	let f = h * 6 - (h * 6).rounded(.down)
	let p = v * (1 - s)
	let q = v * (1 - f * s)
	let t = v * (1 - (1 - f) * s)
	let (r, g, b): (Double, Double, Double)
	switch i {
	case 0: (r, g, b) = (v, t, p)
	case 1: (r, g, b) = (q, v, p)
	case 2: (r, g, b) = (p, v, t)
	case 3: (r, g, b) = (p, q, v)
	case 4: (r, g, b) = (t, p, v)
	default: (r, g, b) = (v, p, q)
	}
	func byte(_ c: Double) -> UInt32 {
		UInt32((min(max(c, 0), 1) * 255).rounded())
	}
	return byte(r) << 16 | byte(g) << 8 | byte(b)
}
