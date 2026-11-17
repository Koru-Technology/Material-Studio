import CoreGraphics
import Foundation
import Observation
import SwiftUI

// The Material Studio (app_api_studio.h): what its native editor shows, read again when the engine's signature changes

struct StudioLayer: Identifiable, Equatable {
	let id: Int32 // Its place, 0 the base at the bottom
	let name: String
	let subtitle: String
	let selected: Bool
}

struct StudioStop: Identifiable, Equatable {
	let id: Int32 // Its index in the ramp, kept while it is dragged past another stop
	let color: SIMD3<Float>
	let pos: Float
	let selected: Bool
}

struct StudioControl: Identifiable, Equatable {
	enum Kind: Int32 {
		case slider = 0, toggle = 1, choice = 2, button = 3, color = 4, gradient = 5
	}

	var id: UInt32 { key }
	let key: UInt32
	let kind: Kind
	let name: String
	let range: ClosedRange<Float>
	let whole: Bool
	let percent: Bool
	let value: Float
	let color: SIMD3<Float>
	let options: [String]
	let stops: [StudioStop]
}

struct StudioSection: Identifiable, Equatable {
	let id: Int32 // app_studio_section_t, 100 + the group of a Variable
	let name: String
	var controls: [StudioControl]
}

@Observable
final class StudioStore: ChangeOnly {
	static let shared = StudioStore()

	private(set) var open                  = false
	private(set) var materialID: Int32     = -1
	private(set) var variables             = false
	private(set) var name                  = ""
	private(set) var sphere                = false
	private(set) var presets: [String]     = []
	private(set) var layerPresets: [String] = []
	private(set) var layers: [StudioLayer] = [] // Top first, the base last
	private(set) var layerMax              = 6
	private(set) var patterns: [String]    = []
	private(set) var pattern: Int32        = 0
	private(set) var stops: [StudioStop]   = [] // By position
	private(set) var rampMax               = 8
	private(set) var rampPlain             = false
	private(set) var rampHard              = false
	private(set) var sections: [StudioSection] = []
	private(set) var layerTiles: [Int32: CGImage] = [:]
	private(set) var patternThumbs: [CGImage?] = []

	@ObservationIgnored private var signature: UInt32    = 0
	@ObservationIgnored private var tilesVersion: UInt32 = 0
	@ObservationIgnored private var tilesReadTime        = TimeInterval(0)
	@ObservationIgnored private let reader                = PixelReader(capacity: 128 * 128 * 4)
	@ObservationIgnored private var canvasOrigin          = CGPoint.zero // The engine canvas in global points
	@ObservationIgnored private var previewFrame          = CGRect.zero  // The preview's place in global points
	@ObservationIgnored private var scale: CGFloat        = 1

	var selectedLayer: StudioLayer? {
		layers.first { $0.selected }
	}

	func refresh() {
		var s = app_studio_state_t()
		app_studio_get_state(&s)
		update(\.open, s.open)
		guard s.open else {
			signature    = 0
			tilesVersion = 0
			return
		}
		update(\.materialID, s.material_id)
		update(\.variables, s.variables)
		update(\.sphere, s.sphere)
		update(\.name, String(cString: s.name))
		if s.signature != signature {
			signature = s.signature
			read()
		}
		// Tiles redraw with every value a slider drag sets: read back at most a few times a second, the last one always
		let now = Date.timeIntervalSinceReferenceDate
		if s.tiles_version != tilesVersion && now - tilesReadTime > 0.2 {
			tilesVersion  = s.tiles_version
			tilesReadTime = now
			readTiles()
		}
		if !variables && (patternThumbs.count != patterns.count || patternThumbs.contains(nil)) {
			readPatternThumbs()
		}
	}

	private func read() {
		if presets.isEmpty {
			presets      = (0..<app_studio_preset_count()).map { String(cString: app_studio_preset_name($0)) }
			layerPresets = (0..<app_studio_layer_preset_count()).map { String(cString: app_studio_layer_preset_name($0)) }
			patterns     = (0..<app_studio_pattern_count()).map { String(cString: app_studio_pattern_name($0)) }
			layerMax     = Int(app_studio_layer_max())
			rampMax      = Int(app_studio_ramp_max())
		}
		var info = app_studio_layer_t()
		update(\.layers, (0..<app_studio_layer_count()).reversed().compactMap { k in
			app_studio_layer(k, &info)
				? StudioLayer(id: k, name: String(cString: info.name), subtitle: String(cString: info.subtitle), selected: info.selected)
				: nil
		})
		update(\.pattern, app_studio_pattern_current())
		var stop = app_studio_stop_t()
		update(\.stops, (0..<app_studio_ramp_count()).compactMap { i in
			app_studio_ramp_stop(i, &stop)
				? StudioStop(id: stop.index, color: SIMD3(stop.r, stop.g, stop.b), pos: stop.pos, selected: stop.selected)
				: nil
		})
		update(\.rampPlain, app_studio_ramp_plain())
		update(\.rampHard, app_studio_ramp_hard())
		update(\.sections, readSections())
	}

