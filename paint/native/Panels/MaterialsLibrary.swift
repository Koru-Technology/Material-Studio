import SwiftUI

// The Library: materials saved from the Material Studio, the materials folder and Material Maker's library, or the cloud
// catalogs. Searched, filtered by style and type; tapping one adds it to the project.
struct MaterialsLibraryView: View {
	var onUse: () -> Void = {}
	@Bindable private var store = MaterialsStore.shared

	var body: some View {
		VStack(spacing: 0) {
			header
			Divider()
			content
		}
		.onAppear {
			store.libraryShown = true
		}
		.onDisappear {
			store.libraryShown = false
		}
	}

	private var header: some View {
		VStack(alignment: .leading, spacing: 8) {
			HStack(spacing: 8) {
				HStack(spacing: 6) {
					Image(systemName: "magnifyingglass")
						.foregroundStyle(.secondary)
					TextField("Search", text: $store.librarySearch)
						.textFieldStyle(.plain)
						.autocorrectionDisabled()
#if os(iOS)
						.textInputAutocapitalization(.never)
#endif
					if !store.librarySearch.isEmpty {
						Button {
							store.librarySearch = ""
						} label: {
							Image(systemName: "xmark.circle.fill")
								.foregroundStyle(.tertiary)
						}
						.buttonStyle(.plain)
						.accessibilityLabel("Clear Search")
					}
				}
				.padding(.horizontal, 10)
				.padding(.vertical, 7)
				.background(.fill.tertiary, in: .capsule)

				Toggle(isOn: Binding(get: { store.libraryCloud }, set: { store.setCloud($0) })) {
					Label("Cloud Materials", systemImage: store.libraryCloud ? "cloud.fill" : "cloud")
				}
				.toggleStyle(.button)
				.labelStyle(.iconOnly)
				.help(store.libraryCloud ? "Show my materials" : "Show the cloud materials")
			}
			HStack(spacing: 8) {
				filterMenu
				Spacer(minLength: 0)
				if !store.libraryCloud && !store.libraryHasFolder {
					Button("Choose Folder…", systemImage: "folder.badge.plus") {
						app_library_pick_folder()
					}
					.font(.caption)
					.buttonStyle(.borderless)
				}
			}
			if !store.libraryStatus.isEmpty {
				Text(store.libraryStatus)
					.font(.caption)
					.foregroundStyle(.secondary)
					.lineLimit(2)
			}
		}
		.padding(.horizontal, 12)
		.padding(.bottom, 8)
	}

	private var filterMenu: some View {
		Menu {
			Picker("Type", selection: $store.libraryFilter) {
				ForEach(Array(store.libraryFilterOptions.enumerated()), id: \.offset) { i, group in
					if i == 0 {
						ForEach(group) { option in
							Text("\(option.title) (\(option.count))").tag(option.filter)
						}
					}
					else if let style = group.first {
						Section(style.title) {
							Text("All (\(style.count))").tag(style.filter)
							ForEach(group.dropFirst()) { option in
								Text("\(option.title) (\(option.count))").tag(option.filter)
							}
						}
					}
				}
			}
			.pickerStyle(.inline)
		} label: {
			HStack(spacing: 4) {
				Text(store.filterTitle(store.libraryFilter))
				Image(systemName: "chevron.up.chevron.down")
					.font(.caption2)
			}
			.font(.subheadline.weight(.medium))
		}
		.menuStyle(.button)
		.buttonStyle(.borderless)
		.fixedSize()
	}

	@ViewBuilder private var content: some View {
		if store.librarySections.isEmpty {
			Group {
				if !store.librarySearch.isEmpty {
					ContentUnavailableView.search(text: store.librarySearch)
				}
				else if store.libraryCloud {
					if store.libraryStatus.isEmpty {
						ContentUnavailableView("No Cloud Materials", systemImage: "cloud")
					}
					else {
						ProgressView()
					}
				}
				else {
					ContentUnavailableView("No Materials", systemImage: "books.vertical",
					                       description: Text("Choose a materials folder to fill the library."))
				}
			}
			.frame(maxWidth: .infinity, maxHeight: .infinity)
		}
		else {
			ScrollViewReader { proxy in
				ScrollView {
					LazyVGrid(columns: materialColumns, spacing: 12, pinnedViews: [.sectionHeaders]) {
						ForEach(store.librarySections) { section in
							Section {
								ForEach(section.items) { row in
									LibraryCell(row: row, onUse: onUse)
								}
							} header: {
								SectionHeader(title: section.title, count: section.items.count)
							}
						}
					}
					.padding(.horizontal, 12)
					.padding(.bottom, 12)
					.id("top")
				}
				.onChange(of: store.libraryFilter) {
					proxy.scrollTo("top", anchor: .top)
				}
				.onChange(of: store.libraryCloud) {
					proxy.scrollTo("top", anchor: .top)
				}
			}
		}
	}
}

private struct SectionHeader: View {
	let title: String
	let count: Int

	var body: some View {
		HStack {
			Text(title)
				.font(.footnote.weight(.semibold))
			Text("\(count)")
				.font(.footnote)
				.foregroundStyle(.secondary)
			Spacer()
		}
		.padding(.vertical, 6)
		.padding(.horizontal, 4)
		.frame(maxWidth: .infinity)
		.background(.bar)
	}
}

private struct LibraryCell: View {
	let row: LibraryRow
	let onUse: () -> Void
	private var store = MaterialsStore.shared

	init(row: LibraryRow, onUse: @escaping () -> Void) {
		self.row   = row
		self.onUse = onUse
	}

	var body: some View {
		let image = store.icons[row.key]
		MaterialTile(image: image, name: row.name, loading: image == nil && !store.iconsMissing.contains(row.key),
		             dimmed: row.busy) {
			if row.busy {
				DownloadRing(progress: store.libraryProgress[row.key] ?? 0)
			}
			else if row.downloaded {
				Image(systemName: "checkmark.circle.fill")
					.symbolRenderingMode(.palette)
					.foregroundStyle(.white, .green)
					.font(.body)
					.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
					.padding(4)
			}
		}
		.onTapGesture {
			use()
		}
		.contextMenu {
			Button {
				use()
			} label: {
				Label("Add to Project", systemImage: "plus.circle")
			}
			.disabled(row.busy)
			if !row.detail.isEmpty {
				Text(row.detail)
			}
			if row.canDelete {
				Divider()
				Button(role: .destructive) {
					store.delete(row)
				} label: {
					Label("Delete", systemImage: "trash")
				}
			}
		}
		.help(row.detail.isEmpty ? row.name : row.detail)
		.accessibilityAddTraits(.isButton)
		.onAppear {
			store.showIcon(row.key)
		}
		.onDisappear {
			store.hideIcon(row.key)
		}
	}

	private func use() {
		guard !row.busy else {
			return
		}
		store.use(row)
		if row.source != .cloud || row.downloaded {
			onUse()
		}
	}
}
