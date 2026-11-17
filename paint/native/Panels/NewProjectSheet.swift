import SwiftUI

// The template a new project starts on (app_api.h): the engine's mesh and shape ids, shown by name
struct ProjectTemplate: Identifiable {
	let id: String
	let index: Int32
	let name: String
	let symbol: String
}

// Names and symbols by id, in the order the sheet shows them. Ids not listed (a mesh added to data/meshes) follow,
// named from the id.
private let knownTemplates: [(id: String, name: String, symbol: String)] = [
	("cube_bevel", "Beveled Cube", "cube.fill"),
	("cube", "Cube", "cube"),
	("cube_shared_uvs", "Cube with Shared UVs", "cube.transparent"),
	("sphere", "Sphere", "circle"),
	("plane", "Plane", "square"),
	("cylinder", "Cylinder", "cylinder"),
	("cone", "Cone", "cone"),
	("capsule", "Capsule", "capsule.portrait"),
	("torus", "Torus", "torus"),
	("empty", "Empty", "square.dashed"),
]

@MainActor func projectTemplates() -> [ProjectTemplate] {
	var byId: [String: Int32] = [:]
	var ids: [String]         = []
	for i in 0..<app_template_count() {
		let id = String(cString: app_template_id(i))
		if byId[id] == nil { // Listed once
			byId[id] = i
			ids.append(id)
		}
	}
	var list = knownTemplates.compactMap { t in
		byId[t.id].map { ProjectTemplate(id: t.id, index: $0, name: t.name, symbol: t.symbol) }
	}
	for id in ids where !knownTemplates.contains(where: { $0.id == id }) {
		list.append(ProjectTemplate(id: id, index: byId[id]!, name: id.replacingOccurrences(of: "_", with: " ").capitalized, symbol: "cube"))
	}
	return list
}

struct NewProjectSheet: View {
	@Environment(\.dismiss) private var dismiss
	@State private var templates: [ProjectTemplate] = []
	@State private var selected: Int32 = -1

	var body: some View {
		NavigationStack {
			ScrollView {
				LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), spacing: 16)], spacing: 16) {
					ForEach(templates) { t in
						Button {
							selected = t.index
						} label: {
							VStack(spacing: 8) {
								Image(systemName: t.symbol)
									.font(.system(size: 40, weight: .light))
									.frame(maxWidth: .infinity)
									.frame(height: 88)
									.background(.fill.tertiary, in: .rect(cornerRadius: 14))
									.overlay {
										RoundedRectangle(cornerRadius: 14)
											.strokeBorder(Color.accentColor, lineWidth: selected == t.index ? 3 : 0)
									}
								Text(t.name)
									.font(.callout)
									.multilineTextAlignment(.center)
									.lineLimit(2, reservesSpace: true)
							}
							.contentShape(.rect)
						}
						.buttonStyle(.plain)
						.accessibilityLabel(t.name)
						.accessibilityAddTraits(selected == t.index ? .isSelected : [])
					}
				}
				.padding(20)
			}
			.navigationTitle("New Project")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
			.toolbar {
				ToolbarItem(placement: .cancellationAction) {
					Button("Cancel") {
						dismiss()
					}
				}
				ToolbarItem(placement: .confirmationAction) {
					Button("Create") {
						let index = selected
						dismiss()
						AppStore.shared.perform { app_project_new_from_template(index) }
					}
					.disabled(selected < 0)
				}
			}
		}
#if os(macOS)
		.frame(minWidth: 560, minHeight: 420)
#endif
		.task(id: AppStore.shared.ready) { // The engine lists them once it started
			templates = projectTemplates()
			let last  = app_template_selected()
			selected  = templates.contains { $0.index == last } ? last : templates.first?.index ?? -1
		}
	}
}
