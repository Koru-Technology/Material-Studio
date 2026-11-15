import CoreGraphics
import Foundation
import Observation

// What a layer is and which layer menu actions apply to it (app_layer_props_t)
struct LayerProps: Equatable {
	var hasChildren = false
	var expanded = true
	var isPath = false
	var isText = false
	var isDecal = false
	var maskID: Int32 = -1
	var blending: Int32 = 0
	var uvType: Int32 = 0
	var uvMap: Int32 = 0
	var hasUV1 = false
	var scale: Float = 1
	var angle: Float = 0
	var fillMaterial: Int32 = -1
	var channels: UInt32 = 0
	var heightBlend = false
	var heightBlendBelow = false
	var heightBlendReach: Float = 0
	var heightBlendInfluence: Float = 0
	var heightBlendSharpness: Float = 1
	var heightBlendOffset: Float = 0
	var showsBlending = false
	var showsChannels = false
	var showsToFill = false
	var showsToPaint = false
	var showsClear = false
	var showsInvert = false
	var showsApply = false
	var showsAddMask = false
	var showsMergeGroup = false
	var showsSaveSmartMaterial = false
	var canDelete = false
	var canMergeDown = false
	var canMergeGroup = false

	init() {}

	init(_ p: app_layer_props_t) {
		hasChildren = p.has_children
		expanded = p.expanded
		isPath = p.is_path
		isText = p.is_text
		isDecal = p.is_decal
		maskID = p.mask_id
		blending = p.blending
		uvType = p.uv_type
		uvMap = p.uv_map
		hasUV1 = p.has_uv1
		scale = p.scale
		angle = p.angle
		fillMaterial = p.fill_material
		channels = p.channels
		heightBlend = p.height_blend
		heightBlendBelow = p.height_blend_below
		heightBlendReach = p.height_blend_reach
		heightBlendInfluence = p.height_blend_influence
		heightBlendSharpness = p.height_blend_sharpness
		heightBlendOffset = p.height_blend_offset
		showsBlending = p.shows_blending
		showsChannels = p.shows_channels
		showsToFill = p.shows_to_fill
		showsToPaint = p.shows_to_paint
		showsClear = p.shows_clear
		showsInvert = p.shows_invert
		showsApply = p.shows_apply
		showsAddMask = p.shows_add_mask
		showsMergeGroup = p.shows_merge_group
		showsSaveSmartMaterial = p.shows_save_smart_material
		canDelete = p.can_delete
		canMergeDown = p.can_merge_down
		canMergeGroup = p.can_merge_group
	}

	func paints(_ channel: app_layer_channel_t) -> Bool {
		channels & (1 << channel.rawValue) != 0
	}
}

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
	var props = LayerProps()
	var parentHidden = false // A group or layer above it is hidden, so it is too
}

// The project's layers (app_api.h, app_api_layers.h), refreshed by AppStore after every frame
@Observable
final class LayersStore: ChangeOnly {
	static let shared = LayersStore()

	private(set) var selectedLayerID: Int32 = -1
	private(set) var layers: [LayerRow] = []
	private(set) var visibleLayers: [LayerRow] = [] // Without the children of folded groups and layers
	private(set) var thumbnailsVersion: UInt32 = 0
	private(set) var materialNames: [String] = [] // Project materials, for the fill material picker
	private(set) var canNew: Set<Int32> = []       // app_layer_new_t kinds the "+" menu enables

	@ObservationIgnored private var layersSignature: UInt32 = 0
	@ObservationIgnored private var propsSignature: UInt32 = 0
	@ObservationIgnored private var thumbnails: [Int32: CGImage] = [:]
	@ObservationIgnored private var thumbnailBuffer = [UInt8](repeating: 0, count: 512 * 512 * 4)

	static let blendingNames: [String] = (0..<app_layer_blending_count()).map { String(cString: app_layer_blending_name($0)) }

	func refresh(_ s: app_state_t) {
		update(\.selectedLayerID, s.selected_layer_id)
		let props = app_layers_props_signature()
		if s.layers_signature != layersSignature || props != propsSignature {
			layersSignature = s.layers_signature
			propsSignature = props
			let rows = readLayers()
			update(\.layers, rows)
			update(\.visibleLayers, Self.visible(rows))
			update(\.materialNames, (0..<app_layer_material_count()).map { String(cString: app_layer_material_name($0)) })
			update(\.canNew, Set((APP_LAYER_NEW_PAINT.rawValue...APP_LAYER_NEW_ID_MASK.rawValue).filter { app_layer_can_new(Int32($0)) }.map { Int32($0) }))
		}
		if s.previews_version != thumbnailsVersion {
			thumbnails.removeAll()
			thumbnailsVersion = s.previews_version
		}
	}

	private func readLayers() -> [LayerRow] {
		var rows: [LayerRow] = []
		var info = app_layer_info_t()
		var props = app_layer_props_t()
		var hidden: [Int32: Bool] = [:] // Layer id: it or a parent is hidden
		for i in 0..<app_layer_count() where app_layer_info(i, &info) {
			var row = LayerRow(id: info.id, parentID: info.parent_id, depth: Int(info.depth),
			                   kind: LayerRow.Kind(rawValue: info.kind) ?? .layer, visible: info.visible,
			                   selected: info.selected, isFill: info.is_fill, opacity: info.opacity,
			                   name: String(cString: info.name))
			if app_layer_props(info.id, &props) {
				row.props = LayerProps(props)
			}
			row.parentHidden = hidden[info.parent_id] ?? false // Listed top first: parents before their children
			hidden[info.id] = row.parentHidden || !info.visible
			rows.append(row)
		}
		return rows
	}

	// Children of a folded group or layer do not show
	private static func visible(_ rows: [LayerRow]) -> [LayerRow] {
		var shown: [Int32: Bool] = [:] // Layer id: its children show
		var result: [LayerRow] = []
		for row in rows {
			let parentShows = row.parentID < 0 || (shown[row.parentID] ?? true)
			shown[row.id] = parentShows && row.props.expanded
			if parentShows {
				result.append(row)
			}
		}
		return result
	}

	// Read back from the GPU on first use after the previews were redrawn. A mask's red channel shows in gray.
	func thumbnail(for id: Int32, mask: Bool = false) -> CGImage? {
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
		var pixels = Array(thumbnailBuffer[0..<Int(bytes)])
		if mask {
			for i in stride(from: 0, to: pixels.count, by: 4) {
				pixels[i + 1] = pixels[i]
				pixels[i + 2] = pixels[i]
				pixels[i + 3] = 255
			}
		}
		guard let image = makeImage(pixels, width: Int(w), height: Int(h)) else {
			return nil
		}
		thumbnails[id] = image
		return image
	}

	var selectedLayer: LayerRow? {
		layers.first { $0.id == selectedLayerID }
	}

	func layer(_ id: Int32) -> LayerRow? {
		layers.first { $0.id == id }
	}

	// Groups a layer can move into: not itself, its own group or one inside it
	func groups(for row: LayerRow) -> [LayerRow] {
		guard row.kind == .layer || row.kind == .group else {
			return []
		}
		return layers.filter { $0.kind == .group && $0.id != row.id && $0.id != row.parentID && !isDescendant($0, of: row.id) }
	}

	func isDescendant(_ row: LayerRow, of ancestor: Int32) -> Bool {
		var parent = row.parentID
		while parent >= 0 {
			if parent == ancestor {
				return true
			}
			parent = layer(parent)?.parentID ?? -1
		}
		return false
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
