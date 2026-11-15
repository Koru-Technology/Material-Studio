import SwiftUI

// A slider that shows its value in a bubble over the thumb while it is dragged. Every slider in the panels is one.
struct TipSlider: View {
	let title: String
	let value: Float
	let range: ClosedRange<Float>
	var format: (Float) -> String = percent
	var editingChanged: (Bool) -> Void = { _ in }
	let set: (Float) -> Void

	@State private var editing = false

	var body: some View {
		Slider(value: Binding(get: { value }, set: { v in AppStore.shared.perform { set(v) } }), in: range) {
			Text(title)
		} onEditingChanged: { e in
			withAnimation(.easeOut(duration: 0.12)) {
				editing = e
			}
			editingChanged(e)
		}
		.labelsHidden()
		.overlay(alignment: .leading) {
			GeometryReader { geo in
				if editing {
					let thumb = 14.0 // Half the thumb: its center travels from 14 to width - 14
					let f     = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
					ValueBubble(text: format(value))
						.position(x: thumb + (geo.size.width - thumb * 2) * min(1, max(0, f)), y: -18)
						.transition(.opacity)
				}
			}
			.allowsHitTesting(false)
		}
	}
}

// The value shown while a slider moves
struct ValueBubble: View {
	let text: String

	var body: some View {
		Text(text)
			.font(.caption.weight(.semibold).monospacedDigit())
			.padding(.horizontal, 8)
			.padding(.vertical, 4)
			.glassEffect(.regular, in: .capsule)
			.fixedSize()
	}
}

nonisolated func percent(_ v: Float) -> String {
	"\(Int((v * 100).rounded()))%"
}
