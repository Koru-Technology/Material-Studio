import Foundation
import Observation

struct ProjectRow: Identifiable, Equatable {
	let id: String // Path of the .arm file
	let index: Int32
	let name: String
	let iconPath: String
}

// The recent projects the iPad's projects screen shows (app_api.h), refreshed by AppStore after every frame
@Observable
final class ProjectsStore: ChangeOnly {
	static let shared = ProjectsStore()

	private(set) var shown = false
	private(set) var projects: [ProjectRow] = []
	private(set) var iconsVersion = 0 // The previews are read again each time the screen opens: saves rewrite them

	@ObservationIgnored private var signature: UInt32 = 0

	func refresh(_ s: app_state_t) {
		if s.projects_shown && !shown {
			iconsVersion += 1
		}
		update(\.shown, s.projects_shown)
		if s.projects_signature != signature {
			signature = s.projects_signature
			read()
		}
	}

	private func read() {
		var rows: [ProjectRow] = []
		for i in 0..<app_project_count() {
			let path = String(cString: app_project_path(i))
			let icon = String(cString: app_project_icon_path(i))
			rows.append(ProjectRow(id: path, index: i, name: URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent,
			                       iconPath: icon))
		}
		update(\.projects, rows)
	}
}
