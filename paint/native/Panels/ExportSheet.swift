import SwiftUI

// Texture export (app_api_export.h): the export's resolution (the last picked, the project's at first), preset and
// layers, as PNG (padding hidden for now); the layers it will scale up from the project's resolution; progress and
// Cancel while it runs.
struct ExportSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var options     = app_export_options_t()
	@State private var presets     = [String]()
	@State private var notUpscaled = [String]()
	@State private var progress    = Float(1)
	@State private var running     = false

	// The project's size (tag 0) in order among the others
	private var sizes: [(Int32, String)] {
		let w = options.working_width, h = options.working_height
		var s: [(Int32, String)] = ([256, 512, 1024, 2048, 4096, 8192, 16384] as [Int32])
			.filter { $0 <= options.max_size && !(w == h && $0 == w) }
			.map { ($0, "\($0) × \($0)") }
		let i = s.firstIndex { $0.0 > w } ?? s.count
		s.insert((0, "\(w) × \(h) (Project)"), at: i)
		return s
	}

	// A size picked earlier that is the project's now shows as the project's entry
	private var sizeBinding: Binding<Int32> {
		let b = binding(\.size)
		return Binding {
			let w = options.working_width
			return w == options.working_height && b.wrappedValue == w ? 0 : b.wrappedValue
		} set: {
			b.wrappedValue = $0
		}
	}

	// Hidden for now, the option stays as it is
	private let showPadding = false

	var body: some View {
		NavigationStack {
			Form {
				Section("Resolution") {
					Picker("Size", selection: sizeBinding) {
						ForEach(sizes, id: \.0) { s in
							Text(s.1).tag(s.0)
						}
					}
				}
				.disabled(running) // The export in progress keeps the settings it started with
				Section("Textures") {
					Picker("Preset", selection: binding(\.preset)) {
						ForEach(presets.indices, id: \.self) { i in
							Text(presets[i]).tag(Int32(i))
						}
					}
					Picker("Layers", selection: binding(\.layers)) {
						Text("Visible").tag(Int32(APP_EXPORT_LAYERS_VISIBLE.rawValue))
						Text("Selected").tag(Int32(APP_EXPORT_LAYERS_SELECTED.rawValue))
						Text("Per Object").tag(Int32(APP_EXPORT_LAYERS_PER_OBJECT.rawValue))
						Text("Per UDIM Tile").tag(Int32(APP_EXPORT_LAYERS_PER_UDIM.rawValue))
					}
					if showPadding {
						Toggle("Padding", isOn: binding(\.padding))
					}
				}
				.disabled(running)
				if !notUpscaled.isEmpty {
					Section {
						VStack(alignment: .leading, spacing: 4) {
							Label("\(notUpscaled.count) \(notUpscaled.count == 1 ? "layer keeps" : "layers keep") \(options.working_width) × \(options.working_height) detail",
							      systemImage: "exclamationmark.triangle.fill")
								.font(.subheadline)
								.foregroundStyle(.orange)
							Text(notUpscaled.joined(separator: ", "))
								.font(.caption)
								.foregroundStyle(.secondary)
								.lineLimit(3)
						}
					}
				}
				if running {
					Section("Exporting") {
						ProgressView(value: progress)
						Button("Cancel Export", role: .destructive) {
							AppStore.shared.perform { app_export_cancel() }
							running = false
						}
					}
				}
			}
			.formStyle(.grouped)
			.navigationTitle("Export Textures")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				ToolbarItem(placement: .cancellationAction) {
					Button(running ? "Hide" : "Cancel") {
						dismiss()
					}
				}
				ToolbarItem(placement: .confirmationAction) {
					Button("Export") {
						AppStore.shared.perform { app_export_start() }
					}
					.disabled(running)
				}
			}
		}
		.onAppear(perform: load)
		.task {
			while !Task.isCancelled { // Progress while the sheet is up
				try? await Task.sleep(for: .milliseconds(100))
				var o = app_export_options_t()
				app_export_get(&o)
				running  = o.running
				progress = app_export_progress()
			}
		}
	}

	private func load() {
		app_export_get(&options)
		if options.format != Int32(APP_EXPORT_FORMAT_PNG.rawValue) { // PNG only
			options.format = Int32(APP_EXPORT_FORMAT_PNG.rawValue)
			var o = options
			AppStore.shared.perform { app_export_set(&o) }
		}
		presets = (0 ..< options.preset_count).map { String(cString: app_export_preset_name($0)) }
		running = options.running
		readNotUpscaled()
	}

	private func readNotUpscaled() {
		let n = app_export_not_upscaled_count()
		notUpscaled = (0 ..< n).map { String(cString: app_export_not_upscaled_name($0)) }
	}

	private func binding<T>(_ path: WritableKeyPath<app_export_options_t, T>) -> Binding<T> {
		Binding {
			options[keyPath: path]
		} set: { value in
			options[keyPath: path] = value
			var o = options
			AppStore.shared.perform { app_export_set(&o) }
			app_export_get(&options)
			readNotUpscaled()
		}
	}
}
