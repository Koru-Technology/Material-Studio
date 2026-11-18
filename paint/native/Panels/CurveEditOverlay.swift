import SwiftUI

// The points of a curve being edited (CurveStore), over the canvas: a white dot each, dragged to reshape the curve, a
// double tap removes it (not the ends). Beside each dot an orange one sets the width of the stroke there: dragged
// toward the dot it narrows, away it widens, a double tap goes back to the brush's. The dots' hit areas take the touch,
// so dragging never paints; a touch between them reaches the canvas: on the curve a tap adds a point, elsewhere it
// confirms the curve (the engine). Placed as an overlay of the canvas: its coordinates are the canvas' pixels over the
// display scale.
struct CurveEditOverlay: View {
	private var curve = CurveStore.shared
	@Environment(\.displayScale) private var scale

	var body: some View {
		let points = curve.points // One snapshot: the rows and their neighbours read the same points
		let sizes  = curve.sizes
		ZStack(alignment: .topLeading) {
			if curve.editing {
				ForEach(Array(points.enumerated()), id: \.offset) { i, p in
					if let p {
						let at = CGPoint(x: p.x / scale, y: p.y / scale)
						CurveSizeHandle(index: Int32(i), center: at, normal: Self.normal(points, i), size: i < sizes.count ? sizes[i] : 1)
						CurvePointHandle(index: Int32(i), position: at, scale: scale)
					}
				}
			}
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
		.coordinateSpace(.named(curveSpace))
	}

	// Across the curve at point i of p, on the side facing up: where its width dot sits
	private static func normal(_ p: [CGPoint?], _ i: Int) -> CGVector {
		guard i < p.count else {
			return CGVector(dx: 0, dy: -1)
		}
		let here = p[i] ?? .zero
		let prev = (i > 0 ? p[i - 1] : nil) ?? here
		let next = (i + 1 < p.count ? p[i + 1] : nil) ?? here
		let dx   = next.x - prev.x
		let dy   = next.y - prev.y
		let len  = hypot(dx, dy)
		guard len > 0.001 else {
			return CGVector(dx: 0, dy: -1)
		}
		var n = CGVector(dx: -dy / len, dy: dx / len)
		if n.dy > 0 {
			n = CGVector(dx: -n.dx, dy: -n.dy)
		}
		return n
	}
}

private let curveSpace = "curve"

// Two taps on a dot within this many seconds are a double tap
let curveDoubleTap = 0.35

private struct CurvePointHandle: View {
	let index: Int32
	let position: CGPoint
	let scale: CGFloat

	@State private var grab: CGSize? // From the finger to the dot's center, kept while dragging: the dot does not jump
	@State private var dragging = false // Moved past a tap's slop

	var body: some View {
		Circle()
			.fill(.white)
			.frame(width: 14, height: 14)
			.overlay(Circle().stroke(.black.opacity(0.55), lineWidth: 2))
			.shadow(color: .black.opacity(0.3), radius: 2)
			.scaleEffect(grab != nil ? 1.3 : 1)
			.frame(width: 44, height: 44)
			.contentShape(.circle)
			.position(position)
			.gesture(
				DragGesture(minimumDistance: 0, coordinateSpace: .named(curveSpace))
					.onChanged { g in
						let offset = grab ?? CGSize(width: position.x - g.startLocation.x, height: position.y - g.startLocation.y)
						grab       = offset
						if !dragging && hypot(g.translation.width, g.translation.height) < 4 {
							return // Still a tap: the point stays where it is
						}
						dragging = true
						let x    = Float((g.location.x + offset.width) * scale)
						let y    = Float((g.location.y + offset.height) * scale)
						AppStore.shared.perform { app_curve_move(index, x, y) }
					}
					.onEnded { _ in
						if !dragging {
							CurveStore.shared.tapped(index)
						}
						grab     = nil
						dragging = false
					}
			)
			.animation(.easeOut(duration: 0.12), value: grab != nil)
	}
}

// The width of the stroke at a point: an orange dot at CurveSizeHandle.near plus CurveSizeHandle.per times the size from
// the point's dot, across the curve. Dragged, its distance from the point's dot sets the size (in any direction while
// the finger holds it); a double tap sets 1.
private struct CurveSizeHandle: View {
	let index: Int32
	let center: CGPoint
	let normal: CGVector
	let size: Float

	static let near: CGFloat = 20 // Points from the point's dot at size 0: clear of its hit area's middle
	static let per: CGFloat  = 24 // Points further for each 1 of size

	@State private var held: CGPoint? // Where the finger has it while dragging
	@State private var lastTap = Date.distantPast

	private var position: CGPoint {
		if let held {
			return held
		}
		let d = Self.near + Self.per * CGFloat(size)
		return CGPoint(x: center.x + normal.dx * d, y: center.y + normal.dy * d)
	}

