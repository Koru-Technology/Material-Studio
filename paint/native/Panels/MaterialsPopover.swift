import SwiftUI

// Project materials, Library and Smart materials: the toolbar's materials popover on iPad (about 380 points wide), a panel
// on the Mac
struct MaterialsPopover: View {
	enum Segment: String, CaseIterable, Identifiable {
		case project, library, smart

		var id: Self { self }

		var title: String {
			switch self {
			case .project: "Project"
			case .library: "Library"
			case .smart: "Smart"
			}
		}
	}

	@State private var segment = Segment(rawValue: UserDefaults.standard.string(forKey: "materials.segment") ?? "") ?? .project

	var body: some View {
		NavigationStack {
			VStack(spacing: 0) {
				Picker("Materials", selection: Binding(get: { segment }, set: { s in
					segment = s
					UserDefaults.standard.set(s.rawValue, forKey: "materials.segment")
				})) {
					ForEach(Segment.allCases) { s in
						Text(s.title).tag(s)
					}
				}
				.pickerStyle(.segmented)
				.labelsHidden()
				.padding(.horizontal, 12)
				.padding(.top, 4)
				.padding(.bottom, 8)

				switch segment {
				case .project:
					MaterialsProjectView()
				case .library:
					MaterialsLibraryView {
						segment = .project // The new material shows selected there
					}
				case .smart:
					MaterialsSmartView()
				}
			}
			.navigationTitle("Materials")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				if segment == .project {
					ToolbarItem(placement: .primaryAction) {
						Button {
							AppStore.shared.perform { _ = app_material_new() }
						} label: {
							Label("New Material", systemImage: "plus")
						}
					}
				}
			}
		}
#if os(iOS)
		.frame(width: 380, height: 560)
#else
		.frame(minWidth: 300, idealWidth: 380, minHeight: 420, idealHeight: 560)
#endif
	}
}

// After an action that opens an editor over the canvas: the iPad popover closes. Not on the Mac, where dismiss would
// close the window the panel is in.
func closePopover(_ dismiss: DismissAction) {
#if os(iOS)
	dismiss()
#endif
}

// The project's materials: tap to paint with one, the context menu edits, duplicates and deletes
struct MaterialsProjectView: View {
	@Environment(\.dismiss) private var dismiss
	private var store = MaterialsStore.shared
	@State private var renaming: MaterialRow?
	@State private var newName = ""

	var body: some View {
		VStack(spacing: 0) {
			header
			Divider()
			ScrollViewReader { proxy in
				ScrollView {
					LazyVGrid(columns: materialColumns, spacing: 12) {
						ForEach(store.materials) { m in
							MaterialTile(image: store.thumbnail(for: m), name: m.name, selected: m.selected, loading: !m.previewReady)
								.id(m.id)
								.onTapGesture {
									select(m)
								}
								.contextMenu {
									menu(for: m)
								}
								.help(m.name)
								.accessibilityAddTraits(m.selected ? [.isSelected, .isButton] : .isButton)
						}
					}
					.padding(12)
				}
				.onAppear {
					proxy.scrollTo(store.selectedID, anchor: .center)
				}
				.onChange(of: store.selectedID) { _, id in
					withAnimation {
						proxy.scrollTo(id, anchor: .center)
					}
				}
			}
		}
		.alert("Rename Material", isPresented: $renaming.isPresent()) {
			TextField("Name", text: $newName)
			Button("Rename") {
				if let m = renaming {
					AppStore.shared.perform { app_material_rename(m.id, newName) }
				}
				renaming = nil
			}
			Button("Cancel", role: .cancel) {
				renaming = nil
			}
		}
	}

	private var header: some View {
		HStack(spacing: 8) {
			Text(store.materials.count == 1 ? "1 Material" : "\(store.materials.count) Materials")
				.font(.subheadline.weight(.semibold))
				.foregroundStyle(.secondary)
			Spacer()
			if let m = store.selectedMaterial {
				Button {
					edit(m)
				} label: {
					Label("Edit", systemImage: "slider.horizontal.3")
				}
				.help("Edit the selected material")
			}
			Menu {
				Button(role: .destructive) {
					AppStore.shared.perform { app_material_delete_unused() }
				} label: {
					Label("Remove Unused Materials", systemImage: "trash.slash")
				}
				.disabled(store.materials.count < 2)
			} label: {
				Label("More", systemImage: "ellipsis.circle")
			}
			.menuIndicator(.hidden)
			.fixedSize()
		}
		.labelStyle(.iconOnly)
		.buttonStyle(.borderless)
		.font(.body)
		.padding(.horizontal, 14)
		.padding(.vertical, 6)
	}

	@ViewBuilder private func menu(for m: MaterialRow) -> some View {
		Button {
			edit(m)
		} label: {
			Label("Edit", systemImage: "slider.horizontal.3")
		}
		Divider()
		Button {
			AppStore.shared.perform { app_material_to_fill_layer(m.id) }
		} label: {
			Label("New Fill Layer", systemImage: "square.stack.3d.up.fill")
		}
		if store.canApplyToFill {
			Button {
				AppStore.shared.perform { app_material_apply_to_fill(m.id) }
			} label: {
				Label("Apply to Fill Layer", systemImage: "square.fill.on.square")
			}
		}
		Divider()
		Button {
			newName  = m.name
			renaming = m
		} label: {
			Label("Rename…", systemImage: "pencil")
		}
		Button {
			AppStore.shared.perform { app_material_duplicate(m.id) }
		} label: {
			Label("Duplicate", systemImage: "plus.square.on.square")
		}
		Button(role: .destructive) {
			AppStore.shared.perform { app_material_delete(m.id) }
		} label: {
			Label("Remove", systemImage: "trash")
		}
		.disabled(!app_material_can_delete(m.id))
	}

	private func select(_ m: MaterialRow) {
		guard !m.selected else {
			return
		}
		AppStore.shared.perform { app_material_select(m.id) }
	}

	// The Material Studio when it opens the material, the node editor otherwise
	private func edit(_ m: MaterialRow) {
		AppStore.shared.perform { app_material_edit(m.id) }
		closePopover(dismiss)
	}
}
