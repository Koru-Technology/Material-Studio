import CoreGraphics
import Foundation
import Observation

// The app state the native UI shows, read from the engine after every frame (app_api.h).
// Fields are assigned only when they changed, so SwiftUI redraws only what changed.
@Observable
final class AppStore: ChangeOnly {
	static let shared = AppStore()

	private(set) var ready         = false
	private(set) var tool: Int32   = 0
	private var pendingTool: Int32? // Chosen and shown, set in the engine after the next frame
	private(set) var brushRadius   = Float(0.5)
	private(set) var brushOpacity  = Float(1)
	private(set) var brushHardness = Float(0.8)
	private(set) var canUndo       = false
	private(set) var canRedo       = false
	private(set) var undoName      = ""
	private(set) var redoName      = ""
	private(set) var historyToast: HistoryToast? // The last undo or redo the user asked for, shown briefly
	private var noticeSerial: UInt32 = 0
	private(set) var viewMode: Int32     = 0
	private(set) var viewportMode: Int32 = 0
	private(set) var brushAngle          = Float(0)
	private(set) var symmetry            = [false, false, false]
	private(set) var engineModal         = false
	private(set) var cameraFov           = Float(0.8)
	private(set) var cameraPivot: Int32    = 0
	private(set) var cameraControls: Int32 = 0
	private(set) var cameraType: Int32     = 0
	private(set) var fingerPaint: Int32    = 0 // 0 auto, 1 always, 2 never
	// The new project sheet, also asked for by the engine (its projects box, the shortcut). Launch argument
	// "-NativeShow NewProject" opens it (screenshots from scripted runs)
	var newProjectShown = UserDefaults.standard.string(forKey: "NativeShow") == "NewProject"
	var bakeShown       = UserDefaults.standard.string(forKey: "NativeShow") == "Bake" // Bake Mesh Maps
	private(set) var cameraRotation      = SIMD4<Float>(0, 0, 0, 1)


	func refresh() {
		var s = app_state_t()
		app_get_state(&s)
		guard s.ready else {
			return
		}
		update(\.ready, true)
		update(\.tool, pendingTool ?? s.tool)
		update(\.brushRadius, s.brush_radius)
		update(\.brushOpacity, s.brush_opacity)
		update(\.brushHardness, s.brush_hardness)
		update(\.canUndo, s.undos > 0)
		update(\.canRedo, s.redos > 0)
		update(\.undoName, String(cString: app_undo_name()))
		update(\.redoName, String(cString: app_redo_name()))
		let notice = app_history_notice()
		if notice.serial != noticeSerial {
			noticeSerial = notice.serial
			historyToast = HistoryToast(id: notice.serial, name: String(cString: notice.name), redo: notice.redo)
		}
		update(\.viewMode, s.view_mode)
		update(\.viewportMode, s.viewport_mode)
		update(\.brushAngle, s.brush_angle)
		update(\.symmetry, [s.sym_x, s.sym_y, s.sym_z])
		update(\.engineModal, s.engine_modal)
		update(\.cameraFov, s.camera_fov)
		update(\.cameraPivot, s.camera_pivot)
		update(\.cameraControls, s.camera_controls)
		update(\.cameraType, s.camera_type)
		update(\.fingerPaint, s.finger_paint)
		if app_take_new_project_request() {
			newProjectShown = true
		}
		update(\.cameraRotation, SIMD4(s.camera_rot.0, s.camera_rot.1, s.camera_rot.2, s.camera_rot.3))
		LayersStore.shared.refresh(s)
		MaterialsStore.shared.refresh()
		StudioStore.shared.refresh()
		BrushesStore.shared.refresh()
		ProjectsStore.shared.refresh(s)
		CurveStore.shared.refresh(s)
	}


	// Shows the tool as selected at once, the engine takes it after the next frame (its material parse holds the main thread)
	func selectTool(_ id: Int32) {
		pendingTool = id
		tool        = id
		FrameDriver.shared.afterNextFrame { [self] in
			if pendingTool == id { // Not a later choice still waiting
				pendingTool = nil
			}
			perform { app_set_tool(id) }
		}
	}

	// Actions: the engine changes, refresh picks it up right away instead of on the next frame.
	// Between frames only (app_api.h): a frame's nested modal loop must not run them.
	// What the action returns (whether the engine took it) is not needed: refresh shows the outcome.
	func perform<T>(_ action: () -> T) {
		assert(!FrameDriver.shared.inTick, "app_* called inside a frame")
		_ = action()
		app_wake() // Many actions run on the engine's next frame, the engine may be idle
		refresh()
	}
}

// Assigns only a changed value. Not through inout: passing an @Observable property inout counts as a write even when
// the value stays, every view reading it redrew on every frame (and a segmented picker reset under the finger).
protocol ChangeOnly: AnyObject {}

extension ChangeOnly {
	func update<T: Equatable>(_ path: ReferenceWritableKeyPath<Self, T>, _ value: T) {
		if self[keyPath: path] != value {
			self[keyPath: path] = value
		}
	}
}
