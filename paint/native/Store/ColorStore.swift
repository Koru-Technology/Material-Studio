import Foundation
import Observation
import SwiftUI

// A paint color: base color and the material values that go with it (app_color_t)
struct PaintColor: Equatable {
	var base: UInt32 = 0xffffffff // 0xAARRGGBB, alpha always 0xff
	var opacity: Float = 1
	var occlusion: Float = 1
	var roughness: Float = 0.8
	var metallic: Float = 0
	var height: Float = 0

	init() {}

	init(_ c: app_color_t) {
		base = c.base
		opacity = c.opacity
		occlusion = c.occlusion
		roughness = c.roughness
		metallic = c.metallic
		height = c.height
	}

	var red: Double { Double((base >> 16) & 0xff) / 255 }
	var green: Double { Double((base >> 8) & 0xff) / 255 }
	var blue: Double { Double(base & 0xff) / 255 }

	var color: Color {
		Color(.sRGB, red: red, green: green, blue: blue)
	}

	var hex: String {
		String(format: "#%06X", base & 0xffffff)
	}
}

struct SwatchRow: Identifiable, Equatable {
	let id: Int // Index in the project's swatches
	let color: PaintColor
}

// The picked color and the swatches (app_api_brushes.h), refreshed by BrushesStore after every frame
@Observable
final class ColorStore {
	static let shared = ColorStore()

	private(set) var picked = PaintColor()
	private(set) var swatches: [SwatchRow] = []
	private(set) var selectedSwatch = -1
	private(set) var canApply = false

	@ObservationIgnored private var signature: UInt32 = 0

	func refresh(_ colorSignature: UInt32) {
		guard colorSignature != signature else {
			return
		}
		signature = colorSignature
		var c = app_color_t()
		if app_color_get(&c) {
			set(&picked, PaintColor(c))
		}
		var rows: [SwatchRow] = []
		for i in 0..<app_swatch_count() where app_swatch_info(i, &c) {
			rows.append(SwatchRow(id: Int(i), color: PaintColor(c)))
		}
		set(&swatches, rows)
		set(&selectedSwatch, Int(app_swatch_selected()))
		set(&canApply, app_color_can_apply())
	}
}
