import Foundation
import Observation

struct ProjectRow: Identifiable, Equatable {
	let id: String // Path of the .arm file
	let index: Int32
	let name: String
	let iconPath: String
	let entry: String // Its entry in the recent list (app_project_entry): the folders key projects by it
}

// The recent projects the iPad's projects screen shows (app_api.h), refreshed by AppStore after every frame
@Observable
final class ProjectsStore: ChangeOnly {
	static let shared = ProjectsStore()

	private(set) var shown = false
	private(set) var closable = false // Opened from a project: a close button goes back to it
	private(set) var projects: [ProjectRow] = []
	private(set) var iconsVersion = 0 // The previews are read again each time the screen opens: saves rewrite them

	// Folders of the projects screen, only there: the projects stay where they are on disk. Kept in the app's
	// defaults, a project by its recent list entry.
	private(set) var folders: [String] = []
	private(set) var folderOf: [String: String] = [:] // Entry: folder

	@ObservationIgnored private var signature: UInt32 = 0
	private static let foldersKey = "projects.folders"

	private struct Saved: Codable {
		var folders: [String]
		var members: [String: String]
	}

	private init() {
		if let data = UserDefaults.standard.data(forKey: Self.foldersKey), let saved = try? JSONDecoder().decode(Saved.self, from: data) {
			folders  = saved.folders
			folderOf = saved.members.filter { saved.folders.contains($0.value) }
		}
	}

	func refresh(_ s: app_state_t) {
		if s.projects_shown && !shown {
			iconsVersion += 1
		}
		update(\.shown, s.projects_shown)
		update(\.closable, s.projects_closable)
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
			                       iconPath: icon, entry: String(cString: app_project_entry(i))))
		}
		update(\.projects, rows)
	}

	// MARK: Folders

	func projects(in folder: String?) -> [ProjectRow] {
		projects.filter { folderOf[$0.entry] == folder }
	}

	func count(in folder: String) -> Int {
		projects.filter { folderOf[$0.entry] == folder }.count
	}

	// "Folder", or "Folder 2", "Folder 3"... when taken
	func freeFolderName(_ name: String) -> String {
		var free = name
		var i    = 2
		while folders.contains(free) {
			free = "\(name) \(i)"
			i += 1
		}
		return free
	}

	@discardableResult
	func newFolder(_ name: String) -> String {
		let folder = freeFolderName(name)
		folders.append(folder)
		save()
		return folder
	}

	func renameFolder(_ folder: String, to name: String) {
		guard let i = folders.firstIndex(of: folder), name != folder else {
			return
		}
		let renamed = freeFolderName(name)
		folders[i]  = renamed
		folderOf    = folderOf.mapValues { $0 == folder ? renamed : $0 }
		save()
	}

	// Its projects go back to the top level, they are not deleted
	func deleteFolder(_ folder: String) {
		folders.removeAll { $0 == folder }
		folderOf = folderOf.filter { $0.value != folder }
		save()
	}

	// nil: out of its folder
	func move(_ entry: String, to folder: String?) {
		guard folderOf[entry] != folder, projects.contains(where: { $0.entry == entry }) else { // Not text dropped from elsewhere
			return
		}
		folderOf[entry] = folder
		save()
	}

	// A project renamed (its entry changed) or deleted (to nil)
	func projectMoved(from entry: String, to new: String?) {
		guard let folder = folderOf.removeValue(forKey: entry) else {
			return
		}
		if let new {
			folderOf[new] = folder
		}
		save()
	}

	private func save() {
		if let data = try? JSONEncoder().encode(Saved(folders: folders, members: folderOf)) {
			UserDefaults.standard.set(data, forKey: Self.foldersKey)
		}
	}
}
