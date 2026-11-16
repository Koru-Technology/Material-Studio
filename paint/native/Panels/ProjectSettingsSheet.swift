import SwiftUI

// Project settings (app_api_export.h): the working resolution (with the tiled export the change keeps painted detail
// and is one undo step, running a layer a frame with its progress here) and the history saved in the project
struct ProjectSettingsSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var info     = app_resolution_t()
	@State private var history  = app_history_info_t()
	@State private var selected = Int32(2048)
	@State private var clearing = false

	private var sizes: [Int32] {
		[1024, 2048, 4096, 8192, 16384].filter { $0 <= info.max_size }
	}

	var body: some View {
		NavigationStack {
			Form {
				Section {
					Picker("Resolution", selection: $selected) {
						ForEach(sizes, id: \.self) { s in
							Text("\(s) × \(s)").tag(s)
						}
					}
					.disabled(info.running)
				} header: {
					Text("Resolution")
				} footer: {
					Text("Painted detail is kept: going back to a resolution brings the layers back as they were. Undo reverts the change.")
				}
				if info.running {
					Section("Changing resolution") {
						ProgressView(value: info.progress)
					}
				}
				Section {
					LabeledContent("Size", value: ByteCountFormatter.string(fromByteCount: history.ram_bytes + history.disk_bytes, countStyle: .file))
					Button("Clear History", role: .destructive) {
						clearing = true
					}
					Button("Save a Copy Without History…") {
						AppStore.shared.perform { app_history_save_copy() }
					}
				} header: {
					Text("History")
				} footer: {
					Text("The undo history is saved with the project. Clearing it keeps the layers and their detail for export.")
				}
			}
			.confirmationDialog("Clear the undo history?", isPresented: $clearing, titleVisibility: .visible) {
				Button("Clear History", role: .destructive) {
					AppStore.shared.perform { app_history_clear() }
					app_history_info(&history)
				}
			} message: {
				Text("Undo can not go back past this point afterwards.")
			}
			.formStyle(.grouped)
			.navigationTitle("Project Settings")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				ToolbarItem(placement: .cancellationAction) {
					Button(info.running ? "Hide" : "Cancel") {
						dismiss()
					}
				}
				ToolbarItem(placement: .confirmationAction) {
					Button("Apply") {
						AppStore.shared.perform { app_resolution_set(selected) }
						app_resolution_get(&info)
					}
					.disabled(info.running || (selected == info.width && selected == info.height))
				}
			}
		}
		.onAppear {
			app_resolution_get(&info)
			app_history_info(&history)
			selected = info.width
		}
		.task {
			while !Task.isCancelled { // Progress while the sheet is up
				try? await Task.sleep(for: .milliseconds(100))
				var r = app_resolution_t()
				app_resolution_get(&r)
				info = r
				app_history_info(&history)
			}
		}
	}
}
