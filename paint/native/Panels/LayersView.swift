import SwiftUI

// The layers panel: the iPad layers popover (a navigation stack, a layer's settings push in) and the Mac sidebar
// (the settings show in the inspector). Rows narrow down with the width they get.
struct LayersView: View {
	var body: some View {
#if os(iOS)
		LayersNavigation()
#else
		LayersList(pushesDetail: false)
#endif
	}
}

#if os(iOS)
// What the popover's stack shows: a layer's settings, then one of its mask effects
private enum LayersRoute: Hashable {
	case layer(Int32)
	case effect(layerID: Int32, index: Int32)

	// A deleted layer's settings close, as do those of an effect no longer there (deleted, or gone with an undo)
	func exists(in rows: [LayerRow]) -> Bool {
		switch self {
		case let .layer(id):
			rows.contains { $0.id == id }
		case let .effect(id, index):
			rows.first { $0.id == id }.map { Int(index) < $0.effects.count } ?? false
		}
	}
}

private struct LayersNavigation: View {
	@State private var path: [LayersRoute] = []

	var body: some View {
		NavigationStack(path: $path) {
			LayersList(pushesDetail: true)
				.navigationTitle("Layers")
				.navigationBarTitleDisplayMode(.inline)
				.toolbar {
					ToolbarItem(placement: .topBarTrailing) {
						NewLayerMenu()
					}
				}
				.navigationDestination(for: LayersRoute.self) { route in
					switch route {
					case let .layer(id):
						LayerDetailView(layerID: id)
					case let .effect(id, index):
						MaskEffectDetailView(layerID: id, index: index)
					}
				}
		}
		.environment(\.openLayerDetail) { id in
			path = [.layer(id)]
		}
		.environment(\.openMaskEffect) { id, index in
			path.append(.effect(layerID: id, index: index))
		}
		.frame(idealWidth: 360, idealHeight: 560)
		.onChange(of: LayersStore.shared.layers) { _, rows in
			let kept = path.filter { $0.exists(in: rows) }
			if kept != path {
				path = kept
			}
		}
	}
}
#endif

struct LayersList: View {
	let pushesDetail: Bool // iPad: a button on the row opens the layer's settings

	private var store  = AppStore.shared
	private var layers = LayersStore.shared
	@State private var renaming: LayerRow?
	@State private var compact = false

	init(pushesDetail: Bool) {
		self.pushesDetail = pushesDetail
	}

	var body: some View {
		list
			.listStyle(.plain)
			.environment(\.layersCompact, compact)
			.onGeometryChange(for: Bool.self) { $0.size.width < 250 } action: { compact = $0 }
			.layerRename($renaming)
			.environment(\.renameLayer) { renaming = $0 }
#if os(macOS)
			.safeAreaInset(edge: .bottom) {
				HStack(spacing: 12) {
					NewLayerMenu()
					Button {
						store.perform { _ = app_layer_delete(layers.selectedLayerID) }
					} label: {
						Label("Delete Layer", systemImage: "trash")
					}
					.disabled(!(layers.selectedLayer?.props.canDelete ?? false))
					Spacer()
				}
				.labelStyle(.iconOnly)
				.buttonStyle(.borderless)
				.menuIndicator(.hidden)
				.fixedSize(horizontal: false, vertical: true)
				.padding(8)
			}
#endif
	}

	@ViewBuilder
	private var list: some View {
#if os(macOS)
		List(selection: Binding(get: { layers.selectedLayerID }, set: { id in
			if let id, id != layers.selectedLayerID {
				store.perform { _ = app_layer_pick(id) }
			}
		})) {
			rows
		}
		.onDeleteCommand {
			if layers.selectedLayer?.props.canDelete ?? false {
				store.perform { _ = app_layer_delete(layers.selectedLayerID) }
			}
		}
#else
		List {
			rows
		}
#endif
	}

