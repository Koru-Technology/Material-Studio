import SwiftUI

// Texture export (app_api_export.h): the export's resolution apart from the project's, preset, layers, format and
// padding; the layers it will scale up from the project's resolution; progress and Cancel while it runs.
struct ExportSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var options     = app_export_options_t()
	@State private var presets     = [String]()
	@State private var notUpscaled = [String]()
	@State private var progress    = Float(1)
	@State private var running     = false

	private var sizes: [Int32] {
		[2048, 4096, 8192, 16384].filter { $0 <= options.max_size }
	}

	private var formats: [(Int32, String)] {
		var f = [(Int32(APP_EXPORT_FORMAT_PNG.rawValue), "PNG"), (Int32(APP_EXPORT_FORMAT_JPG.rawValue), "JPG")]
#if os(macOS)
		f += [(Int32(APP_EXPORT_FORMAT_EXR16.rawValue), "EXR 16 bit"), (Int32(APP_EXPORT_FORMAT_EXR32.rawValue), "EXR 32 bit")]
#endif
		return f
	}

	var body: some View {
		NavigationStack {
			Form {
				Section("Resolution") {
					Picker("Size", selection: binding(\.size)) {
						Text("Project, \(options.working_width) × \(options.working_height)").tag(Int32(0))
						ForEach(sizes, id: \.self) { s in
							Text("\(s) × \(s)").tag(s)
						}
					}
				}
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
					Picker("Format", selection: binding(\.format)) {
						ForEach(formats, id: \.0) { f in
							Text(f.1).tag(f.0)
						}
					}
					if options.format == Int32(APP_EXPORT_FORMAT_JPG.rawValue) {
						LabeledContent("Quality") {
							Slider(value: binding(\.quality), in: 0 ... 100)
						}
					}
					Toggle("Padding", isOn: binding(\.padding))
				}
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
					} footer: {
						Text("Their strokes can't be painted again at a higher resolution yet: they are scaled up.")
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
