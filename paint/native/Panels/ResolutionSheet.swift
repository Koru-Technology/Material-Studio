import SwiftUI

// The project's working resolution (app_api_export.h, app_resolution_*): with the tiled export the change keeps painted
// detail and is one undo step, running a layer a frame with its progress here
struct ResolutionSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var info     = app_resolution_t()
	@State private var selected = Int32(2048)

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
				} footer: {
					Text(info.keeps_detail
					     ? "Painted detail is kept: going back to a resolution brings the layers back as they were. Undo reverts the change."
					     : "Layers are resampled to the new resolution.")
				}
				if info.running {
					Section("Changing resolution") {
						ProgressView(value: info.progress)
					}
				}
			}
			.formStyle(.grouped)
			.navigationTitle("Project Resolution")
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
			selected = info.width
		}
		.task {
			while !Task.isCancelled { // Progress while the sheet is up
				try? await Task.sleep(for: .milliseconds(100))
				var r = app_resolution_t()
				app_resolution_get(&r)
				info = r
			}
		}
	}
}
