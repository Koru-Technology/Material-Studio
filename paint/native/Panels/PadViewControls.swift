import SwiftUI
import simd

#if os(iOS)
// MARK: - View cube and frame

struct PadViewControls: View {
	private var store = AppStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 10) {
			HStack(alignment: .bottom, spacing: 10) {
				VStack(spacing: 10) {
					CameraButton()
					ViewportButton()
				}
				VStack(spacing: 10) {
					Button {
						run(APP_COMMAND_VIEW_FRAME)
					} label: {
						Image(systemName: "viewfinder")
							.frame(width: 44, height: 44)
					}
					.glassEffect(.regular.interactive(), in: .circle)
					.accessibilityLabel("Frame")
					// 3D view, then the 2D view alone, then both side by side (ui_base_view_mode: 0, 1, 2)
					Button {
						store.perform { app_cycle_view_mode() }
					} label: {
						Image(systemName: store.viewMode == 0 ? "cube" : store.viewMode == 1 ? "photo" : "rectangle.split.2x1")
							.frame(width: 44, height: 44)
					}
					.glassEffect(.regular.interactive(), in: .circle)
					.accessibilityLabel("Views")
				}
				.foregroundStyle(.primary)
				ViewCube()
					.frame(width: 96, height: 96)
					.glassEffect(.regular.interactive(), in: .circle)
			}
		}
	}
}

// The axes cube turns with the camera: tap a face to look at it, drag the cube to turn the view, drag around the ring
// outside the cube to roll it as a two finger twist does
struct ViewCube: View {
	private var store = AppStore.shared
	private enum Drag { case none, turn, roll }
	@State private var drag: Drag     = .none
	@State private var last: CGPoint  = .zero

	private static let size: Float  = 96
	private static let scale: Float = 48 * 0.42 // Cube half size: the corners stay inside the ring seen from any side

	private static let faces: [(normal: SIMD3<Float>, label: String, color: Color)] = [
		(SIMD3(1, 0, 0), "Right", Color(red: 0.85, green: 0.54, blue: 0.53)),
		(SIMD3(-1, 0, 0), "Left", Color(red: 0.72, green: 0.44, blue: 0.42)),
		(SIMD3(0, 1, 0), "Back", Color(red: 0.57, green: 0.79, blue: 0.55)),
		(SIMD3(0, -1, 0), "Front", Color(red: 0.45, green: 0.66, blue: 0.44)),
		(SIMD3(0, 0, 1), "Top", Color(red: 0.55, green: 0.68, blue: 0.88)),
		(SIMD3(0, 0, -1), "Bottom", Color(red: 0.44, green: 0.56, blue: 0.76)),
	]

	// Face corners in view space (the camera's inverse rotation), the cube's half size 1
	private func projected(_ face: Int, _ inv: simd_quatf) -> (corners: [SIMD3<Float>], facing: Float) {
		let n         = Self.faces[face].normal
		let axis      = n.x != 0 ? 0 : n.y != 0 ? 1 : 2
		let u         = SIMD3<Float>(axis == 1 ? 1 : 0, axis == 2 ? 1 : 0, axis == 0 ? 1 : 0)
		let v         = simd_cross(n, u)
		let corners   = [n - u - v, n + u - v, n + u + v, n - u + v].map { inv.act($0) }
		return (corners, inv.act(n).z)
	}

	// The face nearest the viewer at a point of the view, -1 off the cube
	private func face(at location: CGPoint, _ inv: simd_quatf) -> Int {
		let half  = Self.size / 2
		let p     = SIMD2<Float>(Float(location.x) - half, half - Float(location.y)) / Self.scale
		var best  = -1
		var depth = -Float.infinity
		for i in Self.faces.indices {
			let (corners, facing) = projected(i, inv)
			if facing <= 0.05 {
				continue
			}
			if contains(corners.map { SIMD2($0.x, $0.y) }, p) && facing > depth {
				best  = i
				depth = facing
			}
		}
		return best
	}

	// Clockwise on screen from the center (y down)
	private static func angle(_ p: CGPoint) -> Float {
		atan2(Float(p.y) - size / 2, Float(p.x) - size / 2)
	}

	var body: some View {
		let r   = store.cameraRotation
		let inv = simd_quatf(ix: r.x, iy: r.y, iz: r.z, r: r.w).inverse
		Canvas { ctx, size in
			let half  = Float(size.width / 2)
			let scale = Self.scale
			// The ring to roll the view, brighter while it does
			let inset = CGFloat(5)
			ctx.stroke(Path(ellipseIn: CGRect(origin: .zero, size: size).insetBy(dx: inset, dy: inset)),
			           with: .color(.primary.opacity(drag == .roll ? 0.45 : 0.15)), lineWidth: drag == .roll ? 2 : 1.5)
			for i in Self.faces.indices {
				let (corners, facing) = projected(i, inv)
				if facing <= 0.05 {
					continue
				}
				var path = Path()
				for (k, c) in corners.enumerated() {
					let p = CGPoint(x: CGFloat(half + c.x * scale), y: CGFloat(half - c.y * scale))
					k == 0 ? path.move(to: p) : path.addLine(to: p)
				}
				path.closeSubpath()
				ctx.fill(path, with: .color(Self.faces[i].color.opacity(Double(0.45 + 0.55 * facing))))
				ctx.stroke(path, with: .color(.black.opacity(0.25)), lineWidth: 1)
				if facing > 0.5 {
					let c = corners.reduce(SIMD3<Float>(), +) / 4
					ctx.draw(Text(Self.faces[i].label).font(.system(size: 9, weight: .semibold)).foregroundStyle(.black.opacity(0.7)),
					         at: CGPoint(x: CGFloat(half + c.x * scale), y: CGFloat(half - c.y * scale)))
				}
			}
		}
		.contentShape(Circle())
		.gesture(DragGesture(minimumDistance: 4)
			.onChanged { g in
				if drag == .none {
					// Started on the cube: turn. Around it: roll.
					drag = face(at: g.startLocation, inv) >= 0 ? .turn : .roll
					last = g.startLocation
				}
				if drag == .roll {
					var d = Self.angle(g.location) - Self.angle(last)
					d     = d > .pi ? d - 2 * .pi : d < -.pi ? d + 2 * .pi : d
					store.perform { app_view_roll(d, true) }
				}
				else {
					let dx = -Float(g.location.x - last.x) * 0.02 // Drag right turns the view right
					let dy = -Float(g.location.y - last.y) * 0.02 // Drag up turns the view up
					store.perform { app_view_turn(dx, dy) }
				}
				last = g.location
			}
			.onEnded { _ in
				if drag == .roll {
					store.perform { app_view_roll(0, false) } // Levels again if let go near level
				}
				drag = .none
			})
		.onTapGesture { location in
			let best = face(at: location, inv)
			if best >= 0 {
				store.perform { app_view_face(Int32(best)) }
			}
		}
		.accessibilityLabel("View cube")
	}

	private func contains(_ poly: [SIMD2<Float>], _ p: SIMD2<Float>) -> Bool {
		var inside = false
		var j      = poly.count - 1
		for i in poly.indices {
			if (poly[i].y > p.y) != (poly[j].y > p.y),
			   p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x {
				inside.toggle()
			}
			j = i
		}
		return inside
	}
}
#endif
