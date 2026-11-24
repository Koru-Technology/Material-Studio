import SwiftUI

#if os(iOS)
import UIKit

// iPad projects screen: the recent projects, five to a row with equal space between them and the edges.
// The header (title, new project button) floats over the grid, which scrolls under its gradient.
// Folders come first: drag a project onto one to put it in, open one to see its projects. Inside, the back button
// takes a project dropped on it out of the folder, as does its menu.
struct ProjectsView: View {
	private var store = ProjectsStore.shared
	@State private var deleting: ProjectRow?
	@State private var folder: String? // The folder shown, nil the top level
	@State private var backTargeted = false

	private static let columns = 5
	private static let tile: CGFloat = 160
	private static let headerHeight: CGFloat = 96

	var body: some View {
		GeometryReader { geo in
			let n    = CGFloat(Self.columns)
			let tile = min(Self.tile, (geo.size.width - 24 * (n + 1)) / n) // Smaller in a narrow window
			let gap  = (geo.size.width - tile * n) / (n + 1)
			ZStack(alignment: .top) {
				ScrollView {
					LazyVGrid(columns: Array(repeating: GridItem(.fixed(tile), spacing: gap, alignment: .top), count: Self.columns),
					          spacing: 24) {
						if folder == nil {
							ForEach(store.folders, id: \.self) { f in
								FolderTile(name: f, count: store.count(in: f), recent: Array(store.projects(in: f).prefix(3)), iconsVersion: store.iconsVersion,
								           size: tile, labelWidth: tile + gap - 16) {
									withAnimation(.easeOut(duration: 0.15)) {
										folder = f
									}
								}
							}
						}
						ForEach(store.projects(in: folder)) { project in
							ProjectTile(project: project, folder: folder, size: tile, labelWidth: tile + gap - 16, iconsVersion: store.iconsVersion) {
								AppStore.shared.perform { app_project_open(project.entry) }
							} onDelete: {
								deleting = project
							}
						}
					}
					.padding(.horizontal, gap)
					.padding(.top, Self.headerHeight + 8)
					.padding(.bottom, gap)
				}
				header(inset: gap)
			}
		}
		.background(Color(white: 26 / 255)) // The 3D view's background in the editor
		.environment(\.colorScheme, .dark)
		.onChange(of: store.folders) { _, folders in
			if let f = folder, !folders.contains(f) { // Renamed or deleted
				folder = nil
			}
		}
		.confirmationDialog("Delete \(deleting?.name ?? "")?", isPresented: $deleting.isPresent(), titleVisibility: .visible) {
			Button("Delete", role: .destructive) {
				if let project = deleting {
					AppStore.shared.perform { app_project_delete(project.entry) }
					store.projectMoved(from: project.entry, to: nil)
				}
			}
		} message: {
			Text("The project and its files are deleted. This cannot be undone.")
		}
	}

	// Close or back, the title, then new folder, import and new project, in line with the grid's first and last columns
	private func header(inset: CGFloat) -> some View {
		HStack(spacing: 16) {
			if folder != nil {
				Button {
					withAnimation(.easeOut(duration: 0.15)) {
						folder = nil
					}
				} label: {
					Image(systemName: "chevron.left")
						.font(.title3)
						.frame(width: 44, height: 44)
				}
				.glassEffect(backTargeted ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .circle)
				.dropDestination(for: String.self) { entries, _ in
					for entry in entries {
						store.move(entry, to: nil)
					}
					return !entries.isEmpty
				} isTargeted: {
					backTargeted = $0
				}
				.accessibilityLabel("Back")
			}
			else if store.closable {
				Button {
					AppStore.shared.perform { app_projects_close() }
				} label: {
					Image(systemName: "xmark")
						.font(.title3)
						.frame(width: 44, height: 44)
				}
				.glassEffect(.regular.interactive(), in: .circle)
				.keyboardShortcut(.cancelAction)
				.accessibilityLabel("Close")
			}
			Text(folder ?? "Material Studio")
				.font(.largeTitle)
				.fontWeight(.semibold)
				.lineLimit(1)
			Spacer()
			GlassEffectContainer(spacing: 8) {
				HStack(spacing: 8) {
					if folder == nil {
						Button {
							RootAlert.shared.text("New Folder", action: "Create", initial: store.freeFolderName("Folder")) { name in
								store.newFolder(name)
							}
						} label: {
							Image(systemName: "folder.badge.plus")
								.font(.title3)
								.frame(width: 44, height: 44)
						}
						.glassEffect(.regular.interactive(), in: .circle)
						.accessibilityLabel("New Folder")
					}
					Button {
						AppStore.shared.perform { app_project_import() }
					} label: {
						Label("Import", systemImage: "square.and.arrow.down")
							.font(.body.weight(.medium))
							.padding(.horizontal, 16)
							.frame(height: 44)
					}
					.glassEffect(.regular.interactive(), in: .capsule)
					.accessibilityHint("Starts a new project on a model")
					Button {
						AppStore.shared.newProjectShown = true
					} label: {
						Image(systemName: "plus")
							.font(.title3)
							.frame(width: 44, height: 44)
					}
					.glassEffect(.regular.interactive(), in: .circle)
					.accessibilityLabel("New Project")
				}
			}
		}
		.foregroundStyle(.primary)
		.padding(.horizontal, inset)
		.frame(height: Self.headerHeight)
		.frame(maxWidth: .infinity)
		.background(alignment: .top) {
			LinearGradient(colors: [.black.opacity(0.3), .black.opacity(0)], startPoint: .top, endPoint: .bottom)
				.ignoresSafeArea(edges: .top)
				.allowsHitTesting(false)
		}
	}
}

