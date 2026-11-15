import SwiftUI

// The brush library: a section per group, each brush with its stroke. Tap paints with it, the menu of a brush edits it in the
// Brush Creator (engine drawn), duplicates, renames, moves or deletes it. Groups of your own are renamed and deleted from
// their header, built-in groups and brushes can only be duplicated from.
struct BrushesPopover: View {
	private var store   = AppStore.shared
	private var brushes = BrushesStore.shared
	@Environment(\.dismiss) private var dismiss

	@State private var naming: Naming?
	@State private var name = ""
	@State private var deleting: Deleting?

	enum Naming: Identifiable {
		case newGroup
		case renameGroup(BrushGroupRow)
		case renameBrush(BrushRow)

		var id: String {
			switch self {
			case .newGroup: "new"
			case .renameGroup(let g): "g\(g.id)"
			case .renameBrush(let b): "b\(b.id)"
			}
		}
	}

	enum Deleting: Identifiable {
		case group(BrushGroupRow)
		case brush(BrushRow)

		var id: String {
			switch self {
			case .group(let g): "g\(g.id)"
			case .brush(let b): "b\(b.id)"
			}
		}

		var title: String {
			switch self {
			case .group(let g): "Delete \u{201C}\(g.name)\u{201D} and its brushes?"
			case .brush(let b): "Delete \u{201C}\(b.name)\u{201D}?"
			}
		}
	}

	var body: some View {
		NavigationStack {
			ScrollViewReader { proxy in
				List {
					let recent = brushes.recentIDs.compactMap { brushes.brush($0) }
					if recent.count > 1 {
						Section("Recent") {
							ForEach(recent) { brush in
								row(brush)
									.id("recent\(brush.id)")
							}
						}
					}
					ForEach(brushes.listedGroups) { group in
						Section {
							ForEach(brushes.brushes(in: group.id)) { brush in
								row(brush)
									.id(brush.id)
							}
						} header: {
							groupHeader(group)
						}
					}
				}
				.onAppear {
					if let selected = brushes.selectedBrush {
						app_brush_previews_request(selected.groupID)
						proxy.scrollTo(selected.id, anchor: .center)
					}
				}
			}
			.navigationTitle("Brushes")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				ToolbarItem(placement: .primaryAction) {
					Menu {
						Button {
							newBrush()
						} label: {
							Label("New Brush", systemImage: "paintbrush.pointed")
						}
						Button {
							name = "New Group"
							naming = .newGroup
						} label: {
							Label("New Group", systemImage: "folder.badge.plus")
						}
					} label: {
						Label("New", systemImage: "plus")
					}
				}
			}
			.alert(namingTitle, isPresented: Binding(get: { naming != nil }, set: { if !$0 { naming = nil } }), presenting: naming) { what in
				TextField("Name", text: $name)
				Button("Cancel", role: .cancel) {}
				Button("OK") {
					commit(what)
				}
				.disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
			}
			.alert(deleting?.title ?? "", isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } }),
			       presenting: deleting) { what in
				Button("Cancel", role: .cancel) {}
				Button("Delete", role: .destructive) {
					store.perform {
						switch what {
						case .group(let g): _ = app_brush_group_delete(g.id)
						case .brush(let b): _ = app_brush_delete(b.id)
						}
					}
				}
			}
		}
		.frame(width: 380)
		.frame(minHeight: 360, idealHeight: 620)
#if os(iOS)
		.presentationCompactAdaptation(.popover)
