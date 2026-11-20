import CoreGraphics
import Foundation
import Observation

// What a layer is and which layer menu actions apply to it (app_layer_props_t)
struct LayerProps: Equatable {
	var hasChildren = false
	var expanded = true
	var isPath = false
	var isText = false
	var isDecal = false // Projected from the decal box: decal or planar fills
	var maskID: Int32 = -1
	var blending: Int32 = 0
	var uvMap: Int32 = 0
	var hasUV1 = false
	var isFill = false
	var fill = FillInfo()      // Fill layers and fill masks
	var sources: [FillSource] = [] // Their channels' sources, app_fill_channel_t order
	var channels: UInt32 = 0
	var raise: Float = 0
	var showsBlending = false
	var showsChannels = false
	var showsRaise = false
	var showsToFill = false
	var showsToPaint = false
	var showsClear = false
	var showsFillPaint = false
	var showsInvert = false
	var showsApply = false
	var showsAddMask = false
	var showsAddEffect = false
	var showsMergeGroup = false
	var showsSaveSmartMaterial = false
	var canDelete = false
	var canMergeDown = false
	var canMergeGroup = false

	init() {}

	// A decal layer: its fill is fixed to the decal projection
	var isDecalLayer: Bool {
		isFill && fill.projection == Int32(APP_FILL_PROJECTION_DECAL.rawValue)
	}