// A folder: tap opens it, a project dropped on it goes in, the menu renames or deletes it (not its projects)
private struct FolderTile: View {
	let name: String
	let count: Int
	let recent: [ProjectRow] // Its most recently opened projects, the most recent first: their previews stack
	let iconsVersion: Int
	let size: CGFloat
	let labelWidth: CGFloat
	let onOpen: () -> Void

	@State private var targeted = false
	private var store = ProjectsStore.shared

	// Each card's turn and shift, the most recent (front) first
	private static let angles: [Double]   = [-4, 9, -14]
	private static let shifts: [CGFloat]  = [0, 6, -6]

	init(name: String, count: Int, recent: [ProjectRow], iconsVersion: Int, size: CGFloat, labelWidth: CGFloat, onOpen: @escaping () -> Void) {
		self.name         = name
		self.count        = count
		self.recent       = recent
		self.iconsVersion = iconsVersion
		self.size         = size
		self.labelWidth   = labelWidth
		self.onOpen       = onOpen
	}

	var body: some View {
		Button(action: onOpen) {
			VStack(spacing: 10) {
				ZStack {
					if recent.isEmpty {
						ZStack {
							Color(white: targeted ? 0.3 : 0.2)
							Image(systemName: "folder.fill")
								.font(.system(size: size * 0.36))
								.foregroundStyle(targeted ? Color.accentColor : Color.white.opacity(0.55))
						}
						.frame(width: size, height: size)
						.clipShape(.rect(cornerRadius: 14, style: .continuous))
						.shadow(color: .black.opacity(0.4), radius: 10, y: 5)
					}
					else {
						ForEach(Array(recent.enumerated().reversed()), id: \.element.id) { i, project in // The most recent on top
							ProjectPreview(path: project.iconPath, iconsVersion: iconsVersion, size: size * 0.74)
								.clipShape(.rect(cornerRadius: 12, style: .continuous))
								.overlay {
									RoundedRectangle(cornerRadius: 12, style: .continuous)
										.strokeBorder(.white.opacity(0.15), lineWidth: 1)
								}
								.shadow(color: .black.opacity(0.35), radius: 6, y: 3)
								.rotationEffect(.degrees(Self.angles[i]))
								.offset(x: Self.shifts[i] * size / 160)
						}
					}
				}
				.frame(width: size, height: size)
				.overlay {
					RoundedRectangle(cornerRadius: 14, style: .continuous)
						.strokeBorder(Color.accentColor, lineWidth: targeted ? 3 : 0)
				}
				VStack(spacing: 2) {
					Text(name)
						.font(.body)
						.lineLimit(1)
						.truncationMode(.tail)
					Text(count == 1 ? "1 project" : "\(count) projects")
						.font(.footnote)
						.foregroundStyle(.secondary)
				}
				.frame(width: labelWidth)
			}
			.frame(width: size)
			.contentShape(.rect)
		}
		.buttonStyle(ProjectTileStyle())
		.dropDestination(for: String.self) { entries, _ in
			for entry in entries {
				store.move(entry, to: name)
			}
			return !entries.isEmpty
		} isTargeted: {
			targeted = $0
		}
		.contextMenu {
			Button("Rename", systemImage: "pencil") {
				RootAlert.shared.text("Rename Folder", initial: name) { new in
					store.renameFolder(name, to: new)
				}
			}
			Button("Delete Folder", systemImage: "folder.badge.minus", role: .destructive) {
				store.deleteFolder(name)
			}
		}
	}
}

