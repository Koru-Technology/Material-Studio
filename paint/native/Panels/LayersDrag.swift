import SwiftUI
import UniformTypeIdentifiers

// Dragging a row of the layers list. Dropped in the upper or lower part of a row it goes above or below it, beside it in
// its group (or none); on the middle of a group row, into the group. Held over the middle of a layer in no group for a
// second, a group of that layer shows (only in the list); dropped then, the group is made of both, dragged away it goes.
@MainActor
@Observable
final class LayerDrag {
	static let shared = LayerDrag()

	enum Zone: Equatable {
		case above, below, into
	}

	private(set) var dragged: Int32?           // The layer dragged
	private(set) var target: Int32?            // The row under the finger
	private(set) var zone: Zone = .above
	private(set) var grouping: Int32?          // The layer the new group shows around
	@ObservationIgnored var rowHeights: [Int32: CGFloat] = [:]
	@ObservationIgnored private var hold: Task<Void, Never>?
	@ObservationIgnored private var leave: Task<Void, Never>?

	static let previewGroupID: Int32 = -1000 // The group row shown while grouping

	func begin(_ id: Int32) {
		reset()
		dragged = id
	}

	// The finger over a row at y (in the row)
	func hover(_ row: LayerRow, y: CGFloat) {
		leave?.cancel()
		guard let dragged, row.id != dragged || grouping != nil else {
			return
		}
		if row.id == Self.previewGroupID || (grouping != nil && row.id == grouping) {
			return // Grouping: the group shows while the finger stays on it or its layer
		}
		endGrouping()
		let h = rowHeights[row.id] ?? 44
		let z: Zone
		if row.kind == .group && y > h * 0.3 && y < h * 0.7 {
			z = .into
		}
		else if y < h * 0.35 {
			z = .above
		}
		else if y > h * 0.65 {
			z = row.kind == .group && row.props.expanded && row.props.hasChildren ? .into : .below
		}
		else {
			z = .into // The middle of a layer: held, a group of it shows
		}
		if target != row.id || zone != z {
			target = row.id
			zone   = z
			hold?.cancel()
			if z == .into && row.kind == .layer && row.parentID < 0 && row.id != dragged {
				let id = row.id
				hold = Task { @MainActor [weak self] in
					try? await Task.sleep(for: .seconds(1))
					guard let self, !Task.isCancelled, self.target == id, self.zone == .into else {
						return
					}
					withAnimation(.snappy(duration: 0.25)) {
						self.grouping = id
					}
				}
			}
		}
	}

	// Off the rows: a moment later (another row may take the finger), nothing is targeted and the group goes
	func exited(_ row: LayerRow) {
		leave?.cancel()
		leave = Task { @MainActor [weak self] in
			try? await Task.sleep(for: .milliseconds(250))
			guard let self, !Task.isCancelled else {
				return
			}
			self.hold?.cancel()
			self.target = nil
			self.endGrouping()
		}
	}

	func drop() -> Bool {
		defer {
			reset()
		}
		guard let dragged else {
			return false
		}
		if let grouping {
			AppStore.shared.perform { _ = app_layer_group_with(dragged, grouping) }
			return true
		}
		guard let target, target != dragged else {
			return false
		}
		let rows = LayersStore.shared
		let row  = rows.layer(target)
		let where_: app_layer_drop_t = switch zone {
		case .above: APP_LAYER_DROP_ABOVE
		case .below: APP_LAYER_DROP_BELOW
		case .into: row?.kind == .group ? APP_LAYER_DROP_INTO : APP_LAYER_DROP_BELOW // Let go before the group showed
		}
		AppStore.shared.perform { _ = app_layer_drop(dragged, target, Int32(where_.rawValue)) }
		return true
	}

	func reset() {
		hold?.cancel()
		leave?.cancel()
		dragged = nil
		target  = nil
		endGrouping()
	}

	private func endGrouping() {
		if grouping != nil {
			withAnimation(.snappy(duration: 0.25)) {
				grouping = nil
			}
		}
	}

	// The insertion line or highlight a row shows
	func mark(for id: Int32) -> Zone? {
		guard dragged != nil, grouping == nil, target == id, id != dragged else {
			return nil
		}
		return zone
	}
}

struct LayerDropDelegate: DropDelegate {
	let row: LayerRow

	func validateDrop(info: DropInfo) -> Bool {
		LayerDrag.shared.dragged != nil
	}

	func dropEntered(info: DropInfo) {
		LayerDrag.shared.hover(row, y: info.location.y)
	}

	func dropUpdated(info: DropInfo) -> DropProposal? {
		LayerDrag.shared.hover(row, y: info.location.y)
		return DropProposal(operation: .move)
	}

	func dropExited(info: DropInfo) {
		LayerDrag.shared.exited(row)
	}

	func performDrop(info: DropInfo) -> Bool {
		LayerDrag.shared.drop()
	}
}

// The group row shown while a layer is held over another
struct LayerGroupPreviewRow: View {
	var body: some View {
		HStack(spacing: 6) {
			Image(systemName: "chevron.right")
				.font(.caption.weight(.semibold))
				.rotationEffect(.degrees(90))
				.foregroundStyle(.secondary)
				.frame(width: 16, height: 28)
			Image(systemName: "folder")
				.font(.title3)
				.foregroundStyle(Color.accentColor)
				.frame(width: 36, height: 36)
			Text("New Group")
				.foregroundStyle(Color.accentColor)
			Spacer()
		}
	}
}
