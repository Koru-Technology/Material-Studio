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

// The project's layers (app_api.h, app_api_layers.h), refreshed by AppStore after every frame
@Observable
final class LayersStore {
	static let shared = LayersStore()

	private(set) var selectedLayerID: Int32 = -1
	private(set) var layers: [LayerRow] = []
	private(set) var thumbnailsVersion: UInt32 = 0

	@ObservationIgnored private var layersSignature: UInt32 = 0
	@ObservationIgnored private var thumbnails: [Int32: CGImage] = [:]
	@ObservationIgnored private var thumbnailBuffer = [UInt8](repeating: 0, count: 512 * 512 * 4)

	func refresh(_ s: app_state_t) {
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
		guard bytes > 0, let image = makeImage(Array(thumbnailBuffer[0..<Int(bytes)]), width: Int(w), height: Int(h)) else {
			return nil
		}
		thumbnails[id] = image
		return image
	}

	var selectedLayer: LayerRow? {
		layers.first { $0.id == selectedLayerID }
	}
}

// RGBA8 rows top to bottom, as the app_api thumbnail functions return them
func makeImage(_ bytes: [UInt8], width: Int, height: Int) -> CGImage? {
	guard let provider = CGDataProvider(data: Data(bytes) as CFData) else {
		return nil
	}
	return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
	               space: CGColorSpace(name: CGColorSpace.sRGB)!,
	               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
	               provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
}