// A project's preview, rounded with a drop shadow, its name under it. Tap opens it, drag puts it in a folder, the menu
// renames it, moves it between folders or deletes it.
private struct ProjectTile: View {
	let project: ProjectRow
	let folder: String? // The folder it is shown in
	let size: CGFloat
	let labelWidth: CGFloat
	let iconsVersion: Int
	let onOpen: () -> Void
	let onDelete: () -> Void

	@State private var image: UIImage?
	private var store = ProjectsStore.shared

	init(project: ProjectRow, folder: String?, size: CGFloat, labelWidth: CGFloat, iconsVersion: Int, onOpen: @escaping () -> Void,
	     onDelete: @escaping () -> Void) {
		self.project      = project
		self.folder       = folder
		self.size         = size
		self.labelWidth   = labelWidth
		self.iconsVersion = iconsVersion
		self.onOpen       = onOpen
		self.onDelete     = onDelete
	}

	var body: some View {
		Button(action: onOpen) {
			VStack(spacing: 10) {
				preview
					.shadow(color: .black.opacity(0.4), radius: 10, y: 5)
				Text(project.name)
					.font(.body)
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(width: labelWidth)
			}
			.frame(width: size)
			.contentShape(.rect)
		}
		.buttonStyle(ProjectTileStyle())
		.draggable(project.entry) {
			preview
				.frame(width: size * 0.6, height: size * 0.6)
		}
		.contextMenu {
			Button("Rename", systemImage: "pencil") {
				rename()
			}
			Button("Duplicate", systemImage: "plus.square.on.square") {
				duplicate()
			}
			if !store.folders.isEmpty {
				Menu("Move to Folder", systemImage: "folder") {
					ForEach(store.folders.filter { $0 != folder }, id: \.self) { f in
						Button(f) {
							store.move(project.entry, to: f)
						}
					}
				}
			}
			if folder != nil {
				Button("Remove from Folder", systemImage: "folder.badge.minus") {
					store.move(project.entry, to: nil)
				}
			}
			Divider()
			Button("Delete", systemImage: "trash", role: .destructive, action: onDelete)
		}
		.task(id: "\(project.iconPath)#\(iconsVersion)") {
			let path = project.iconPath
			image = await Task.detached(priority: .userInitiated) { UIImage(contentsOfFile: path) }.value
		}
	}

	private var preview: some View {
		ZStack {
			Color(white: 0.353) // The grey behind the model in the previews
			if let image {
				Image(uiImage: image)
					.resizable()
					.interpolation(.high)
					.scaledToFill()
			}
			else {
				Image(systemName: "cube")
					.font(.system(size: size * 0.3, weight: .light))
					.foregroundStyle(.white.opacity(0.35))
			}
		}
		.frame(width: size, height: size)
		.clipShape(.rect(cornerRadius: 14, style: .continuous))
	}

	// The copy goes in the same folder
	private func duplicate() {
		let row    = project
		let folder = self.folder
		var entry  = ""
		AppStore.shared.perform {
			entry = String(cString: app_project_duplicate(row.entry))
		}
		if !entry.isEmpty, let folder {
			store.move(entry, to: folder)
		}
	}

	private func rename() {
		let row = project
		RootAlert.shared.text("Rename Project", initial: row.name) { name in
			AppStore.shared.perform {
				let entry = String(cString: app_project_rename(row.entry, name))
				if !entry.isEmpty {
					ProjectsStore.shared.projectMoved(from: row.entry, to: entry)
				}
			}
		}
	}
}

// A project's preview image, read off the main thread, the grey of the previews' background behind it
private struct ProjectPreview: View {
	let path: String
	let iconsVersion: Int
	let size: CGFloat

	@State private var image: UIImage?

	var body: some View {
		ZStack {
			Color(white: 0.353)
			if let image {
				Image(uiImage: image)
					.resizable()
					.interpolation(.high)
					.scaledToFill()
			}
			else {
				Image(systemName: "cube")
					.font(.system(size: size * 0.3, weight: .light))
					.foregroundStyle(.white.opacity(0.35))
			}
		}
		.frame(width: size, height: size)
		.task(id: "\(path)#\(iconsVersion)") {
			let path = path
			image = await Task.detached(priority: .userInitiated) { UIImage(contentsOfFile: path) }.value
		}
	}
}

// Pressed: a little smaller and dimmer
private struct ProjectTileStyle: ButtonStyle {
	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.scaleEffect(configuration.isPressed ? 0.96 : 1)
			.opacity(configuration.isPressed ? 0.8 : 1)
			.animation(.easeOut(duration: 0.12), value: configuration.isPressed)
	}
}
#endif
