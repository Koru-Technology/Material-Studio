import CoreGraphics
import Foundation
import Observation

// A brush stroke held still became a curve (app_curve_*): whether one is being edited and where its points are, in
// canvas pixels. Read every frame while editing: the points move with the view.
@Observable
final class CurveStore: ChangeOnly {
	static let shared = CurveStore()

	private(set) var editing = false
	private(set) var points: [CGPoint?] = [] // nil: behind the view or off it
	@ObservationIgnored private var lastTap: (index: Int32, time: Date)?

	// A tap on dot index: the second one in a row removes it (not the ends, app_curve_remove)
	func tapped(_ index: Int32) {
		if let last = lastTap, last.index == index, Date().timeIntervalSince(last.time) < 0.35 {
			lastTap = nil
			AppStore.shared.perform { _ = app_curve_remove(index) }
			return
		}
		lastTap = (index, Date())
	}

	func refresh(_ s: app_state_t) {
		update(\.editing, s.curve_editing)
		guard s.curve_editing else {
			update(\.points, [])
			return
		}
		var now: [CGPoint?] = []
		for i in 0..<s.curve_point_count {
			var x = Float(0)
			var y = Float(0)
			now.append(app_curve_point(i, &x, &y) ? CGPoint(x: CGFloat(x), y: CGFloat(y)) : nil)
		}
		update(\.points, now)
	}
}
