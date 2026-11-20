import SwiftUI

// Smart materials: fill layers with their masks, or groups, saved to reuse. Tapping one puts it on top of the layers.
struct MaterialsSmartView: View {
	@Environment(\.dismiss) private var dismiss
	private var store = MaterialsStore.shared

	var body: some View {
		VStack(spacing: 0) {
			HStack {
				Text(store.smart.count == 1 ? "1 Smart Material" : "\(store.smart.count) Smart Materials")
					.font(.subheadline.weight(.semibold))
					.foregroundStyle(.secondary)
				Spacer()
				Button {
					RootAlert.shared.text("Save Smart Material", action: "Save", initial: store.smartDefaultName,
					                      message: "The selected fill layer or group, with its masks.") { name in
						store.saveSmart(name: name)
					}
				} label: {
					Label("Save Layer", systemImage: "square.and.arrow.down")
				}
				.disabled(!store.smartCanSave)
				.help("Saves the selected fill layer or group with its masks")
				.buttonStyle(.borderless)
			}
			.padding(.horizontal, 14)
			.padding(.vertical, 6)
			Divider()
			if store.smart.isEmpty {
				ContentUnavailableView("No Smart Materials", systemImage: "square.stack.3d.up",
				                       description: Text("Select a fill layer or group, then Save Layer."))
					.frame(maxWidth: .infinity, maxHeight: .infinity)
			}
			else {
				ScrollView {
					LazyVGrid(columns: materialColumns, spacing: 12) {
						ForEach(store.smart) { row in
							let image = store.icons[row.key]
							MaterialTile(image: image, name: row.name, loading: image == nil && !store.iconsMissing.contains(row.key))
								.onTapGesture {
									apply(row)
								}
								.contextMenu {
									Button {
										apply(row)
									} label: {
										Label("Add to Layers", systemImage: "square.stack.3d.up.badge.automatic")
									}
									Divider()
									Button(role: .destructive) {
										store.deleteSmart(row)
									} label: {
										Label("Delete", systemImage: "trash")
									}
								}
								.help("\(row.name): add to the layers")
								.accessibilityAddTraits(.isButton)
								.onAppear {
									store.showIcon(row.key)
								}
								.onDisappear {
									store.hideIcon(row.key)
								}
						}
					}
					.padding(12)
				}
			}
		}
		.onAppear {
			store.smartShown = true
		}
		.onDisappear {
			store.smartShown = false
		}
	}

	private func apply(_ row: SmartRow) {
		store.applySmart(row)
		closePopover(dismiss)
	}
}
