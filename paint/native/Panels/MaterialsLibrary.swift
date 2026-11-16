import SwiftUI

// The Library: materials saved from the Material Studio, the materials folder and Material Maker's library.
// Searched, filtered by style and type; tapping one adds it to the project.
struct MaterialsLibraryView: View {
	var onUse: () -> Void = {}
	@Bindable private var store = MaterialsStore.shared
	@State private var searchFocused = false

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
#if os(iOS)
					PlainTextField(placeholder: "Search", text: $store.librarySearch, focused: $searchFocused)
#else
					TextField("Search", text: $store.librarySearch)
						.textFieldStyle(.plain)
						.autocorrectionDisabled()
#endif
					if !store.librarySearch.isEmpty || searchFocused {
						// Clears and puts the keyboard away. Its target is as tall as the field and wider than the symbol.
						Button {
							store.librarySearch = ""
							searchFocused       = false
						} label: {
							Image(systemName: "xmark.circle.fill")
								.foregroundStyle(.tertiary)
								.frame(width: 36, height: 36)
								.contentShape(.rect)
						}
						.buttonStyle(.plain)
						.padding(.vertical, -7)
						.padding(.trailing, -10)
						.accessibilityLabel("Clear Search")
					}
				}
				.padding(.horizontal, 10)
				.padding(.vertical, 7)
				.background(.fill.tertiary, in: .capsule)
			}
			HStack(spacing: 8) {
				filterMenu
				Spacer(minLength: 0)
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
		// Buttons in titled sections: a Picker inside the menu loses the Stylized and Realistic headings on iOS
		Menu {
			ForEach(Array(store.libraryFilterOptions.enumerated()), id: \.offset) { i, group in
				if i == 0 {
					ForEach(group) { option in
						filterButton(option.filter, "\(option.title) (\(option.count))")
					}
				}
				else if let style = group.first {
					Section(style.title) {
						filterButton(style.filter, "All \(style.title) (\(style.count))")
						ForEach(group.dropFirst()) { option in
							filterButton(option.filter, "\(option.title) (\(option.count))")
						}
					}
				}
			}
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

	private func filterButton(_ filter: LibraryFilter, _ title: String) -> some View {
		Button {
			store.libraryFilter = filter
		} label: {
			if store.libraryFilter == filter {
				Label(title, systemImage: "checkmark")
			}
			else {
				Text(title)
			}
		}
	}

	@ViewBuilder private var content: some View {
		if store.librarySections.isEmpty {
			Group {
				if !store.librarySearch.isEmpty {
					ContentUnavailableView.search(text: store.librarySearch)
				}
				else {
					ContentUnavailableView("No Materials", systemImage: "books.vertical")
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
		MaterialTile(image: image, name: row.name, loading: image == nil && !store.iconsMissing.contains(row.key))
		.onTapGesture {
			use()
		}
		.contextMenu {
			Button {
				use()
			} label: {
				Label("Add to Project", systemImage: "plus.circle")
			}
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
		store.use(row)
		onUse()
	}
}
