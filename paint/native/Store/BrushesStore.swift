import CoreGraphics
import Foundation
import Observation

struct BrushGroupRow: Identifiable, Equatable {
	let id: Int32
	let name: String
	let builtin: Bool
	let hidden: Bool
}

struct BrushRow: Identifiable, Equatable {
	let id: Int32
	let groupID: Int32
	let name: String
	let builtin: Bool
	let selected: Bool
	let previewReady: Bool
	let previewKey: UInt32
}

// The brush library (app_api_brushes.h), refreshed by AppStore after every frame. Refreshes ColorStore too.
@Observable
final class BrushesStore: ChangeOnly {
	static let shared = BrushesStore()

	private(set) var groups: [BrushGroupRow] = []
	private(set) var brushes: [BrushRow] = []
	private(set) var recentIDs: [Int32] = []
	private(set) var selectedBrushID: Int32 = -1

	@ObservationIgnored private var librarySignature: UInt32 = 0
	@ObservationIgnored private var previewsSignature: UInt32 = 0
	@ObservationIgnored private var previews: [Int32: (key: UInt32, image: CGImage)] = [:]
	@ObservationIgnored private var previewBuffer = [UInt8]()

	func refresh() {
		var s = app_brushes_state_t()
		app_brushes_get_state(&s)
		update(\.selectedBrushID, s.selected_brush_id)
		if s.library_signature != librarySignature || s.previews_signature != previewsSignature {
			librarySignature = s.library_signature
			previewsSignature = s.previews_signature
			readLibrary()
		}
		ColorStore.shared.refresh(s.color_signature)
	}

	private func readLibrary() {
		var groupRows: [BrushGroupRow] = []
		var g = app_brush_group_info_t()
		for i in 0..<app_brush_group_count() where app_brush_group_info(i, &g) {
			groupRows.append(BrushGroupRow(id: g.id, name: String(cString: g.name), builtin: g.builtin, hidden: g.hidden))
		}
		var brushRows: [BrushRow] = []
		var b = app_brush_info_t()
		for i in 0..<app_brush_count() where app_brush_info(i, &b) {
			brushRows.append(BrushRow(id: b.id, groupID: b.group_id, name: String(cString: b.name), builtin: b.builtin,
			                          selected: b.selected, previewReady: b.preview_ready, previewKey: b.preview_key))
		}
		update(\.groups, groupRows)
		update(\.brushes, brushRows)
		update(\.recentIDs, (0..<3).map { app_brush_recent($0) }.filter { $0 >= 0 })
		let ids = Set(brushRows.map(\.id))
		previews = previews.filter { ids.contains($0.key) }
	}

	// Groups the popover lists, the brushes of each
	var listedGroups: [BrushGroupRow] {
		groups.filter { !$0.hidden }
	}

	var userGroups: [BrushGroupRow] {
		groups.filter { !$0.builtin }
	}

	func brushes(in group: Int32) -> [BrushRow] {
		brushes.filter { $0.groupID == group }
	}

	func brush(_ id: Int32) -> BrushRow? {
		brushes.first { $0.id == id }
	}

	var selectedBrush: BrushRow? {
		brush(selectedBrushID)
	}

	// The stroke of a brush: read back from the GPU once per preview_key, nil until it rendered
	func preview(for brush: BrushRow) -> CGImage? {
		if let cached = previews[brush.id], cached.key == brush.previewKey {
			return cached.image
		}
		guard brush.previewReady else {
			return previews[brush.id]?.image // The previous stroke shows until the new one arrives
		}
		let size = Int(app_brush_preview_size(brush.id))
		guard size > 0 else {
			return nil
		}
		if previewBuffer.count < size {
			previewBuffer = [UInt8](repeating: 0, count: size)
		}
		var w: Int32 = 0
		var h: Int32 = 0
		let bytes = previewBuffer.withUnsafeMutableBufferPointer {
			app_brush_preview(brush.id, $0.baseAddress, Int32($0.count), &w, &h)
		}
		guard bytes > 0, let image = makeStraightImage(Array(previewBuffer[0..<Int(bytes)]), width: Int(w), height: Int(h)) else {
			return nil
		}
		previews[brush.id] = (brush.previewKey, image)
		return image
	}
}

// RGBA8 rows top to bottom, alpha not premultiplied (the brush strokes)
private func makeStraightImage(_ bytes: [UInt8], width: Int, height: Int) -> CGImage? {
	guard let provider = CGDataProvider(data: Data(bytes) as CFData) else {
		return nil
	}
	return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
	               space: CGColorSpace(name: CGColorSpace.sRGB)!,
	               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
	               provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
}