	var body: some View {
		ZStack {
			Path { path in
				path.move(to: center)
				path.addLine(to: position)
			}
			.stroke(.orange.opacity(0.7), lineWidth: 1.5)
			.allowsHitTesting(false)
			Circle()
				.fill(.orange)
				.frame(width: 10, height: 10)
				.overlay(Circle().stroke(.white, lineWidth: 1.5))
				.shadow(color: .black.opacity(0.3), radius: 2)
				.scaleEffect(held != nil ? 1.3 : 1)
				.frame(width: 32, height: 32)
				.contentShape(.circle)
				.position(position)
				.gesture(
					DragGesture(minimumDistance: 0, coordinateSpace: .named(curveSpace))
						.onChanged { g in
							guard held != nil || hypot(g.translation.width, g.translation.height) >= 3 else {
								return // Still a tap
							}
							held   = g.location
							let d  = hypot(g.location.x - center.x, g.location.y - center.y)
							let s  = Float(max(0, (d - Self.near) / Self.per))
							AppStore.shared.perform { app_curve_set_point_size(index, s) }
						}
						.onEnded { _ in
							if held == nil { // A tap: the second in a row resets
								if Date().timeIntervalSince(lastTap) < curveDoubleTap {
									AppStore.shared.perform { app_curve_set_point_size(index, 1) }
									lastTap = .distantPast
								}
								else {
									lastTap = Date()
								}
							}
							held = nil
						}
				)
		}
		.animation(.easeOut(duration: 0.12), value: held != nil)
	}
}

// The width of the whole curve, then Cancel (drops the curve) and Confirm (paints it)
struct CurveButtons: View {
	private var curve = CurveStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 12) {
			VStack(spacing: 12) {
				CurveScrubber(title: "Stroke", value: curve.width, range: 0.05...10) { v in
					AppStore.shared.perform { app_curve_set_width(v) }
				}
				buttons
			}
		}
	}

	private var buttons: some View {
		HStack(spacing: 12) {
			Button {
				AppStore.shared.perform { app_curve_cancel() }
			} label: {
				label("Cancel")
			}
			.buttonStyle(.plain)
			.glassEffect(.regular.interactive(), in: .capsule)
			.keyboardShortcut(.cancelAction)
			Button {
				AppStore.shared.perform { app_curve_confirm() }
			} label: {
				label("Confirm")
					.foregroundStyle(.white)
			}
			.buttonStyle(.plain)
			.glassEffect(.regular.tint(.accentColor).interactive(), in: .capsule)
			.keyboardShortcut(.defaultAction)
		}
	}

	private func label(_ text: String) -> some View {
		Text(text)
			.font(.headline)
			.padding(.horizontal, 24)
			.frame(height: 44)
			.contentShape(.capsule)
	}
}

// A value dragged sideways without end: the drag scales it (half a control's width doubles or halves it), the ruler
// under it scrolls along. Clamped to range only. A double tap goes back to 100%.
private struct CurveScrubber: View {
	let title: String
	let value: Float
	let range: ClosedRange<Float>
	let set: (Float) -> Void

	@State private var start: Float?     // The value when the drag started
	@State private var travel: CGFloat = 0 // Points dragged, scrolls the ruler

	private static let doubling: CGFloat = 80 // Points dragged to double the value

	var body: some View {
		HStack(spacing: 8) {
			Text(title)
				.font(.subheadline.weight(.semibold))
			Spacer(minLength: 4)
			Text("\(Int((value * 100).rounded()))%")
				.font(.subheadline.monospacedDigit())
				.foregroundStyle(.secondary)
		}
		.padding(.horizontal, 16)
		.frame(width: 170, height: 44)
		.background(alignment: .bottom) {
			Ruler(offset: travel)
				.frame(height: 8)
				.padding(.horizontal, 14)
				.padding(.bottom, 4)
		}
		.contentShape(.capsule)
		.glassEffect(start != nil ? .regular.tint(.orange.opacity(0.4)).interactive() : .regular.interactive(), in: .capsule)
		.gesture(
			DragGesture(minimumDistance: 2)
				.onChanged { g in
					let from = start ?? max(value, 0.01) // Scaled from 0 it would stay 0
					start    = from
					travel   = g.translation.width
					var v    = from * Float(pow(2.0, g.translation.width / Self.doubling))
					if range.lowerBound == 0 && v < 0.01 {
						v = 0
					}
					set(min(range.upperBound, max(v, range.lowerBound)))
				}
				.onEnded { _ in
					start = nil
				}
		)
		.simultaneousGesture(
			TapGesture(count: 2).onEnded {
				set(1)
			}
		)
	}

	// Ticks scrolling with the drag: a dial without ends
	private struct Ruler: View {
		let offset: CGFloat

		var body: some View {
			Canvas { context, size in
				let step: CGFloat = 8
				var x = offset.truncatingRemainder(dividingBy: step)
				if x < 0 {
					x += step
				}
				while x < size.width {
					let h: CGFloat = 4
					context.fill(Path(CGRect(x: x, y: size.height - h, width: 1, height: h)), with: .color(.secondary.opacity(0.5)))
					x += step
				}
			}
			.allowsHitTesting(false)
		}
	}
}
