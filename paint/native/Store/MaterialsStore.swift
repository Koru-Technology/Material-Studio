import CoreGraphics
import Foundation
import Observation

struct MaterialRow: Identifiable, Equatable {
	let id: Int32
	let previewVersion: UInt32
	let selected: Bool
	let previewReady: Bool
	let canOpenStudio: Bool
	let name: String
}

struct LibraryRow: Identifiable, Equatable {
	enum Source: Int32 {
		case saved = 0, folder = 1, materialMaker = 2, cloud = 3
	}

	var id: UInt32 { key }
	let index: Int32 // Valid until the library signature changes
	let key: UInt32
	let source: Source
	let section: Int32
	let sectionName: String
	let group: Int32 // -1: no type, no style
	let groupName: String
	let stylized: Bool
	let busy: Bool
	let downloaded: Bool
	let canDelete: Bool
	let name: String
	let detail: String
}

struct LibrarySection: Identifiable, Equatable {
	let id: Int32
	let title: String
	let items: [LibraryRow]
}

// The type menu of the library: everything, one style, or one type of a style
enum LibraryFilter: Hashable {
	case all
	case style(stylized: Bool)
	case group(stylized: Bool, group: Int32)
}

struct LibraryFilterOption: Identifiable, Hashable {
	var id: LibraryFilter { filter }
	let filter: LibraryFilter
	let title: String
	let count: Int
}

struct SmartRow: Identifiable, Equatable {
	var id: UInt32 { key }
	let index: Int32
	let key: UInt32
	let name: String
}

// Project materials, the Library and smart materials (app_api_materials.h), refreshed by AppStore after every frame.
// The Library and smart materials are only read while they show (libraryShown, smartShown): opening Material Maker's
// library or the cloud catalog costs.
@Observable
final class MaterialsStore {
	static let shared = MaterialsStore()

	// Project
	private(set) var ready                       = false
	private(set) var selectedID: Int32           = -1
	private(set) var materials: [MaterialRow]    = []
	private(set) var smartCanSave                = false
	private(set) var canApplyToFill              = false
	private(set) var studioOpen                  = false

	// Library
	private(set) var libraryCloud                = false
	private(set) var libraryStatus               = ""
	private(set) var libraryHasFolder            = true
	private(set) var library: [LibraryRow]       = []
	private(set) var librarySections: [LibrarySection] = []
	private(set) var libraryFilterOptions: [[LibraryFilterOption]] = [] // All, then one group per style
	private(set) var libraryProgress: [UInt32: Float] = [:]          // Cloud downloads by key
	var librarySearch = "" {
		didSet {
			if librarySearch != oldValue {
				updateSections()
			}
		}
	}
	var libraryFilter = LibraryFilter.all {
		didSet {
			if libraryFilter != oldValue {
				updateSections()
			}
		}
	}

	// Smart materials
	private(set) var smart: [SmartRow] = []

	// Icons of the library and smart materials by key, loaded a few per frame for the cells on screen
	private(set) var icons: [UInt32: CGImage] = [:]
	private(set) var iconsMissing: Set<UInt32> = [] // Items without an icon

	@ObservationIgnored var libraryShown = false {
		didSet {
			if libraryShown && !oldValue {
				refreshLibrary()
			}
		}
	}
	@ObservationIgnored var smartShown = false {
		didSet {
			if smartShown && !oldValue {
				refreshSmart()
			}
		}
	}

	@ObservationIgnored private var materialsSignature: UInt32 = 0
	@ObservationIgnored private var previewsVersion: UInt32    = 0
	@ObservationIgnored private var librarySignature: UInt32?
	@ObservationIgnored private var smartSignature: UInt32?
	@ObservationIgnored private var thumbnails: [Int32: (version: UInt32, image: CGImage)] = [:]
	@ObservationIgnored private var wanted: [UInt32: Int] = [:] // Keys of the cells on screen, how many cells show each
	@ObservationIgnored private var iconUse: [UInt32: Int] = [:]  // Last use, the least recently used go past maxIcons
	@ObservationIgnored private var iconClock = 0
	@ObservationIgnored private var libraryIndex: [UInt32: Int32] = [:]
	@ObservationIgnored private var smartIndex: [UInt32: Int32] = [:]
	@ObservationIgnored private var buffer = [UInt8](repeating: 0, count: 256 * 256 * 4)

	private let maxIcons      = 600
	private let iconsPerFrame = 8

	func refresh() {
		var s = app_materials_state_t()
		app_materials_get_state(&s)
		guard s.ready else {
			return
		}
		set(&ready, true)
		set(&selectedID, s.selected_id)
		set(&smartCanSave, s.smart_can_save)
		set(&canApplyToFill, s.can_apply_to_fill)
		set(&studioOpen, s.studio_open)
		if s.materials_signature != materialsSignature || s.previews_version != previewsVersion {
			materialsSignature = s.materials_signature
			previewsVersion    = s.previews_version
			set(&materials, readMaterials())
		}
		if libraryShown {
			refreshLibrary()
		}
		if smartShown {
			refreshSmart()
		}
		if libraryShown || smartShown {
			loadIcons()
		}
	}

