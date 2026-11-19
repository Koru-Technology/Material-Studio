import SwiftUI
import UniformTypeIdentifiers

// Bake Mesh Maps (app_api_bake.h): the maps to bake, an optional high poly file (read into the ray tracer only, never
// imported into the scene) with a preview of where its rays miss, the map settings, and the project's mesh maps.
struct BakeMeshMapsSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var picking  = false
	@State private var loading  = false
	@State private var failed   = false
	@State private var showHigh = false

	private static let sizes: [Int32] = [1024, 2048, 4096, 8192]
	private static let meshExtensions = ["obj", "fbx", "gltf", "glb"]

	var body: some View {
		NavigationStack {
			Form {
				if bake.state.running {
					Section("Baking") {
						VStack(alignment: .leading, spacing: 6) {
							Text(String(cString: app_bake_map_name(bake.state.map)))
							ProgressView(value: bake.state.progress)
						}
						Button("Cancel Bake", role: .destructive) {
							AppStore.shared.perform { app_bake_cancel() }
						}
					}
				}
				Section("Maps") {
					ForEach(0 ..< Int32(APP_BAKE_MAP_COUNT.rawValue), id: \.self) { map in
						Toggle(String(cString: app_bake_map_name(map)), isOn: mapBinding(map))
					}
				}
				.disabled(bake.state.running)
				highPolySection
					.disabled(bake.state.running)
				Section("Settings") {
					Picker("Size", selection: binding(\.size)) {
						ForEach(Self.sizes, id: \.self) { s in
							Text("\(s) × \(s)").tag(s)
						}
					}
					if baking(APP_BAKE_MAP_AO) || baking(APP_BAKE_MAP_THICKNESS) {
						Stepper(value: binding(\.rays), in: 4 ... 512, step: 4) {
							LabeledContent("Rays", value: "\(bake.options.rays)")
						}
					}
				}
				.disabled(bake.state.running)
				if baking(APP_BAKE_MAP_AO) {
					Section("Ambient Occlusion") {
						slider("Strength", \.ao_strength, 0 ... 2)
						slider("Radius", \.ao_radius, 0 ... 2)
						slider("Offset", \.ao_offset, 0 ... 2)
					}
					.disabled(bake.state.running)
				}
				if baking(APP_BAKE_MAP_CURVATURE) {
					Section("Curvature") {
						slider("Strength", \.curvature_strength, 0 ... 2)
						slider("Radius", \.curvature_radius, 0 ... 2)
						slider("Offset", \.curvature_offset, -2 ... 2)
						Stepper(value: binding(\.curvature_smooth), in: 0 ... 5) {
							LabeledContent("Smooth", value: "\(bake.options.curvature_smooth)")
						}
					}
					.disabled(bake.state.running)
				}
				if baking(APP_BAKE_MAP_CAVITY) {
					Section("Cavity") {
						slider("Detail Size", \.cavity_size, 1 ... 16)
						slider("Contrast", \.cavity_contrast, 0.5 ... 6)
					}
					.disabled(bake.state.running)
				}
				meshMapsSection
				if bake.state.has_baked {
					Section {
						Button("Add Baked Layer", systemImage: "square.3.layers.3d") {
							AppStore.shared.perform { _ = app_bake_add_layer() }
							dismiss()
						}
					}
				}
			}
			.formStyle(.grouped)
			.navigationTitle("Bake Mesh Maps")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				ToolbarItem(placement: .cancellationAction) {
					Button(bake.state.running ? "Hide" : "Close") {
						dismiss()
					}
				}
				ToolbarItem(placement: .confirmationAction) {
					Button("Bake") {
						AppStore.shared.perform { _ = app_bake_start() }
						refresh()
					}
					.disabled(bake.state.running || bake.options.maps == 0 || loading)
				}
			}
			.fileImporter(isPresented: $picking, allowedContentTypes: [.data]) { result in
				if case let .success(url) = result {
					loadHigh(url)
				}
			}
		}
#if os(macOS)
		.frame(minWidth: 520, minHeight: 640)
