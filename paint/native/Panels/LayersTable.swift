#if os(iOS)
import SwiftUI
import UIKit

// The iPad layers list: a table, for the system's drag to reorder (the row lifts, a gap opens where it would go, it
// drops into place). Let go in a gap, the layer goes there, beside the row below it in its group (or none). Over the
// middle of a group row, the group highlights and the layer goes into it. Held a second over the middle of a layer in no
// group, a new group shows around that layer; let go then, the group is made of both; moved away, it goes.
struct LayersTable: UIViewRepresentable {
	let ids: [Int32] // The rows, LayersStore.visibleLayers

	func makeCoordinator() -> Coordinator {
		Coordinator()
	}

	func makeUIView(context: Context) -> UITableView {
		context.coordinator.makeTable()
	}

	func updateUIView(_ table: UITableView, context: Context) {
		context.coordinator.apply(ids, animated: true)
	}

	@MainActor
	final class Coordinator: NSObject, UITableViewDelegate, UITableViewDragDelegate, UITableViewDropDelegate {
		private var table: UITableView!
		private var source: UITableViewDiffableDataSource<Int, Int32>!
		private var ids: [Int32] = []
		private var drag = LayersTableDrag.shared

		func makeTable() -> UITableView {
			let table = UITableView(frame: .zero, style: .plain)
			table.backgroundColor              = .clear
			table.separatorStyle               = .none
			table.allowsSelection              = false // A tap on the row's content picks it
			table.dragInteractionEnabled       = true
			table.dragDelegate                 = self
			table.dropDelegate                 = self
			table.delegate                     = self
			table.rowHeight                    = UITableView.automaticDimension
			table.estimatedRowHeight           = 52
			table.selfSizingInvalidation       = .enabledIncludingConstraints
			table.register(UITableViewCell.self, forCellReuseIdentifier: "layer")
			source = UITableViewDiffableDataSource<Int, Int32>(tableView: table) { table, path, id in
				let cell = table.dequeueReusableCell(withIdentifier: "layer", for: path)
				cell.contentConfiguration   = UIHostingConfiguration { LayerTableCell(id: id) }.margins(.all, 0)
				cell.backgroundConfiguration = .clear()
				return cell
			}
			source.defaultRowAnimation = .fade
			self.table = table
			return table
		}

		func apply(_ new: [Int32], animated: Bool) {
			guard new != ids else {
				return
			}
			ids = new
			var snapshot = NSDiffableDataSourceSnapshot<Int, Int32>()
			snapshot.appendSections([0])
			snapshot.appendItems(new)
			source.apply(snapshot, animatingDifferences: animated && table.window != nil)
		}

		// MARK: Drag

		func tableView(_ table: UITableView, itemsForBeginning session: UIDragSession, at path: IndexPath) -> [UIDragItem] {
			guard path.row < ids.count, let row = LayersStore.shared.layer(ids[path.row]), row.kind != .mask else {
				return []
			}
			drag.begin(row.id)
			let item         = UIDragItem(itemProvider: NSItemProvider(object: "\(row.id)" as NSString))
			item.localObject = row.id
			return [item]
		}

		func tableView(_ table: UITableView, dragPreviewParametersForRowAt path: IndexPath) -> UIDragPreviewParameters? {
			let p = UIDragPreviewParameters()
			if let cell = table.cellForRow(at: path) {
				p.visiblePath = UIBezierPath(roundedRect: cell.bounds.insetBy(dx: 4, dy: 2), cornerRadius: 10)
			}
			return p
		}

		func tableView(_ table: UITableView, dragSessionDidEnd session: UIDragSession) {
			drag.reset()
		}

		// MARK: Drop

		func tableView(_ table: UITableView, canHandle session: UIDropSession) -> Bool {
			session.localDragSession != nil
		}

		func tableView(_ table: UITableView, dropSessionDidUpdate session: UIDropSession,
		               withDestinationIndexPath destination: IndexPath?) -> UITableViewDropProposal {
			let p = session.location(in: table)
			if let path = table.indexPathForRow(at: p), path.row < ids.count, let row = LayersStore.shared.layer(ids[path.row]),
			   row.id != drag.dragged {
				let rect = table.rectForRow(at: path)
				let f    = (p.y - rect.minY) / max(rect.height, 1)
				if drag.grouping == row.id { // Its new group shows: the whole row keeps it
					return UITableViewDropProposal(operation: .move, intent: .insertIntoDestinationIndexPath)
				}
				if f > 0.25 && f < 0.75 && (row.kind == .group || (row.kind == .layer && row.parentID < 0)) {
					drag.hold(over: row.id, group: row.kind == .group)
					return UITableViewDropProposal(operation: .move, intent: .insertIntoDestinationIndexPath)
				}
			}
			drag.leave()
			return UITableViewDropProposal(operation: .move, intent: .insertAtDestinationIndexPath)
		}

		func tableView(_ table: UITableView, dropSessionDidExit session: UIDropSession) {
			drag.leave()
		}

