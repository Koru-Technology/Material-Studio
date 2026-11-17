import SwiftUI

#if os(iOS)
import UIKit
// MARK: - Tools

struct PadToolColumn: View {
	var body: some View {
		VStack(spacing: 2) {
			ForEach(paintTools) { tool in
				PadToolButton(tool: tool)
			}
		}
		.padding(4)
		.glassEffect(.regular, in: .rect(cornerRadius: 16))
		.foregroundStyle(.primary)
	}
}

// Right edge: the brush sliders the tool reads, stacked, and its symmetry below them
struct PadBrushControls: View {
	private var store = AppStore.shared

	var body: some View {
		let controls = Tool.selected(store.tool)?.controls ?? []
		GlassEffectContainer(spacing: 10) {
			VStack(spacing: 10) {
				if controls.contains(.radius) {
					VerticalSlider(title: "Radius", value: store.brushRadius, range: 0.01...2, symbol: "circle.fill", format: radiusText,
					               showsBrush: true) { v in
						app_set_brush_radius(v)
					}
				}
				if controls.contains(.opacity) {
					VerticalSlider(title: "Opacity", value: store.brushOpacity, range: 0...1, symbol: "circle.lefthalf.filled") { v in
						app_set_brush_opacity(v)
					}
				}
				if controls.contains(.hardness) {
					VerticalSlider(title: "Hardness", value: store.brushHardness, range: 0...1, symbol: "smallcircle.filled.circle") { v in
						app_set_brush_hardness(v)
					}
				}
				if controls.contains(.symmetry) {
					SymmetryButton()
				}
			}
		}
		.foregroundStyle(.primary)
	}
}

// The mirror axes as lines through the middle, lit in their axis color when on: X across, Z up, Y in depth
struct SymmetryGizmo: View {
	let on: [Bool]

	var body: some View {
		Canvas { c, size in
			let m  = CGPoint(x: size.width / 2, y: size.height / 2)
			let r  = min(size.width, size.height) * 0.36
			let d  = r * 0.72
			let ends: [(CGPoint, CGPoint)] = [
				(CGPoint(x: m.x - r, y: m.y), CGPoint(x: m.x + r, y: m.y)),           // X
				(CGPoint(x: m.x + d, y: m.y - d), CGPoint(x: m.x - d, y: m.y + d)),   // Y
				(CGPoint(x: m.x, y: m.y + r), CGPoint(x: m.x, y: m.y - r)),           // Z
			]
			for axis in [1, 0, 2] { // Lit axes over the others
				var p = Path()
				p.move(to: ends[axis].0)
				p.addLine(to: ends[axis].1)
				let lit = axis < on.count && on[axis]
				c.stroke(p, with: .color(lit ? symmetryAxes[axis].color : Color.secondary.opacity(0.45)),
				         style: StrokeStyle(lineWidth: lit ? 3 : 2, lineCap: .round))
			}
		}
	}
}

struct SymmetryButton: View {
	private var store    = AppStore.shared
	private var popovers = PadPopovers.shared

	var body: some View {
		Button {
			popovers.toggle("Symmetry")
		} label: {
			SymmetryGizmo(on: store.symmetry)
				.frame(width: 44, height: 44)
				.contentShape(.circle)
		}
		.glassEffect(popovers.shown == "Symmetry" ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .circle)
		.accessibilityLabel("Symmetry")
		.padPopover("Symmetry", arrowEdge: .trailing) {
			SymmetryPanel()
		}
	}
}

struct SymmetryPanel: View {
	var body: some View {
		VStack(alignment: .leading, spacing: 10) {
			Text("Symmetry")
				.font(.headline)
			HStack(spacing: 8) {
				ForEach(0..<3, id: \.self) { axis in
					Toggle(isOn: symmetryBinding(axis)) {
						Text(symmetryAxes[axis].name)
							.font(.headline)
							.frame(width: 36, height: 28)
					}
					.toggleStyle(.button)
					.tint(symmetryAxes[axis].color)
					.accessibilityLabel("Symmetry \(symmetryAxes[axis].name)")
				}
			}
		}
		.padding(14)
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
	}

	private func select() {
		if store.tool != tool.id {
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

// A slider standing up, dragged anywhere on its track; the brush settings on the right edge. While dragged, its title and
// value show to its left, level with the fill, and the radius shows the brush circle in the middle of the view.
struct VerticalSlider: View {
	let title: String
	let value: Float
	let range: ClosedRange<Float>
	let symbol: String
	var format: (Float) -> String = percent
	var showsBrush = false
	let set: (Float) -> Void

	@State private var start: Float?
	@State private var height: CGFloat = 0

	private let width: CGFloat = 30 // The track and its padding

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
					if start == nil && showsBrush {
						AppStore.shared.perform { app_set_brush_preview(true) }
					}
					let base   = start ?? value
					start      = base
					let delta  = Float(-g.translation.height / geo.size.height) * (range.upperBound - range.lowerBound)
					let v      = min(range.upperBound, max(range.lowerBound, base + delta))
					AppStore.shared.perform { set(v) }
				}
				.onEnded { _ in
					start = nil
					if showsBrush {
						AppStore.shared.perform { app_set_brush_preview(false) }
					}
				})
		}
		.frame(width: width - 12, height: 150)
		.padding(6)
		.glassEffect(.regular.interactive(), in: .capsule)
		.accessibilityElement()
		.accessibilityLabel(title)
		.accessibilityValue(format(value))
		.onGeometryChange(for: CGFloat.self) { $0.size.height } action: { height = $0 }
		.overlay(alignment: .bottomTrailing) {
			if start != nil {
				let f = CGFloat((value - range.lowerBound) / (range.upperBound - range.lowerBound))
				ValueBubble(text: "\(title): \(format(value))")
					.offset(x: -(width + 10), y: -max(12, (height - 12) * min(1, max(0, f))))
					.transition(.opacity)
			}
		}
		.animation(.easeOut(duration: 0.12), value: start != nil)
		.onDisappear {
			if start != nil && showsBrush { // The tool changed with the finger still down
				AppStore.shared.perform { app_set_brush_preview(false) }
			}
		}
	}
}
#endif