#endif
		.onAppear {
			bake.load()
		}
		.onDisappear {
			AppStore.shared.perform { app_bake_close() }
		}
		.task {
			while !Task.isCancelled {
				try? await Task.sleep(for: .milliseconds(100))
				refresh()
			}
		}
	}

	private var bake: BakeStore {
		.shared
	}

	// MARK: High poly

	@ViewBuilder private var highPolySection: some View {
		Section("High Poly") {
			if bake.state.high_triangles > 0 {
				LabeledContent(bake.highName, value: "\(bake.state.high_triangles.formatted()) triangles")
				if bake.state.high_fit == 0 {
					warning("Does not overlap the mesh")
				}
				else if bake.state.high_fit < 0.5 || bake.state.high_fit > 2 {
					warning("Size differs from the mesh")
				}
				previewView
				Picker("Show", selection: $showHigh) {
					Text("Low Poly").tag(false)
					Text("High Poly").tag(true)
				}
				.pickerStyle(.segmented)
				.onChange(of: showHigh) { renderPreview() }
				LabeledContent("Missed Rays", value: bake.state.missed.formatted(.percent.precision(.fractionLength(1))))
				slider("Max Distance", \.max_distance, 0.001 ... 0.2, format: { String(format: "%.3f", $0) }) {
					renderPreview()
				}
				Toggle("Average Normals", isOn: binding(\.average_normals))
				Button("Remove High Poly", systemImage: "xmark.circle", role: .destructive) {
					AppStore.shared.perform { _ = app_bake_set_high("") }
					refresh()
				}
			}
			else if loading {
				HStack {
					Text("Loading…")
					Spacer()
					ProgressView()
				}
			}
			else {
				Button("Choose High Poly…", systemImage: "cube") {
					failed  = false
					picking = true
					FrameDriver.shared.pickerWillShow()
				}
				if failed {
					warning("Could not read the mesh")
				}
			}
		}
	}

	private var previewView: some View {
		ZStack {
			Color.black.opacity(0.85)
			if let image = bake.image {
				Image(decorative: image, scale: 1)
					.resizable()
					.interpolation(.medium)
					.aspectRatio(contentMode: .fit)
			}
		}
		.aspectRatio(CGFloat(BakeStore.previewWidth) / CGFloat(BakeStore.previewHeight), contentMode: .fit)
		.clipShape(.rect(cornerRadius: 10))
		.contentShape(.rect)
		.gesture(
			DragGesture(minimumDistance: 1)
				.onChanged { g in
					bake.orbit(g.translation)
					renderPreview(fast: true)
				}
				.onEnded { _ in
					bake.endGesture()
					renderPreview()
				}
				.simultaneously(with: MagnifyGesture()
					.onChanged { g in
						bake.magnify(Float(g.magnification))
						renderPreview(fast: true)
					}
					.onEnded { _ in
						bake.endGesture()
						renderPreview()
					})
		)
		.listRowInsets(EdgeInsets(top: 8, leading: 8, bottom: 8, trailing: 8))
		.onAppear { renderPreview() }
	}

	private func renderPreview(fast: Bool = false) {
		bake.render(high: showHigh, fast: fast)
	}

	// The picked file is copied, then read into the ray tracer and the copy deleted: the high poly never joins the
	// project or its scene
	private func loadHigh(_ url: URL) {
		guard Self.meshExtensions.contains(url.pathExtension.lowercased()) else {
			failed = true
			return
		}
		loading = true
		Task {
			let copy = await Task.detached(priority: .userInitiated) { Self.copyHigh(url) }.value
			guard let copy else {
				loading = false
				failed  = true
				return
			}
			try? await Task.sleep(for: .milliseconds(50)) // The spinner shows before the engine reads the file
			var count = Int32(0)
			AppStore.shared.perform { count = app_bake_set_high(copy.path) }
			try? FileManager.default.removeItem(at: copy.deletingLastPathComponent())
			loading = false
			failed  = count == 0
			showHigh = false
			refresh()
			renderPreview()
		}
	}

	nonisolated private static func copyHigh(_ url: URL) -> URL? {
		let scoped = url.startAccessingSecurityScopedResource()
		defer {
			if scoped {
				url.stopAccessingSecurityScopedResource()
			}
		}
		let dir = FileManager.default.temporaryDirectory.appending(path: "HighPoly-\(UUID().uuidString)")
		let dst = dir.appending(path: url.lastPathComponent)
		var ok  = false
		do {
			try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
		}
		catch {
			return nil
		}
		// Coordinated: a file of a provider (iCloud, a share) is downloaded first
		var error: NSError?
		NSFileCoordinator().coordinate(readingItemAt: url, options: [], error: &error) { src in
			ok = (try? FileManager.default.copyItem(at: src, to: dst)) != nil
		}
		if !ok {
			try? FileManager.default.removeItem(at: dir)
			return nil
		}
		return dst
	}

	// MARK: Mesh maps

	@ViewBuilder private var meshMapsSection: some View {
		Section {
			ForEach(bake.meshMaps.indices, id: \.self) { type in
				Picker(String(cString: app_mesh_map_name(Int32(type))), selection: Binding(get: { bake.meshMaps[type] }, set: { texture in
					AppStore.shared.perform { _ = app_mesh_map_set(Int32(type), texture) }
					refresh()
				})) {
					Text("None").tag(Int32(-1))
					ForEach(bake.textures.indices, id: \.self) { i in
						Text(bake.textures[i]).tag(Int32(i))
					}
				}
			}
		} header: {
			Text("Mesh Maps")
		} footer: {
			if bake.state.stale {
				warning("The mesh changed since these were baked")
			}
		}
		.disabled(bake.state.running)
	}

	// MARK: Helpers

	private func refresh() {
		bake.refresh()
	}

	private func baking(_ map: app_bake_map_t) -> Bool {
		bake.options.maps & (1 << map.rawValue) != 0
	}

	private func warning(_ text: String) -> some View {
		Label(text, systemImage: "exclamationmark.triangle.fill")
			.font(.subheadline)
			.foregroundStyle(.orange)
	}

	private func set(_ o: app_bake_options_t) {
		AppStore.shared.perform { bake.set(o) }
	}

	private func binding<T>(_ path: WritableKeyPath<app_bake_options_t, T>) -> Binding<T> {
		Binding {
			bake.options[keyPath: path]
		} set: { value in
			var o = bake.options
			o[keyPath: path] = value
			set(o)
		}
	}

	private func mapBinding(_ map: Int32) -> Binding<Bool> {
		Binding {
			bake.options.maps & (1 << UInt32(map)) != 0
		} set: { on in
			var o = bake.options
			o.maps = on ? o.maps | (1 << UInt32(map)) : o.maps & ~(1 << UInt32(map))
			set(o)
		}
	}

	private func slider(_ title: String, _ path: WritableKeyPath<app_bake_options_t, Float>, _ range: ClosedRange<Float>,
	                    format: @escaping (Float) -> String = { String(format: "%.2f", $0) }, changed: @escaping () -> Void = {}) -> some View {
		LayerSlider(title, value: bake.options[keyPath: path], range: range, format: format, begin: {}, set: { v in
			var o = bake.options
			o[keyPath: path] = v
			bake.set(o)
			changed()
		}, end: {})
	}
}