#endif
	}

	private func row(_ brush: BrushRow) -> some View {
		BrushRowView(brush: brush, preview: brushes.preview(for: brush))
			.contentShape(.rect)
			.onTapGesture {
				store.perform { _ = app_brush_use(brush.id) }
			}
			.listRowBackground(brush.selected ? Color.accentColor.opacity(0.18) : nil)
			.contextMenu {
				brushMenu(brush)
			}
	}

	@ViewBuilder
	private func brushMenu(_ brush: BrushRow) -> some View {
		if !brush.builtin {
			Button {
				edit(brush)
			} label: {
				Label("Edit", systemImage: "slider.horizontal.3")
			}
			Button {
				name = brush.name
				naming = .renameBrush(brush)
			} label: {
				Label("Rename", systemImage: "pencil")
			}
		}
		Button {
			store.perform { _ = app_brush_duplicate(brush.id) }
		} label: {
			Label("Duplicate", systemImage: "plus.square.on.square")
		}
		let targets = brushes.userGroups.filter { $0.id != brush.groupID }
		if !brush.builtin && !targets.isEmpty {
			Menu {
				ForEach(targets) { group in
					Button(group.name) {
						store.perform { _ = app_brush_move(brush.id, group.id) }
					}
				}
			} label: {
				Label("Move to Group", systemImage: "folder")
			}
		}
		if !brush.builtin {
			Divider()
			Button(role: .destructive) {
				deleting = .brush(brush)
			} label: {
				Label("Delete", systemImage: "trash")
			}
		}
	}

	private func groupHeader(_ group: BrushGroupRow) -> some View {
		HStack {
			Text(group.name)
			Spacer()
			if !group.builtin {
				Menu {
					Button {
						newBrush(in: group.id)
					} label: {
						Label("New Brush", systemImage: "paintbrush.pointed")
					}
					Button {
						name = group.name
						naming = .renameGroup(group)
					} label: {
						Label("Rename", systemImage: "pencil")
					}
					Button(role: .destructive) {
						deleting = .group(group)
					} label: {
						Label("Delete", systemImage: "trash")
					}
				} label: {
					Image(systemName: "ellipsis.circle")
				}
				.menuStyle(.button)
				.buttonStyle(.borderless)
				.fixedSize()
			}
		}
	}

	private var namingTitle: String {
		switch naming {
		case .newGroup: "New Group"
		case .renameGroup: "Rename Group"
		case .renameBrush: "Rename Brush"
		case nil: ""
		}
	}

	private func commit(_ what: Naming) {
		let text = name.trimmingCharacters(in: .whitespaces)
		guard !text.isEmpty else {
			return
		}
		store.perform {
			switch what {
			case .newGroup: _ = app_brush_group_add(text)
			case .renameGroup(let g): _ = app_brush_group_rename(g.id, text)
			case .renameBrush(let b): _ = app_brush_rename(b.id, text)
			}
		}
	}

	// The Brush Creator is drawn by the engine over the canvas: the popover makes way for it
	private func newBrush(in group: Int32? = nil) {
		let target = group ?? brushes.selectedBrush?.groupID ?? -1
		store.perform { app_brush_new(target) }
		dismiss()
	}

	private func edit(_ brush: BrushRow) {
		store.perform { _ = app_brush_edit(brush.id) }
		dismiss()
	}
}

private struct BrushRowView: View {
	let brush: BrushRow
	let preview: CGImage?

	var body: some View {
		VStack(alignment: .leading, spacing: 2) {
			HStack {
				Text(brush.name)
					.font(.subheadline)
					.fontWeight(brush.selected ? .semibold : .regular)
					.lineLimit(1)
				Spacer()
				if brush.selected {
					Image(systemName: "checkmark")
						.font(.caption.bold())
						.foregroundStyle(.tint)
				}
			}
			BrushStrokeView(image: preview)
				.frame(height: 40)
		}
		.padding(.vertical, 2)
		.accessibilityElement(children: .combine)
		.accessibilityAddTraits(brush.selected ? [.isButton, .isSelected] : .isButton)
	}
}

// The stroke strip, its middle band filling the row as the old brush list draws it; tinted with the text color
struct BrushStrokeView: View {
	let image: CGImage?

	var body: some View {
		GeometryReader { geo in
			if let image {
				Image(decorative: image, scale: 1)
					.renderingMode(.template)
					.resizable()
					.interpolation(.medium)
					.scaledToFill()
					.frame(width: geo.size.width, height: geo.size.height)
					.clipped()
					.foregroundStyle(.primary)
			}
			else {
				RoundedRectangle(cornerRadius: 4)
					.fill(.quaternary)
					.overlay {
						ProgressView()
							.controlSize(.small)
					}
			}
		}
	}
}
