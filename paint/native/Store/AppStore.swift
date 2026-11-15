import CoreGraphics
import Foundation
import Observation

struct LayerRow: Identifiable, Equatable {
	enum Kind: Int32 {
		case layer = 0, group = 1, mask = 2, filter = 3
	}

	let id: Int32
	let parentID: Int32
	let depth: Int
	let kind: Kind
	let visible: Bool
	let selected: Bool
	let isFill: Bool
	let opacity: Float
	let name: String
}

// The app state the native UI shows, read from the engine after every frame (app_api.h).
// Fields are assigned only when they changed, so SwiftUI redraws only what changed.
@Observable
final class AppStore {
	static let shared = AppStore()

	private(set) var ready         = false
	private(set) var tool: Int32   = 0
	private(set) var brushRadius   = Float(0.5)
	private(set) var brushOpacity  = Float(1)
	private(set) var brushHardness = Float(0.8)
	private(set) var canUndo       = false
	private(set) var canRedo       = false
	private(set) var undoName      = ""
	private(set) var selectedLayerID: Int32 = -1
	private(set) var layers: [LayerRow] = []
	private(set) var thumbnailsVersion: UInt32 = 0

	@ObservationIgnored private var layersSignature: UInt32 = 0
	@ObservationIgnored private var thumbnails: [Int32: CGImage] = [:]
	@ObservationIgnored private var thumbnailBuffer = [UInt8](repeating: 0, count: 512 * 512 * 4)

	func refresh() {
		var s = app_state_t()
		app_get_state(&s)
		guard s.ready else {
			return
		}
		set(&ready, true)
		set(&tool, s.tool)
		set(&brushRadius, s.brush_radius)
		set(&brushOpacity, s.brush_opacity)
		set(&brushHardness, s.brush_hardness)
		set(&canUndo, s.undos > 0)
		set(&canRedo, s.redos > 0)
		set(&undoName, String(cString: app_undo_name()))
		set(&selectedLayerID, s.selected_layer_id)
		if s.layers_signature != layersSignature {
			layersSignature = s.layers_signature
			set(&layers, readLayers())
		}
		if s.previews_version != thumbnailsVersion {
			thumbnails.removeAll()
			thumbnailsVersion = s.previews_version
		}
	}

	private func set<T: Equatable>(_ field: inout T, _ value: T) {
		if field != value {
			field = value
		}
	}

	private func readLayers() -> [LayerRow] {
		var rows: [LayerRow] = []
		var info = app_layer_info_t()
		for i in 0..<app_layer_count() where app_layer_info(i, &info) {
			rows.append(LayerRow(id: info.id, parentID: info.parent_id, depth: Int(info.depth),
			                     kind: LayerRow.Kind(rawValue: info.kind) ?? .layer, visible: info.visible,
			                     selected: info.selected, isFill: info.is_fill, opacity: info.opacity,
			                     name: String(cString: info.name)))
		}
		return rows
	}

	// Read back from the GPU on first use after the previews were redrawn
	func thumbnail(for id: Int32) -> CGImage? {
		if let image = thumbnails[id] {
			return image
		}
		var w: Int32 = 0
		var h: Int32 = 0
		let bytes = thumbnailBuffer.withUnsafeMutableBufferPointer {
			app_layer_thumbnail(id, $0.baseAddress, Int32($0.count), &w, &h)
		}
		guard bytes > 0 else {
			return nil
		}
		let data = Data(thumbnailBuffer[0..<Int(bytes)]) as CFData
		guard let provider = CGDataProvider(data: data),
		      let image = CGImage(width: Int(w), height: Int(h), bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(w) * 4,
		                          space: CGColorSpace(name: CGColorSpace.sRGB)!,
		                          bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
		                          provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
		else {
			return nil
		}
		thumbnails[id] = image
		return image
	}

	var selectedLayer: LayerRow? {
		layers.first { $0.id == selectedLayerID }
	}

	// Actions: the engine changes, refresh picks it up right away instead of on the next frame
	func perform(_ action: () -> Void) {
		action()
		refresh()
	}
}