	private func readSections() -> [StudioSection] {
		var out: [StudioSection] = []
		var c = app_studio_control_t()
		for i in 0..<app_studio_control_count() where app_studio_control(i, &c) {
			guard let kind = StudioControl.Kind(rawValue: c.kind) else {
				continue
			}
			var stops: [StudioStop] = []
			if kind == .gradient {
				var v: [Float] = [0, 0, 0, 0]
				for s in 0..<c.stops where app_studio_control_stop(c.key, s, &v) {
					stops.append(StudioStop(id: s, color: SIMD3(v[0], v[1], v[2]), pos: v[3], selected: false))
				}
			}
			let options = String(cString: c.options)
			let control = StudioControl(key: c.key, kind: kind, name: String(cString: c.name),
			                            range: c.min...max(c.max, c.min + 0.0001), whole: c.step == 1, percent: c.percent,
			                            value: c.value, color: SIMD3(c.color.0, c.color.1, c.color.2),
			                            options: options.isEmpty ? [] : options.components(separatedBy: "\n"),
			                            stops: stops)
			if out.last?.id == c.section {
				out[out.count - 1].controls.append(control)
			}
			else {
				out.append(StudioSection(id: c.section, name: String(cString: c.section_name), controls: [control]))
			}
		}
		return out
	}

	private func readTiles() {
		var tiles: [Int32: CGImage] = [:]
		for layer in layers {
			if let image = reader.read(premultiplied: true, { app_studio_layer_thumbnail(layer.id, 128, $0, $1, &$2, &$3) }).image {
				tiles[layer.id] = image
			}
		}
		layerTiles = tiles
	}

	private func readPatternThumbs() {
		var thumbs = patternThumbs.count == patterns.count ? patternThumbs : Array(repeating: nil, count: patterns.count)
		for i in thumbs.indices where thumbs[i] == nil {
			thumbs[i] = reader.read(premultiplied: true, { app_studio_pattern_thumbnail(Int32(i), 128, $0, $1, &$2, &$3) }).image
			if thumbs[i] == nil {
				break // Not made yet, the studio makes them once it shows
			}
		}
		patternThumbs = thumbs
	}

	func section(_ id: Int32) -> StudioSection? {
		sections.first { $0.id == id }
	}

	// The preview's place: the engine draws it there, in its canvas pixels

	func canvasMoved(_ frame: CGRect) {
		canvasOrigin = frame.origin
		sendPreviewRect()
	}

	func previewMoved(_ frame: CGRect, scale: CGFloat) {
		previewFrame = frame
		self.scale   = scale
		sendPreviewRect()
	}

	private func sendPreviewRect() {
		let r = previewFrame.offsetBy(dx: -canvasOrigin.x, dy: -canvasOrigin.y)
		app_studio_set_preview_rect(Float(r.minX * scale), Float(r.minY * scale), Float(r.width * scale), Float(r.height * scale))
	}
}

// Colors from the system picker have no gesture end: an edit starts with the first change and ends once the color
// stays a moment, one undo step
@MainActor
enum StudioColorEdit {
	private static var endTask: Task<Void, Never>?

	static func set(_ apply: @escaping () -> Void) {
		if let task = endTask {
			task.cancel()
		}
		else {
			AppStore.shared.perform { app_studio_edit_begin() }
		}
		AppStore.shared.perform(apply)
		endTask = Task { @MainActor in
			try? await Task.sleep(for: .milliseconds(400))
			if !Task.isCancelled {
				endTask = nil
				AppStore.shared.perform { app_studio_edit_end() }
			}
		}
	}
}

extension SIMD3 where Scalar == Float {
	var color: Color {
		Color(.sRGB, red: Double(x), green: Double(y), blue: Double(z))
	}
}

extension Color {
	// Components in sRGB, as the nodes keep them
	var srgb: SIMD3<Float>? {
		guard let space = CGColorSpace(name: CGColorSpace.sRGB),
		      let c = resolve(in: EnvironmentValues()).cgColor.converted(to: space, intent: .defaultIntent, options: nil),
		      let p = c.components, p.count >= 3 else {
			return nil
		}
		return SIMD3(Float(p[0]), Float(p[1]), Float(p[2]))
	}
}