	private var rows: some View {
		ForEach(layers.visibleLayers) { layer in
			LayerRowView(layer: layer, pushesDetail: pushesDetail)
				.tag(layer.id)
				.contextMenu {
					LayerContextMenu(layer: layer)
				}
#if os(iOS)
				.listRowBackground(layer.selected ? Color.accentColor.opacity(0.22) : Color.clear)
				.listRowInsets(EdgeInsets(top: 4, leading: 8, bottom: 4, trailing: 8))
#endif
		}
		.onMove { from, to in
			move(from, to)
		}
	}

	// A row dropped before visible row `to`: it goes below the row above the gap
	private func move(_ from: IndexSet, _ to: Int) {
		let rows = layers.visibleLayers
		guard let source = from.first, from.count == 1, to != source, to != source + 1 else {
			return
		}
		let upper = to == 0 ? -1 : rows[to - 1].id
		store.perform { _ = app_layer_move_below(rows[source].id, upper) }
	}
}

struct LayerRowView: View {
	let layer: LayerRow
	let pushesDetail: Bool

	@Environment(\.layersCompact) private var compact
	@Environment(\.openLayerDetail) private var openDetail
	private var layers = LayersStore.shared

	init(layer: LayerRow, pushesDetail: Bool) {
		self.layer = layer
		self.pushesDetail = pushesDetail
	}

	var body: some View {
		HStack(spacing: 6) {
			disclosure
			thumbnails
			VStack(alignment: .leading, spacing: 1) {
				Text(layer.name)
					.lineLimit(1)
					.truncationMode(.middle)
				if !compact, let subtitle {
					Text(subtitle)
						.font(.caption)
						.foregroundStyle(.secondary)
						.lineLimit(1)
				}
			}
			.opacity(layer.visible && !layer.parentHidden ? 1 : 0.5)
			Spacer(minLength: 4)
			visibility
#if os(iOS)
			if pushesDetail {
				Button {
					if !layer.selected {
						AppStore.shared.perform { _ = app_layer_pick(layer.id) }
					}
					openDetail(layer.id)
				} label: {
					Image(systemName: "slider.horizontal.3")
						.font(.body)
						.frame(width: 30, height: 30)
						.contentShape(.rect)
				}
				.buttonStyle(.borderless)
				.accessibilityLabel("Layer Settings")
			}
#endif
		}
		.padding(.leading, CGFloat(layer.depth) * (compact ? 10 : 14))
		.contentShape(.rect)
#if os(iOS)
		.onTapGesture {
			if !layer.selected {
				AppStore.shared.perform { _ = app_layer_pick(layer.id) }
			}
		}
#endif
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(layer.selected ? .isSelected : [])
	}

	// Groups and layers with masks fold
	@ViewBuilder
	private var disclosure: some View {
		if layer.props.hasChildren {
			Button {
				AppStore.shared.perform { _ = app_layer_set_expanded(layer.id, !layer.props.expanded) }
			} label: {
				Image(systemName: "chevron.right")
					.font(.caption.weight(.semibold))
					.rotationEffect(.degrees(layer.props.expanded ? 90 : 0))
					.foregroundStyle(.secondary)
					.frame(width: 16, height: 28)
					.contentShape(.rect)
			}
			.buttonStyle(.borderless)
			.accessibilityLabel(layer.props.expanded ? "Collapse" : "Expand")
		}
		else {
			Color.clear.frame(width: 16, height: 1)
		}
	}

	private var thumbnailSize: CGFloat {
		compact ? 28 : 36
	}

	@ViewBuilder
	private var thumbnails: some View {
		HStack(spacing: 3) {
			if layer.kind == .group {
				Image(systemName: layer.props.expanded ? "folder" : "folder.fill")
					.font(.title3)
					.foregroundStyle(.secondary)
					.frame(width: thumbnailSize, height: thumbnailSize)
			}
			else {
				LayerThumbnail(id: layer.id, mask: layer.kind == .mask, size: thumbnailSize)
					.overlay(alignment: .bottomTrailing) {
						if let badge {
							Image(systemName: badge)
								.font(.system(size: 9, weight: .bold))
								.foregroundStyle(.white)
								.padding(2)
								.background(.tint, in: .circle)
								.offset(x: 3, y: 3)
						}
					}
				if layer.props.maskID >= 0 && !compact { // The layer's mask, as it paints through it
					LayerThumbnail(id: layer.props.maskID, mask: true, size: thumbnailSize)
				}
			}
		}
	}

