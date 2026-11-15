import SwiftUI

#if os(iOS)
// MARK: - Tools

struct PadToolColumn: View {
	private var store = AppStore.shared
	@State private var propertiesShown = false

	var body: some View {
		GlassEffectContainer(spacing: 10) {
			VStack(spacing: 10) {
				VStack(spacing: 2) {
					ForEach(paintTools) { tool in
						Button {
							if store.tool == tool.id {
								propertiesShown.toggle() // A second tap on the tool opens its properties
							}
							else {
								store.perform { app_set_tool(tool.id) }
							}
						} label: {
							Image(systemName: tool.symbol)
								.font(.system(size: 18))
								.frame(width: 44, height: 40)
								.background(store.tool == tool.id ? Color.accentColor.opacity(0.35) : .clear, in: .rect(cornerRadius: 10))
						}
						.accessibilityLabel(tool.name)
						.popover(isPresented: Binding(get: { propertiesShown && store.tool == tool.id }, set: { propertiesShown = $0 }),
						         arrowEdge: .leading) {
							ToolPropertiesView()
								.frame(width: 320)
								.presentationCompactAdaptation(.popover)
						}
					}
				}
				.padding(4)
				.glassEffect(.regular, in: .rect(cornerRadius: 16))

				HStack(spacing: 8) {
					VerticalSlider(value: store.brushRadius, range: 0.01...2, symbol: "circle.fill", format: radiusText, bubbleX: 76) { v in
						app_set_brush_radius(v)
					}
					VerticalSlider(value: store.brushOpacity, range: 0...1, symbol: "circle.lefthalf.filled") { v in
						app_set_brush_opacity(v)
					}
				}
				.frame(height: 220)
			}
		}
		.foregroundStyle(.primary)
	}
}

// Undo and redo side by side in the bottom left corner
struct PadHistoryControls: View {
	private var store = AppStore.shared

	var body: some View {
		HStack(spacing: 2) {
			Button {
				store.perform { app_undo() }
			} label: {
				Image(systemName: "arrow.uturn.backward").frame(width: 44, height: 40)
			}
			.disabled(!store.canUndo)
			.opacity(store.canUndo ? 1 : 0.3) // The primary style set on the capsule keeps the system's dimming away
			.accessibilityLabel("Undo")
			Button {
				store.perform { app_redo() }
			} label: {
				Image(systemName: "arrow.uturn.forward").frame(width: 44, height: 40)
			}
			.disabled(!store.canRedo)
			.opacity(store.canRedo ? 1 : 0.3)
			.accessibilityLabel("Redo")
		}
		.padding(4)
		.glassEffect(.regular, in: .capsule)
		.foregroundStyle(.primary)
	}
}

// A slider standing up, dragged anywhere on its track; the brush size and opacity
struct VerticalSlider: View {
	let value: Float
	let range: ClosedRange<Float>
	let symbol: String
	var format: (Float) -> String = percent
	var bubbleX: CGFloat = 48 // The bubble's distance to the right, past the other slider of the pair
	let set: (Float) -> Void

	@State private var start: Float?
	@State private var height: CGFloat = 0

	var body: some View {
		GeometryReader { geo in
			let f = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
			ZStack(alignment: .bottom) {
				Capsule().fill(.clear)
				Capsule()
					.fill(Color.accentColor.opacity(0.55))
					.frame(height: max(12, geo.size.height * f))
				Image(systemName: symbol)
					.font(.system(size: 10))
					.padding(.bottom, 8)
			}
			.contentShape(Capsule())
			.gesture(DragGesture(minimumDistance: 0)
				.onChanged { g in
					let base   = start ?? value
					start      = base
					let delta  = Float(-g.translation.height / geo.size.height) * (range.upperBound - range.lowerBound)
					let v      = min(range.upperBound, max(range.lowerBound, base + delta))
					AppStore.shared.perform { set(v) }
				}
				.onEnded { _ in start = nil })
		}
		.frame(width: 18)
		.padding(6)
		.glassEffect(.regular.interactive(), in: .capsule)
		.onGeometryChange(for: CGFloat.self) { $0.size.height } action: { height = $0 }
		.overlay(alignment: .bottom) {
			if start != nil { // While dragged: the value to the right of the sliders, level with the fill
				let f = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
				ValueBubble(text: format(value))
					.offset(x: bubbleX, y: -max(12, (height - 12) * min(1, max(0, f))))
					.transition(.opacity)
			}
		}
		.animation(.easeOut(duration: 0.12), value: start != nil)
	}
}
#endif
