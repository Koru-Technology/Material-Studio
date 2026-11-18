import CoreGraphics
import Foundation
import Observation

// A brush stroke held still became a curve (app_curve_*): whether one is being edited, where its points are (canvas
// pixels) and the width of the stroke at each. Read every frame while editing: the points move with the view.
@Observable
final class CurveStore: ChangeOnly {
	static let shared = CurveStore()

	private(set) var editing = false
	private(set) var points: [CGPoint?] = [] // nil: behind the view or off it
	private(set) var sizes: [Float] = []     // Width at each point, 1 the brush's
	private(set) var width = Float(1)        // The whole curve's
	@ObservationIgnored private var lastTap: (index: Int32, time: Date)?

	// A tap on dot index: the second one in a row removes it (not the ends, app_curve_remove)
	func tapped(_ index: Int32) {
		if let last = lastTap, last.index == index, Date().timeIntervalSince(last.time) < curveDoubleTap {
			lastTap = nil
			AppStore.shared.perform { _ = app_curve_remove(index) }
			return
		}
		lastTap = (index, Date())
	}

	func refresh(_ s: app_state_t) {
		update(\.editing, s.curve_editing)
		update(\.width, s.curve_width)
		guard s.curve_editing else {
			update(\.points, [])
			update(\.sizes, [])
			return
		}
		var now: [CGPoint?] = []
		var size: [Float]   = []
		for i in 0..<s.curve_point_count {
			var x = Float(0)
			var y = Float(0)
			now.append(app_curve_point(i, &x, &y) ? CGPoint(x: CGFloat(x), y: CGFloat(y)) : nil)
			size.append(app_curve_point_size(i))
		}
		update(\.points, now)
		update(\.sizes, size)
	}
}