	private func readMaterials() -> [MaterialRow] {
		var rows: [MaterialRow] = []
		var info = app_material_info_t()
		for i in 0..<app_material_count() where app_material_info(i, &info) {
			rows.append(MaterialRow(id: info.id, previewVersion: info.preview_version, selected: info.selected,
			                        previewReady: info.preview_ready, canOpenStudio: info.can_open_studio,
			                        name: String(cString: info.name)))
		}
		let ids = Set(rows.map(\.id))
		thumbnails = thumbnails.filter { ids.contains($0.key) }
		return rows
	}

	// The preview sphere, read back again once its version changed. The last one while a new one is not ready.
	func thumbnail(for row: MaterialRow) -> CGImage? {
		if let t = thumbnails[row.id], t.version == row.previewVersion {
			return t.image
		}
		var w: Int32 = 0
		var h: Int32 = 0
		let bytes = buffer.withUnsafeMutableBufferPointer {
			app_material_thumbnail(row.id, 256, $0.baseAddress, Int32($0.count), &w, &h)
		}
		guard bytes > 0, let image = makeImage(Array(buffer[0..<Int(bytes)]), width: Int(w), height: Int(h)) else {
			return thumbnails[row.id]?.image
		}
		thumbnails[row.id] = (row.previewVersion, image)
		return image
	}

	var selectedMaterial: MaterialRow? {
		materials.first { $0.id == selectedID }
	}

	// Library

	private func refreshLibrary() {
		let sig = app_library_signature()
		set(&libraryCloud, app_library_cloud())
		set(&libraryStatus, String(cString: app_library_status()))
		set(&libraryHasFolder, app_library_has_folder())
		if sig != librarySignature {
			librarySignature = sig
			readLibrary()
		}
		if libraryCloud {
			readProgress()
		}
	}

	private func readLibrary() {
		var rows: [LibraryRow] = []
		var item = app_library_item_t()
		let n = app_library_count()
		rows.reserveCapacity(Int(n))
		for i in 0..<n where app_library_item(i, &item) {
			rows.append(LibraryRow(index: i, key: item.key, source: LibraryRow.Source(rawValue: item.source) ?? .materialMaker,
			                       section: item.section, sectionName: String(cString: item.section_name),
			                       group: item.group, groupName: String(cString: item.group_name), stylized: item.stylized,
			                       busy: item.busy, downloaded: item.downloaded, canDelete: item.can_delete,
			                       name: String(cString: item.name), detail: String(cString: item.detail)))
		}
		libraryIndex = Dictionary(rows.map { ($0.key, $0.index) }, uniquingKeysWith: { a, _ in a })
		set(&library, rows)
		updateSections()
	}

	private func readProgress() {
		guard library.contains(where: \.busy) else {
			set(&libraryProgress, [:])
			return
		}
		var progress: [UInt32: Float] = [:]
		var item = app_library_item_t()
		for row in library where row.busy && app_library_item(row.index, &item) {
			progress[row.key] = item.progress
		}
		set(&libraryProgress, progress)
	}

	// Sections of the items matching the search and the type picked, and the type menu with its counts
	private func updateSections() {
		let search  = librarySearch.trimmingCharacters(in: .whitespaces)
		let matches = search.isEmpty ? library : library.filter { app_library_matches($0.index, search) }

		var styled: [Bool: Int] = [:]
		var groups: [LibraryFilter: (title: String, count: Int)] = [:]
		var groupOrder: [LibraryFilter] = []
		for row in matches where row.group >= 0 {
			styled[row.stylized, default: 0] += 1
			let g = LibraryFilter.group(stylized: row.stylized, group: row.group)
			if groups[g] == nil {
				groupOrder.append(g)
				groups[g] = (row.groupName, 0)
			}
			groups[g]!.count += 1
		}
		var options: [[LibraryFilterOption]] = [[LibraryFilterOption(filter: .all, title: "All Types", count: matches.count)]]
		for stylized in [true, false] {
			guard let n = styled[stylized], n > 0 else {
				continue
			}
			var group = [LibraryFilterOption(filter: .style(stylized: stylized), title: stylized ? "Stylized" : "Realistic", count: n)]
			for g in groupOrder {
				if case .group(let s, _) = g, s == stylized, let info = groups[g] {
					group.append(LibraryFilterOption(filter: g, title: info.title, count: info.count))
				}
			}
			options.append(group)
		}
		set(&libraryFilterOptions, options)

		// The type picked shows nothing now: all of them
		let filter = libraryFilter
		if !options.joined().contains(where: { $0.filter == filter }) {
			libraryFilter = .all // Updates again
			return
		}

		var sections: [LibrarySection] = []
		var current: [LibraryRow] = []
		var currentID: Int32 = -1
		var currentTitle = ""
		func flush() {
			if !current.isEmpty {
				sections.append(LibrarySection(id: currentID, title: currentTitle, items: current))
				current = []
			}
		}
		for row in matches {
			switch filter {
			case .all:
				break
			case .style(let stylized):
				if row.group < 0 || row.stylized != stylized {
					continue
				}
			case .group(let stylized, let group):
				if row.group != group || row.stylized != stylized || row.group < 0 {
					continue
				}
			}
			if row.section != currentID || current.isEmpty {
				flush()
				currentID    = row.section
				currentTitle = filter == .all ? row.sectionName : row.groupName
			}
			current.append(row)
		}
		flush()
		set(&librarySections, sections)
	}

