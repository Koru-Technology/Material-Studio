import Observation
import SwiftUI

// The Decal tool's image and channels (app_api_materials.h): the decal stamps the selected material, a decal material is
// made of the picked image. Refreshed by AppStore while the tool is selected.
@Observable
final class DecalStore: ChangeOnly {
	static let shared = DecalStore()

	static let channelNames = ["Base Color", "Opacity", "Occlusion", "Roughness", "Metallic", "Normal", "Emission", "Height"]

	private(set) var image: Int32 = -1 // Of the selected material, -1: not a decal material
	private(set) var images: [String] = [] // The project's images
	private(set) var channelImages = [Int32](repeating: -1, count: channelNames.count)
	private(set) var channelValues = [Float](repeating: 0, count: channelNames.count)
	@ObservationIgnored private var importFrom: Int? // Images there were when Import was picked: the new one is used

	func refresh(tool: Int32) {
		guard tool == APP_TOOL_DECAL.id else {
			importFrom = nil
			return
		}
		let count = Int(app_mask_image_count())
		if images.count != count || (0..<count).contains(where: { String(cString: app_mask_image_name(Int32($0))) != images[$0] }) {
			update(\.images, (0..<count).map { String(cString: app_mask_image_name(Int32($0))) })
		}
		if let from = importFrom, count > from {
			importFrom = nil
			_ = app_decal_use_image(Int32(count - 1))
		}
		update(\.image, app_decal_image())
		update(\.channelImages, (0..<Int32(Self.channelNames.count)).map { app_decal_channel_image($0) })
		update(\.channelValues, (0..<Int32(Self.channelNames.count)).map { app_decal_channel_value($0) })
	}

	func use(_ image: Int32) {
		AppStore.shared.perform { _ = app_decal_use_image(image) }
	}

	func importImage() {
		importFrom = images.count
		AppStore.shared.perform { app_command(Int32(APP_COMMAND_IMPORT_TEXTURE.rawValue)) }
	}
}

#if os(iOS)
// Bottom middle while the Decal tool is selected: the image it stamps, and its channels
struct DecalBar: View {
	private var store    = DecalStore.shared
	private var popovers = PadPopovers.shared

	var body: some View {
		GlassEffectContainer(spacing: 8) {
			HStack(spacing: 8) {
				Menu {
					ForEach(Array(store.images.enumerated()), id: \.offset) { i, name in
						Button {
							store.use(Int32(i))
						} label: {
							if Int32(i) == store.image {
								Label(name, systemImage: "checkmark")
							}
							else {
								Text(name)
							}
						}
					}
					if !store.images.isEmpty {
						Divider()
					}
					Button("Import Image…", systemImage: "photo.badge.plus") {
						store.importImage()
					}
				} label: {
					HStack(spacing: 8) {
						Image(systemName: "photo")
						Text(store.image >= 0 && Int(store.image) < store.images.count ? store.images[Int(store.image)] : "Image")
							.lineLimit(1)
							.truncationMode(.middle)
						Image(systemName: "chevron.up.chevron.down")
							.font(.footnote.weight(.semibold))
							.foregroundStyle(.secondary)
					}
					.font(.body.weight(.medium))
					.padding(.horizontal, 16)
					.frame(maxWidth: 320)
					.frame(height: 44)
				}
				.glassEffect(.regular.interactive(), in: .capsule)
				.accessibilityLabel("Decal Image")
				Button {
					popovers.toggle("Decal")
				} label: {
					Image(systemName: "gearshape")
						.frame(width: 44, height: 44)
				}
				.glassEffect(popovers.shown == "Decal" ? .regular.tint(.accentColor).interactive() : .regular.interactive(), in: .circle)
				.disabled(store.image < 0)
				.accessibilityLabel("Decal Channels")
				.padPopover("Decal") {
					DecalChannelsPanel()
						.frame(width: 340, height: 520)
				}
			}
		}
		.foregroundStyle(.primary)
	}
}
#endif

// Each channel of the decal material: an image or a value (the normal only an image, the base color always one)
struct DecalChannelsPanel: View {
	private var store = DecalStore.shared
	@State private var drafts: [Int: Float] = [:] // A slider being dragged: set when it is let go, one undo step

	var body: some View {
		NavigationStack {
			Form {
				ForEach(0..<DecalStore.channelNames.count, id: \.self) { c in
					channel(c)
				}
			}
			.navigationTitle("Decal")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
		}
	}

	private func channel(_ c: Int) -> some View {
		let ch    = Int32(c)
		let image = store.channelImages[c]
		return Section(DecalStore.channelNames[c]) {
			Picker("Image", selection: Binding(get: { image }, set: { v in
				AppStore.shared.perform { _ = app_decal_set_channel_image(ch, v) }
			})) {
				if ch != APP_DECAL_BASE.id {
					Text(ch == APP_DECAL_NORMAL.id ? "None" : "Value").tag(Int32(-1))
				}
				ForEach(Array(store.images.enumerated()), id: \.offset) { i, name in
					Text(name).tag(Int32(i))
				}
			}
			if image < 0 && ch != APP_DECAL_BASE.id && ch != APP_DECAL_NORMAL.id {
				TipSlider(title: DecalStore.channelNames[c], value: drafts[c] ?? store.channelValues[c], range: 0...1, editingChanged: { editing in
					if !editing, let v = drafts.removeValue(forKey: c) {
						AppStore.shared.perform { _ = app_decal_set_channel_value(ch, v) }
					}
				}) { v in
					drafts[c] = v
				}
			}
		}
	}
}

extension app_decal_channel_t {
	var id: Int32 {
		Int32(rawValue)
	}
}