		func tableView(_ table: UITableView, dropSessionDidEnd session: UIDropSession) {
			drag.reset()
		}

		func tableView(_ table: UITableView, performDropWith coordinator: UITableViewDropCoordinator) {
			guard let dragged = drag.dragged else {
				return
			}
			let grouping = drag.grouping
			let into     = coordinator.proposal.intent == .insertIntoDestinationIndexPath
			let path     = coordinator.destinationIndexPath
			drag.reset()
			var done = false
			if into, let path, path.row < ids.count, let target = LayersStore.shared.layer(ids[path.row]) {
				if grouping == target.id {
					AppStore.shared.perform { done = app_layer_group_with(dragged, target.id) >= 0 }
				}
				else if target.kind == .group {
					AppStore.shared.perform { done = app_layer_drop(dragged, target.id, Int32(APP_LAYER_DROP_INTO.rawValue)) }
				}
			}
			else if let path {
				let rest = ids.filter { $0 != dragged } // As the rows are with the dragged one out
				let at   = min(path.row, rest.count)
				AppStore.shared.perform {
					if at < rest.count {
						done = app_layer_drop(dragged, rest[at], Int32(APP_LAYER_DROP_ABOVE.rawValue))
					}
					else if let last = rest.last {
						done = app_layer_drop(dragged, last, Int32(APP_LAYER_DROP_BELOW.rawValue))
					}
				}
			}
			apply(LayersStore.shared.visibleLayers.map(\.id), animated: true)
			if done, let item = coordinator.items.first, let at = ids.firstIndex(of: dragged) {
				coordinator.drop(item.dragItem, toRowAt: IndexPath(row: at, section: 0))
			}
		}

		// MARK: Menu

		func tableView(_ table: UITableView, contextMenuConfigurationForRowAt path: IndexPath, point: CGPoint) -> UIContextMenuConfiguration? {
			guard path.row < ids.count, let row = LayersStore.shared.layer(ids[path.row]) else {
				return nil
			}
			return UIContextMenuConfiguration(identifier: nil, previewProvider: nil) { _ in
				layerMenu(for: row)
			}
		}
	}
}

// What a drag over the table does: the layer dragged, the row held over and the new group showing around it
@MainActor
@Observable
final class LayersTableDrag {
	static let shared = LayersTableDrag()

	private(set) var dragged: Int32?
	private(set) var grouping: Int32? // The layer a new group shows around
	@ObservationIgnored private var holding: Int32?
	@ObservationIgnored private var timer: Task<Void, Never>?

	func begin(_ id: Int32) {
		reset()
		dragged = id
	}

	// Over the middle of a row: a layer in no group gets a new group around it once held a second
	func hold(over id: Int32, group: Bool) {
		guard holding != id else {
			return
		}
		leave()
		holding = id
		if group {
			return
		}
		timer = Task { @MainActor [weak self] in
			try? await Task.sleep(for: .seconds(1))
			guard let self, !Task.isCancelled, self.holding == id, self.dragged != nil else {
				return
			}
			withAnimation(.snappy(duration: 0.25)) {
				self.grouping = id
			}
		}
	}

	func leave() {
		timer?.cancel()
		holding = nil
		if grouping != nil {
			withAnimation(.snappy(duration: 0.25)) {
				grouping = nil
			}
		}
	}

	func reset() {
		leave()
		dragged = nil
	}
}

// A row: the layer's row, under the new group it would make while a dragged layer is held over it
private struct LayerTableCell: View {
	let id: Int32

	private var layers = LayersStore.shared
	private var drag   = LayersTableDrag.shared

	init(id: Int32) {
		self.id = id
	}

