import SwiftUI

#if os(iOS)
import UIKit
// MARK: - Tools

struct PadToolColumn: View {
	private var store = AppStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 10) {
			VStack(spacing: 10) {
				VStack(spacing: 2) {
					ForEach(paintTools) { tool in
						PadToolButton(tool: tool)
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

// A tool in the column, chosen on touch down rather than on release
struct PadToolButton: View {
	let tool: Tool
	private var store = AppStore.shared

	init(tool: Tool) {
		self.tool = tool
	}

	var body: some View {
		VStack(spacing: 3) {
			Image(systemName: tool.symbol)
				.font(.system(size: 27))
				.symbolRenderingMode(.palette)
				.foregroundStyle(.secondary, tool.group.color) // The group's color on the accent layer
			Text(tool.name)
				.font(.system(size: 10, weight: .medium))
				.foregroundStyle(tool.group.color)
		}
		.frame(width: 66, height: 62)
		.background(store.tool == tool.id ? tool.group.color.mix(with: Color(white: 0.25), by: 0.6) : .clear, in: .rect(cornerRadius: 12))
		.transaction { $0.disablesAnimations = true; $0.animation = nil } // The selection shows at once, no fade
		.contentShape(.rect)
		.gesture(TouchDownGesture { select() })
		.accessibilityElement(children: .ignore)
		.accessibilityLabel(tool.name)
		.accessibilityAddTraits(.isButton)
		.accessibilityAction { select() }
		.padPopover("Tool \(tool.id)", arrowEdge: .leading) {
			ToolPropertiesView()
				.frame(width: 320, height: 460)
		}
	}

	private func select() {
		if store.tool == tool.id {
			PadPopovers.shared.toggle("Tool \(tool.id)") // A second touch on the tool opens its properties
		}
		else {
			if PadPopovers.shared.shown?.hasPrefix("Tool ") == true {
				PadPopovers.shared.close() // Another tool's properties
			}
			store.selectTool(tool.id)
		}
	}
}

// Fires once as a finger lands. A UIKit recognizer: on the tool column SwiftUI's gestures got the touch only on release
// or after ~0.75 s (measured on the iPad), a recognizer is given it at once
struct TouchDownGesture: UIGestureRecognizerRepresentable {
	let action: () -> Void

	final class Recognizer: UIGestureRecognizer {
		override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
			if state == .possible {
				state = .recognized
			}
		}

		override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent) {
			state = .failed
		}

		override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent) {
			state = .failed
		}
	}

	func makeUIGestureRecognizer(context: Context) -> Recognizer {
		let r = Recognizer()
		r.cancelsTouchesInView = false
		r.delaysTouchesBegan   = false
		return r
	}

	func handleUIGestureRecognizerAction(_ recognizer: Recognizer, context: Context) {
		action()
	}
}

// Undo and redo side by side in the bottom left corner
struct PadHistoryControls: View {
	var body: some View {
		GlassEffectContainer(spacing: 10) { // As the view buttons in the other corner
			HStack(spacing: 10) {
				RepeatButton(symbol: "arrow.uturn.backward", label: "Undo", enabled: { AppStore.shared.canUndo }) {
					AppStore.shared.perform { app_undo() }
				}
				RepeatButton(symbol: "arrow.uturn.forward", label: "Redo", enabled: { AppStore.shared.canRedo }) {
					AppStore.shared.perform { app_redo() }
				}
			}
		}
		.foregroundStyle(.primary)
	}
}

// A system button (in the bottom corner fingers reach only it, not a recognizer of ours). Held for a second, it fires
// every 100 ms until let go or nothing is left to do, a tap fires once.
struct RepeatButton: View {
	let symbol: String
	let label: String
	let enabled: () -> Bool
	let action: () -> Void

	@State private var timer: Timer?
	@State private var repeated = false // The release does not fire again

	var body: some View {
		Button {
			if !repeated && enabled() {
				action()
			}
			repeated = false
		} label: {
			Image(systemName: symbol)
				.opacity(enabled() ? 1 : 0.3) // The arrow, the glass stays
				.frame(width: 44, height: 44)
		}
		.buttonStyle(PressReportingStyle { pressed in
			if pressed {
				hold()
			}
			else {
				release()
			}
		})
		.disabled(!enabled())
		.glassEffect(.regular.interactive(), in: .circle) // Its response to the touch
		.accessibilityLabel(label)
		.onDisappear {
			release() // An editor took the screen with the finger still down
		}
	}

	private func hold() {
		release()
		repeated = false
		schedule(after: 1.0) {
			schedule(after: 0.1, repeats: true) {
				if enabled() {
					repeated = true
					action()
				}
				else {
					release()
				}
			}
		}
	}

	private func release() {
		timer?.invalidate()
		timer = nil
	}

	// In the common modes: it fires while touches are tracked
	private func schedule(after interval: TimeInterval, repeats: Bool = false, _ fire: @escaping @MainActor () -> Void) {
		let t = Timer(timeInterval: interval, repeats: repeats) { _ in
			MainActor.assumeIsolated {
				fire()
			}
		}
		RunLoop.main.add(t, forMode: .common)
		timer = t
	}
}

// Reports the press of a button, the interactive glass around it shows it
struct PressReportingStyle: ButtonStyle {
	let pressed: (Bool) -> Void

	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.contentShape(.circle) // The whole button takes the touch, not only the glyph's pixels
			.onChange(of: configuration.isPressed) { _, on in
				pressed(on)
			}
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