// The sheet's engine state (app_api_bake.h). Kept out of the view: SwiftUI's comparison of a view holding these C
// structs among its other state crashed (EXC_BAD_ACCESS in AG::LayoutDescriptor::compare).
@Observable
final class BakeStore: ChangeOnly {
	static let shared        = BakeStore()
	static let previewWidth  = 480
	static let previewHeight = 360

	private(set) var options  = app_bake_options_t()
	private(set) var state    = app_bake_state_t()
	private(set) var highName = ""
	private(set) var textures = [String]()
	private(set) var meshMaps = [Int32]() // Texture index per mesh map kind, -1 for none
	private(set) var image: CGImage? // The high poly preview

	@ObservationIgnored private var yaw   = Float(0.8)
	@ObservationIgnored private var pitch = Float(0.35)
	@ObservationIgnored private var zoom  = Float(1.3)
	@ObservationIgnored private var from: SIMD3<Float>? // Yaw, pitch and zoom as the gesture began
	@ObservationIgnored private let reader = PixelReader(capacity: BakeStore.previewWidth * BakeStore.previewHeight * 4)

	func load() {
		app_bake_get(&options)
		refresh()
	}

	func set(_ o: app_bake_options_t) {
		var o = o
		app_bake_set(&o)
		app_bake_get(&options)
	}

	func refresh() {
		var s = app_bake_state_t()
		app_bake_state(&s)
		state = s
		update(\.highName, String(cString: app_bake_high_name()))
		update(\.textures, (0 ..< app_texture_count()).map { String(cString: app_texture_name($0)) })
		update(\.meshMaps, (0 ..< app_mesh_map_count()).map { app_mesh_map_texture($0) })
	}

	// The preview turned by a drag, zoomed by a pinch

	func orbit(_ translation: CGSize) {
		let f = begin()
		yaw   = f.x - Float(translation.width) * 0.01
		pitch = min(1.5, max(-1.5, f.y + Float(translation.height) * 0.01))
	}

	func magnify(_ magnification: Float) {
		zoom = min(5, max(0.2, begin().z / magnification))
	}

	func endGesture() {
		from = nil
	}

	private func begin() -> SIMD3<Float> {
		let f = from ?? SIMD3(yaw, pitch, zoom)
		from  = f
		return f
	}

	func render(high: Bool, fast: Bool) {
		let (w, h) = (Int32(Self.previewWidth), Int32(Self.previewHeight))
		let (yaw, pitch, zoom) = (self.yaw, self.pitch, self.zoom)
		if let image = reader.read(premultiplied: false, { buffer, capacity, pw, ph in
			app_bake_preview(yaw, pitch, zoom, high, fast, w, h, buffer, capacity, &pw, &ph)
		}).image {
			self.image = image
		}
	}
}
