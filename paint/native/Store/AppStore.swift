import CoreGraphics
import Foundation
import Observation

// The app state the native UI shows, read from the engine after every frame (app_api.h).
// Fields are assigned only when they changed, so SwiftUI redraws only what changed.
@Observable
final class AppStore {
	static let shared = AppStore()

	private(set) var ready         = false
	private(set) var tool: Int32   = 0
	private(set) var brushRadius   = Float(0.5)
	private(set) var brushOpacity  = Float(1)
	private(set) var brushHardness = Float(0.8)
	private(set) var canUndo       = false
	private(set) var canRedo       = false
	private(set) var undoName      = ""


	func refresh() {
		var s = app_state_t()
		app_get_state(&s)
		guard s.ready else {
			return
		}
		set(&ready, true)
		set(&tool, s.tool)
		set(&brushRadius, s.brush_radius)
		set(&brushOpacity, s.brush_opacity)
		set(&brushHardness, s.brush_hardness)
		set(&canUndo, s.undos > 0)
		set(&canRedo, s.redos > 0)
		set(&undoName, String(cString: app_undo_name()))
		LayersStore.shared.refresh(s)
		MaterialsStore.shared.refresh()
		BrushesStore.shared.refresh()
	}


	// Actions: the engine changes, refresh picks it up right away instead of on the next frame
	func perform(_ action: () -> Void) {
		action()
		refresh()
	}
}

// Assigns only a changed value: an @Observable property set to the same value still redraws its views
func set<T: Equatable>(_ field: inout T, _ value: T) {
	if field != value {
		field = value
	}
}
