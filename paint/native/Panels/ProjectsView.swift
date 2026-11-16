import SwiftUI

#if os(iOS)
import UIKit

// iPad projects screen: the recent projects, five to a row with equal space between them and the edges.
// The header (title, new project button) floats over the grid, which scrolls under its gradient.
struct ProjectsView: View {
	private var store = ProjectsStore.shared
	@State private var deleting: ProjectRow?

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
						ForEach(store.projects) { project in
							ProjectTile(project: project, size: tile, labelWidth: tile + gap - 16, iconsVersion: store.iconsVersion) {
								AppStore.shared.perform { app_project_open(project.index) }
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
		.confirmationDialog("Delete \(deleting?.name ?? "")?", isPresented: $deleting.isPresent(), titleVisibility: .visible) {
			Button("Delete", role: .destructive) {
				if let project = deleting {
					AppStore.shared.perform { app_project_delete(project.index) }
				}
			}
		} message: {
			Text("The project and its files are deleted. This cannot be undone.")
		}
	}

	// Title and new project button, in line with the grid's first and last columns
	private func header(inset: CGFloat) -> some View {
		HStack {
			Text("Material Studio")
				.font(.largeTitle)
				.fontWeight(.semibold)
			Spacer()
			Button {
				AppStore.shared.perform { app_project_new() }
			} label: {
				Image(systemName: "plus")
					.font(.title3)
					.frame(width: 44, height: 44)
			}
			.glassEffect(.regular.interactive(), in: .circle)
			.accessibilityLabel("New Project")
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

// A project's preview, rounded with a drop shadow, its name under it. Tap opens it, the menu deletes it.
private struct ProjectTile: View {
	let project: ProjectRow
	let size: CGFloat
	let labelWidth: CGFloat
	let iconsVersion: Int
	let onOpen: () -> Void
	let onDelete: () -> Void

	@State private var image: UIImage?

	var body: some View {
		Button(action: onOpen) {
			VStack(spacing: 10) {
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
		.contextMenu {
			Button("Delete", systemImage: "trash", role: .destructive, action: onDelete)
		}
		.task(id: "\(project.iconPath)#\(iconsVersion)") {
			let path = project.iconPath
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
