import SwiftUI

// Picking the parts of an ID Selection (app_api_masks.h, app_id_pick): the ID map's parts listed beside the canvas, a tap
// on a row or on the model picks or unpicks a part, Confirm sets them, Cancel leaves the effect as it was.
@Observable
final class IDPickStore: ChangeOnly {
	static let shared = IDPickStore()

	struct Part: Identifiable, Equatable {
		let id: Int32
		let name: String
		let color: Color
		let picked: Bool
	}

	private(set) var active = false
	private(set) var parts: [Part] = []
	private(set) var picked: Int32 = 0
	private(set) var max: Int32    = 0
	@ObservationIgnored private var version: UInt32 = 0
#if os(iOS)
	@ObservationIgnored private var panelBefore: String? // The panel open when picking began, open again after
#endif

	func refresh() {
		var p = app_id_pick_t()
		app_id_pick(&p)
		if p.active != active {
#if os(iOS)
			if p.active {
				panelBefore = PadPopovers.shared.shown
				PadPopovers.shared.close() // The model is picked on: no panel over it
			}
			else if let panel = panelBefore {
				panelBefore = nil
				PadPopovers.shared.toggle(panel)
			}
#endif
			active = p.active
		}
		guard p.version != version else {
			return
		}
		version = p.version
		update(\.picked, p.picked)
		update(\.max, p.max)
		update(\.parts, (0 ..< p.parts).map { i in
			let c = app_id_part_color(i)
			return Part(id: i, name: String(cString: app_id_part_name(i)),
			            color: Color(red: Double(c >> 16 & 0xff) / 255, green: Double(c >> 8 & 0xff) / 255, blue: Double(c & 0xff) / 255),
			            picked: app_id_part_picked(i))
		})
	}
}

struct IDPartsPanel: View {
	private var store = IDPickStore.shared

	var body: some View {
		GlassEffectContainer(spacing: 12) {
			VStack(spacing: 12) {
				list
				buttons
			}
		}
		.frame(width: 260)
	}

	private var list: some View {
		VStack(alignment: .leading, spacing: 0) {
			HStack {
				Text("Parts")
					.font(.headline)
				Spacer()
				Text("\(store.picked) of \(store.max)")
					.foregroundStyle(.secondary)
					.monospacedDigit()
			}
			.padding(.horizontal, 16)
			.padding(.vertical, 12)
			ScrollView {
				VStack(spacing: 2) {
					ForEach(store.parts) { part in
						row(part)
					}
				}
				.padding(.horizontal, 8)
				.padding(.bottom, 8)
			}
			.scrollBounceBehavior(.basedOnSize)
			.frame(maxHeight: 420)
			.fixedSize(horizontal: false, vertical: true)
		}
		.glassEffect(.regular, in: .rect(cornerRadius: 22))
	}

	private func row(_ part: IDPickStore.Part) -> some View {
		Button {
			AppStore.shared.perform { app_id_part_toggle(part.id) }
		} label: {
			HStack(spacing: 10) {
				RoundedRectangle(cornerRadius: 5)
					.fill(part.color)
					.frame(width: 22, height: 22)
					.overlay {
						RoundedRectangle(cornerRadius: 5)
							.strokeBorder(.white.opacity(0.25), lineWidth: 1)
					}
				Text(part.name)
					.lineLimit(1)
					.truncationMode(.middle)
				Spacer(minLength: 4)
				Image(systemName: part.picked ? "checkmark.circle.fill" : "circle")
					.foregroundStyle(part.picked ? Color.accentColor : .secondary)
					.font(.title3)
			}
			.padding(.horizontal, 8)
			.frame(height: 44)
			.background(part.picked ? Color.accentColor.opacity(0.15) : .clear, in: .rect(cornerRadius: 12))
			.contentShape(.rect)
		}
		.buttonStyle(.plain)
		.disabled(!part.picked && store.picked >= store.max)
		.accessibilityAddTraits(part.picked ? .isSelected : [])
	}

	private var buttons: some View {
		HStack(spacing: 12) {
			Button {
				AppStore.shared.perform { app_id_pick_cancel() }
			} label: {
				label("Cancel")
			}
			.buttonStyle(.plain)
			.glassEffect(.regular.interactive(), in: .capsule)
			.keyboardShortcut(.cancelAction)
			Button {
				AppStore.shared.perform { app_id_pick_confirm() }
			} label: {
				label("Confirm")
					.foregroundStyle(.white)
			}
			.buttonStyle(.plain)
			.glassEffect(.regular.tint(.accentColor).interactive(), in: .capsule)
			.keyboardShortcut(.defaultAction)
		}
	}

	private func label(_ text: String) -> some View {
		Text(text)
			.font(.headline)
			.frame(maxWidth: .infinity)
			.frame(height: 44)
			.contentShape(.capsule)
	}
}