	// Kind of layer, on the thumbnail
	private var badge: String? {
		if layer.kind == .filter {
			return "camera.filters"
		}
		if layer.props.isText {
			return "textformat"
		}
		if layer.props.isPath {
			return "scribble"
		}
		if layer.props.isDecal {
			return "seal.fill"
		}
		if layer.isFill {
			return "drop.fill"
		}
		return nil
	}

	private var subtitle: String? {
		var parts: [String] = []
		switch layer.kind {
		case .group:
			parts.append("Group")
		case .mask:
			parts.append(layer.isFill ? "Fill Mask" : layer.props.heightBlend ? "Height Blend Mask" : "Mask")
		case .filter:
			parts.append("Filter")
		case .layer:
			if layer.props.isText {
				parts.append("Text")
			}
			else if layer.props.isPath {
				parts.append("Path")
			}
			else if layer.isFill {
				parts.append(FillSummary.text(layer.props, materials: layers.materialNames))
			}
		}
		if layer.props.showsBlending && layer.props.blending != 0 {
			parts.append(LayersStore.blendingNames[Int(layer.props.blending)])
		}
		if layer.opacity < 0.995 {
			parts.append("\(Int((layer.opacity * 100).rounded()))%")
		}
		return parts.isEmpty ? nil : parts.joined(separator: " · ")
	}

	private var visibility: some View {
		Button {
			AppStore.shared.perform { _ = app_layer_set_visible(layer.id, !layer.visible) }
		} label: {
			Image(systemName: layer.visible ? "eye" : "eye.slash")
				.foregroundStyle(layer.visible && !layer.parentHidden ? .primary : .tertiary)
				.frame(width: 28, height: 28)
				.contentShape(.rect)
		}
		.buttonStyle(.borderless)
		.accessibilityLabel(layer.visible ? "Hide" : "Show")
	}
}

struct LayerThumbnail: View {
	let id: Int32
	let mask: Bool
	let size: CGFloat

	private var layers = LayersStore.shared

	init(id: Int32, mask: Bool, size: CGFloat) {
		self.id = id
		self.mask = mask
		self.size = size
	}

	var body: some View {
		let _ = layers.thumbnailsVersion // Redraws when the thumbnails change
		ZStack {
			if !mask {
				Checkerboard()
			}
			if let image = layers.thumbnail(for: id, mask: mask) {
				Image(decorative: image, scale: 1)
					.resizable()
					.interpolation(.medium)
			}
			else if mask {
				Color.secondary.opacity(0.2)
			}
		}
		.frame(width: size, height: size)
		.clipShape(.rect(cornerRadius: 5))
		.overlay {
			RoundedRectangle(cornerRadius: 5)
				.strokeBorder(.separator, lineWidth: 0.5)
		}
	}
}

// Behind transparent layer thumbnails
private struct Checkerboard: View {
	var body: some View {
		Canvas { context, size in
			let cell = size.width / 6
			context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.white))
			for y in 0..<6 {
				for x in 0..<6 where (x + y) % 2 == 1 {
					context.fill(Path(CGRect(x: CGFloat(x) * cell, y: CGFloat(y) * cell, width: cell, height: cell)),
					             with: .color(Color(white: 0.82)))
				}
			}
		}
	}
}

extension EnvironmentValues {
	@Entry var layersCompact = false
	@Entry var renameLayer: (LayerRow) -> Void = { _ in }
	@Entry var openLayerDetail: (Int32) -> Void = { _ in }
	@Entry var openMaskEffect: ((Int32, Int32) -> Void)? = nil // Pushes an effect's settings, nil: no navigation stack (Mac)
}