	init(_ p: app_layer_props_t) {
		hasChildren = p.has_children
		expanded = p.expanded
		isPath = p.is_path
		isText = p.is_text
		isDecal = p.is_decal
		maskID = p.mask_id
		blending = p.blending
		uvMap = p.uv_map
		hasUV1 = p.has_uv1
		isFill = p.is_fill
		if p.is_fill {
			var f = app_fill_t()
			if app_layer_fill(p.id, &f) {
				fill = FillInfo(f)
			}
			var s = app_fill_source_t()
			sources = (0..<Int32(APP_FILL_CHANNEL_COUNT.rawValue)).compactMap { app_layer_fill_source(p.id, $0, &s) ? FillSource(s) : nil }
		}
		channels = p.channels
		raise = p.height_raise
		showsBlending = p.shows_blending
		showsChannels = p.shows_channels
		showsRaise = p.shows_raise
		showsToFill = p.shows_to_fill
		showsToPaint = p.shows_to_paint
		showsClear = p.shows_clear
		showsFillPaint = p.shows_fill_paint
		showsInvert = p.shows_invert
		showsApply = p.shows_apply
		showsAddMask = p.shows_add_mask
		showsAddEffect = p.shows_add_effect
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

// A fill's projection (app_fill_t)
struct FillInfo: Equatable {
	var projection: Int32 = 0
	var scaleX: Float = 1
	var scaleY: Float = 1
	var offsetX: Float = 0
	var offsetY: Float = 0
	var angle: Float = 0
	var hardness: Float = 0.5
	var isMask = false

	init() {}

	init(_ f: app_fill_t) {
		projection = f.projection
		scaleX = f.scale_x
		scaleY = f.scale_y
		offsetX = f.offset_x
		offsetY = f.offset_y
		angle = f.angle
		hardness = f.hardness
		isMask = f.is_mask
	}
}

// Where one channel of a fill comes from (app_fill_source_t)
struct FillSource: Equatable {
	var kind: Int32 = 0
	var color: [Float] = [0, 0, 0]
	var color2: [Float] = [0, 0, 0]
	var image: Int32 = -1
	var imageChannel: Int32 = 4
	var pattern: Int32 = 0
	var params: [Float] = []
	var material: Int32 = -1

	init(_ s: app_fill_source_t) {
		kind = s.kind
		color = withUnsafeBytes(of: s.color) { Array($0.bindMemory(to: Float.self)) }
		color2 = withUnsafeBytes(of: s.color2) { Array($0.bindMemory(to: Float.self)) }
		image = s.image
		imageChannel = s.image_channel
		pattern = s.pattern
		params = withUnsafeBytes(of: s.params) { Array($0.bindMemory(to: Float.self)) }
		material = s.material
	}
}

// One effect of a mask's stack (app_mask_effect_t)
struct MaskEffect: Equatable {
	let type: Int32
	let visible: Bool
	let opacity: Float
	let blending: Int32
	let params: [Float]
	let isPaint: Bool
	let isFilter: Bool
	let isGenerator: Bool
	let needsAO: Bool
	let needsThickness: Bool
	let needsIDMap: Bool

	init(_ e: app_mask_effect_t) {
		type = e.type
		visible = e.visible
		opacity = e.opacity
		blending = e.blending
		params = withUnsafeBytes(of: e.params) { Array($0.bindMemory(to: Float.self)) }
		isPaint = e.is_paint
		isFilter = e.is_filter
		isGenerator = e.is_generator
		needsAO = e.needs_ao
		needsThickness = e.needs_thickness
		needsIDMap = e.needs_id_map
	}

	var name: String {
		LayersStore.effectTypeNames.indices.contains(Int(type)) ? LayersStore.effectTypeNames[Int(type)] : "Effect"
	}

	var settings: [MaskParam] { // Of its type
		LayersStore.effectParams.indices.contains(Int(type)) ? LayersStore.effectParams[Int(type)] : []
	}
}

// One setting of an effect type (app_mask_param_t)
struct MaskParam: Identifiable {
	let name: String
	let index: Int32 // Into MaskEffect.params
	let kind: app_mask_param_kind_t
	let range: ClosedRange<Float>
	let options: [String] // Choices
	let shownBy: Int32    // The choice it depends on, when shownWhen is not 0
	let shownWhen: UInt32 // Bit v: shown while params[shownBy] is v, 0 always

	var id: Int32 {
		index
	}

	var isNoise: Bool {
		index >= APP_MASK_NOISE_FIRST
	}

	// Shown for the effect's current choices (a Fill's Image setting only for the Image source)
	func shown(in effect: MaskEffect) -> Bool {
		guard shownWhen != 0, effect.params.indices.contains(Int(shownBy)) else {
			return true
		}
		let v = Int(effect.params[Int(shownBy)].rounded())
		return v >= 0 && v < 32 && shownWhen & (1 << UInt32(v)) != 0
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
	var effects: [MaskEffect] = [] // Masks: the effect stack, top first
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
	private(set) var imageNames: [String] = []    // Project images, for the image pickers of fill effects and fill layers
	private(set) var canNew: Set<Int32> = []       // app_layer_new_t kinds the "+" menu enables

	@ObservationIgnored private var layersSignature: UInt32 = 0
	@ObservationIgnored private var propsSignature: UInt32 = 0
	@ObservationIgnored private var thumbnails: [Int32: CGImage] = [:]
	@ObservationIgnored private let thumbnailReader = PixelReader(capacity: 512 * 512 * 4)

	static let patternNames: [String] = (0..<app_fill_pattern_count()).map { String(cString: app_fill_pattern_name($0)) }
	static let blendingNames: [String] = (0..<app_layer_blending_count()).map { String(cString: app_layer_blending_name($0)) }
	static let maskBlendingNames: [String] = (0..<app_mask_blending_count()).map { String(cString: app_mask_blending_name($0)) }
	static let effectTypeNames: [String] = (0..<app_mask_effect_type_count()).map { String(cString: app_mask_effect_type_name($0)) }
	// The Add Effect menu's sections, all types but Paint
	static let effectSources: [Int32] = (1..<app_mask_effect_type_count()).filter { !app_mask_effect_type_is_filter($0) && !app_mask_effect_type_is_generator($0) }
	static let effectFilters: [Int32] = (1..<app_mask_effect_type_count()).filter { app_mask_effect_type_is_filter($0) }
	static let effectGenerators: [Int32] = { // The Mask Builder first, it combines the others
		let all = (1..<app_mask_effect_type_count()).filter { app_mask_effect_type_is_generator($0) }
		let builder = Int32(APP_MASK_EFFECT_MASK_BUILDER.rawValue)
		return all.filter { $0 == builder } + all.filter { $0 != builder }
	}()
	static let effectParams: [[MaskParam]] = (0..<app_mask_effect_type_count()).map { type in
		var p = app_mask_param_t()
		return (0..<app_mask_effect_param_count(type)).compactMap { i in
			guard app_mask_effect_param(type, i, &p) else {
				return nil
			}
			let options = String(cString: p.options)
			return MaskParam(name: String(cString: p.name), index: p.index, kind: app_mask_param_kind_t(UInt32(p.kind)),
			                 range: p.min...p.max, options: options.isEmpty ? [] : options.components(separatedBy: ","),
			                 shownBy: p.shown_by, shownWhen: p.shown_when)
		}
	}

	func refresh(_ s: app_state_t) {
		update(\.selectedLayerID, s.selected_layer_id)
		if s.layers_signature != layersSignature || s.layer_props_signature != propsSignature {
			layersSignature = s.layers_signature
			propsSignature = s.layer_props_signature
			let rows = readLayers()
			update(\.layers, rows)
			update(\.visibleLayers, Self.visible(rows))
			update(\.materialNames, (0..<app_layer_material_count()).map { String(cString: app_layer_material_name($0)) })
			update(\.imageNames, (0..<app_mask_image_count()).map { String(cString: app_mask_image_name($0)) })
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
				var effect = app_mask_effect_t()
				row.effects = (0..<props.effect_count).compactMap { app_mask_effect(info.id, $0, &effect) ? MaskEffect(effect) : nil }
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
		let gray: (inout [UInt8]) -> Void = { pixels in
			for i in stride(from: 0, to: pixels.count, by: 4) {
				pixels[i + 1] = pixels[i]
				pixels[i + 2] = pixels[i]
				pixels[i + 3] = 255
			}
		}
		guard let image = thumbnailReader.read(premultiplied: true, adjust: mask ? gray : nil, { app_layer_thumbnail(id, $0, $1, &$2, &$3) }).image else {
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

	private func isDescendant(_ row: LayerRow, of ancestor: Int32) -> Bool {
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