	func filterTitle(_ filter: LibraryFilter) -> String {
		for group in libraryFilterOptions {
			for option in group where option.filter == filter {
				switch filter {
				case .group(let stylized, _):
					return "\(stylized ? "Stylized" : "Realistic") \(option.title)"
				default:
					return option.title
				}
			}
		}
		return "All Types"
	}

	func setCloud(_ on: Bool) {
		app_library_set_cloud(on)
		libraryFilter = .all
		refreshLibrary()
	}

	func use(_ row: LibraryRow) {
		AppStore.shared.perform { _ = app_library_use(row.index) }
	}

	func delete(_ row: LibraryRow) {
		AppStore.shared.perform { _ = app_library_delete(row.index) }
		refreshLibrary()
	}

	// Smart materials

	private func refreshSmart() {
		let sig = app_smart_signature()
		guard sig != smartSignature else {
			return
		}
		smartSignature = sig
		var rows: [SmartRow] = []
		var info = app_smart_info_t()
		for i in 0..<app_smart_count() where app_smart_info(i, &info) {
			rows.append(SmartRow(index: i, key: info.key, name: String(cString: info.name)))
		}
		smartIndex = Dictionary(rows.map { ($0.key, $0.index) }, uniquingKeysWith: { a, _ in a })
		set(&smart, rows)
	}

	func applySmart(_ row: SmartRow) {
		AppStore.shared.perform { _ = app_smart_apply(row.index) }
	}

	func deleteSmart(_ row: SmartRow) {
		AppStore.shared.perform { _ = app_smart_delete(row.index) }
		refreshSmart()
	}

	func saveSmart(name: String) {
		let name = name.trimmingCharacters(in: .whitespaces)
		guard !name.isEmpty else {
			return
		}
		AppStore.shared.perform { _ = app_smart_save(name) }
	}

	var smartDefaultName: String {
		String(cString: app_smart_default_name())
	}

	// Icons: the cells on screen ask for theirs, they load a few per frame

	func showIcon(_ key: UInt32) {
		wanted[key, default: 0] += 1
	}

	func hideIcon(_ key: UInt32) {
		if let n = wanted[key] {
			wanted[key] = n > 1 ? n - 1 : nil
		}
	}

	private func loadIcons() {
		var loaded: [UInt32: CGImage] = [:]
		var missing: [UInt32] = []
		var reads = 0
		for key in wanted.keys {
			iconClock += 1
			iconUse[key] = iconClock
			if icons[key] != nil || iconsMissing.contains(key) {
				continue
			}
			var w: Int32 = 0
			var h: Int32 = 0
			var bytes: Int32
			if let index = libraryIndex[key], libraryShown {
				bytes = buffer.withUnsafeMutableBufferPointer { app_library_icon(index, $0.baseAddress, Int32($0.count), &w, &h) }
			}
			else if let index = smartIndex[key], smartShown {
				bytes = buffer.withUnsafeMutableBufferPointer { app_smart_icon(index, $0.baseAddress, Int32($0.count), &w, &h) }
			}
			else {
				continue
			}
			if bytes < 0 {
				missing.append(key)
			}
			else if bytes > 0, let image = makeImage(Array(buffer[0..<Int(bytes)]), width: Int(w), height: Int(h)) {
				loaded[key] = image
				reads += 1
				if reads >= iconsPerFrame {
					break
				}
			}
		}
		if !missing.isEmpty {
			iconsMissing.formUnion(missing)
		}
		guard !loaded.isEmpty else {
			return
		}
		var all = icons
		all.merge(loaded) { _, new in new }
		if all.count > maxIcons { // The least recently shown go, not the ones on screen
			let drop = all.keys.filter { wanted[$0] == nil }.sorted { (iconUse[$0] ?? 0) < (iconUse[$1] ?? 0) }
			for key in drop.prefix(all.count - maxIcons) {
				all[key]     = nil
				iconUse[key] = nil
			}
		}
		icons = all
	}
}