	var body: some View {
		if let layer = layers.layer(id) {
			VStack(alignment: .leading, spacing: 0) {
				if drag.grouping == id {
					HStack(spacing: 6) {
						Image(systemName: "chevron.down")
							.font(.caption.weight(.semibold))
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
					.padding(.horizontal, 8)
					.padding(.vertical, 4)
					.transition(.opacity)
				}
				LayerRowView(layer: layer, pushesDetail: true)
					.padding(.leading, drag.grouping == id ? 14 : 0)
					.padding(.horizontal, 8)
					.padding(.vertical, 4)
					.background(layer.selected || layer.props.maskID == layers.selectedLayerID ? Color.accentColor.opacity(0.22) : Color.clear)
			}
			.background(drag.grouping == id ? Color.accentColor.opacity(0.12) : Color.clear)
			.environment(\.openLayerDetail) { id in
				LayersPanelState.shared.path = [.layer(id)]
			}
		}
	}
}

// A row's menu, as LayerContextMenu (the Mac's), for the table
@MainActor
func layerMenu(for layer: LayerRow) -> UIMenu {
	let store  = AppStore.shared
	let layers = LayersStore.shared
	let p      = layer.props
	func action(_ title: String, _ symbol: String, destructive: Bool = false, disabled: Bool = false, _ run: @escaping () -> Void) -> UIAction {
		var attributes: UIMenuElement.Attributes = []
		if destructive {
			attributes.insert(.destructive)
		}
		if disabled {
			attributes.insert(.disabled)
		}
		return UIAction(title: title, image: UIImage(systemName: symbol), attributes: attributes) { _ in
			run()
		}
	}
	var sections: [UIMenuElement] = []
	sections.append(UIMenu(options: .displayInline, children: [
		action("Rename…", "pencil") { renameLayer(layer) },
		action("Duplicate", "plus.square.on.square") { store.perform { _ = app_layer_duplicate(layer.id) } },
		action(layer.visible ? "Hide" : "Show", layer.visible ? "eye.slash" : "eye") {
			store.perform { _ = app_layer_set_visible(layer.id, !layer.visible) }
		},
	]))
	var merge: [UIMenuElement] = [action("Merge Down", "arrow.down.to.line", disabled: !p.canMergeDown) {
		store.perform { _ = app_layer_merge_down(layer.id) }
	}]
	if p.showsMergeGroup {
		merge.append(action("Merge Group", "square.stack.3d.down.forward", disabled: !p.canMergeGroup) {
			store.perform { _ = app_layer_merge_group(layer.id) }
		})
	}
	if p.showsToFill {
		merge.append(action("To Fill Layer", "drop.fill") { store.perform { _ = app_layer_to_fill(layer.id) } })
	}
	if p.showsToPaint {
		merge.append(action("To Paint Layer", "paintbrush") { store.perform { _ = app_layer_to_paint(layer.id) } })
	}
	sections.append(UIMenu(options: .displayInline, children: merge))
	if layer.isFill, let mask = layers.layer(p.maskID) { // Its mask, which the list does not show
		var items: [UIMenuElement] = []
		if mask.props.showsFillPaint {
			items.append(action("Fill Mask", "circle.fill") { store.perform { _ = app_layer_fill_paint(mask.id) } })
		}
		if mask.props.showsClear {
			items.append(action("Clear Mask", "circle") { store.perform { _ = app_layer_clear(mask.id) } })
		}
		sections.append(UIMenu(options: .displayInline, children: items))
	}
	var more: [UIMenuElement] = []
	if p.showsAddMask {
		more.append(UIMenu(title: "Add Mask", image: UIImage(systemName: "circle.lefthalf.filled"), children: [
			action("Black Mask", "circle.fill") { store.perform { _ = app_layer_add_mask(layer.id, Int32(APP_LAYER_NEW_BLACK_MASK.rawValue)) } },
			action("White Mask", "circle") { store.perform { _ = app_layer_add_mask(layer.id, Int32(APP_LAYER_NEW_WHITE_MASK.rawValue)) } },
		]))
	}
	if p.showsAddEffect {
		func effects(_ title: String, _ types: [Int32]) -> UIMenu {
			UIMenu(title: title, options: .displayInline, children: types.map { type in
				action(LayersStore.effectTypeNames[Int(type)], maskEffectSymbol(type)) { store.perform { _ = app_layer_add_effect(layer.id, type) } }
			})
		}
		more.append(UIMenu(title: "Add Effect", image: UIImage(systemName: "wand.and.stars"), children: [
			effects("Sources", LayersStore.effectSources), effects("Filters", LayersStore.effectFilters),
			effects("Generators", LayersStore.effectGenerators),
		]))
	}
	let groups = layers.groups(for: layer)
	if !groups.isEmpty {
		more.append(UIMenu(title: "Move to Group", image: UIImage(systemName: "folder"), children: groups.map { group in
			action(group.name, "folder") { store.perform { _ = app_layer_set_group(layer.id, group.id) } }
		}))
	}
	if layer.kind != .mask && layer.kind != .filter, let parent = layers.layer(layer.parentID), parent.kind == .group {
		more.append(action("Remove from Group", "folder.badge.minus") { store.perform { _ = app_layer_set_group(layer.id, -1) } })
	}
	sections.append(UIMenu(options: .displayInline, children: more))
	var files: [UIMenuElement] = []
	if p.showsSaveSmartMaterial {
		files.append(action("Save as Smart Material…", "square.and.arrow.down") { store.perform { _ = app_layer_save_smart_material(layer.id) } })
	}
	files.append(action("Export…", "square.and.arrow.up") { store.perform { _ = app_layer_export(layer.id) } })
	sections.append(UIMenu(options: .displayInline, children: files))
	var last: [UIMenuElement] = []
	if p.showsFillPaint {
		last.append(action("Fill Paint", "circle.fill") { store.perform { _ = app_layer_fill_paint(layer.id) } })
	}
	if p.showsClear {
		last.append(action("Clear", "eraser", destructive: true) { store.perform { _ = app_layer_clear(layer.id) } })
	}
	last.append(action("Delete", "trash", destructive: true, disabled: !p.canDelete) { store.perform { _ = app_layer_delete(layer.id) } })
	sections.append(UIMenu(options: .displayInline, children: last))
	return UIMenu(children: sections)
}
#endif
