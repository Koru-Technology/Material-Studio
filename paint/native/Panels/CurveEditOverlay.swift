import SwiftUI

// The points of a curve being edited (CurveStore), over the canvas: a dot each, dragged to reshape the curve, a double
// tap removes it (not the ends). The dot's hit area takes the touch, so dragging never paints; a touch between them
// reaches the canvas: on the curve a tap adds a point, elsewhere it confirms the curve (the engine).
// Placed as an overlay of the canvas: its coordinates are the canvas' pixels over the display scale.
struct CurveEditOverlay: View {
	private var curve = CurveStore.shared
	@Environment(\.displayScale) private var scale

	var body: some View {
		ZStack(alignment: .topLeading) {
			if curve.editing {
				ForEach(Array(curve.points.enumerated()), id: \.offset) { i, p in
					if let p {
						CurveHandle(index: Int32(i), position: CGPoint(x: p.x / scale, y: p.y / scale), scale: scale)
					}
				}
			}
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
		.coordinateSpace(.named(CurveHandle.space))
	}
}

private struct CurveHandle: View {
	static let space = "curve"

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
				DragGesture(minimumDistance: 0, coordinateSpace: .named(Self.space))
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

// Paints the curve as a stroke
struct CurveConfirmButton: View {
	var body: some View {
		Button {
			AppStore.shared.perform { app_curve_confirm() }
		} label: {
			Text("Confirm")
				.font(.headline)
				.foregroundStyle(.white)
				.padding(.horizontal, 24)
				.frame(height: 44)
				.contentShape(.capsule)
		}
		.buttonStyle(.plain)
		.glassEffect(.regular.tint(.accentColor).interactive(), in: .capsule)
		.keyboardShortcut(.defaultAction)
	}
}
