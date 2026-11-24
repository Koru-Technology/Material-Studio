import SwiftUI

// Alerts asked for by the panels, shown from the root view. On iPad an alert presented from inside a panel (its own
// hosting controller, sized to its content: PadPanelContent) makes SwiftUI lay the panel out again forever, and the app
// freezes. The root view presents them instead (.rootAlerts()).
@MainActor
@Observable
final class RootAlert {
	static let shared = RootAlert()

	enum Kind {
		case text(placeholder: String, commit: (String) -> Void) // A name to type, the action button commits it
		case confirm(run: () -> Void)                             // A destructive action to confirm
		case notice                                               // Something the user has to know, only OK
	}

	struct Request: Identifiable {
		let id = UUID()
		let title: String
		let action: String
		let message: String?
		let kind: Kind
	}

	var request: Request?
	var text = ""

	// Asks for a name: commit gets it trimmed, never empty
	func text(_ title: String, action: String = "Rename", initial: String, placeholder: String = "Name", message: String? = nil,
	          commit: @escaping (String) -> Void) {
		text    = initial
		request = Request(title: title, action: action, message: message, kind: .text(placeholder: placeholder, commit: commit))
	}

	func confirm(_ title: String, message: String? = nil, action: String = "Delete", run: @escaping () -> Void) {
		request = Request(title: title, action: action, message: message, kind: .confirm(run: run))
	}

	func notice(_ title: String, message: String? = nil) {
		request = Request(title: title, action: "OK", message: message, kind: .notice)
	}

	fileprivate var trimmed: String {
		text.trimmingCharacters(in: .whitespacesAndNewlines)
	}
}

extension View {
	// On the root view: shows what the panels ask for (RootAlert)
	func rootAlerts() -> some View {
		modifier(RootAlertPresenter())
	}
}

private struct RootAlertPresenter: ViewModifier {
	@Bindable private var alert = RootAlert.shared

	private var shown: Binding<Bool> {
		Binding(get: { alert.request != nil }, set: { if !$0 { alert.request = nil } })
	}

	func body(content: Content) -> some View {
		content
			.alert(alert.request?.title ?? "", isPresented: shown, presenting: alert.request) { request in
				switch request.kind {
				case .text(let placeholder, let commit):
					TextField(placeholder, text: $alert.text)
					Button("Cancel", role: .cancel) {}
					Button(request.action) {
						let name = alert.trimmed
						if !name.isEmpty {
							commit(name)
						}
					}
					.keyboardShortcut(.defaultAction)
				case .confirm(let run):
					Button("Cancel", role: .cancel) {}
					Button(request.action, role: .destructive, action: run)
				case .notice:
					Button(request.action, role: .cancel) {}
				}
			} message: { request in
				if let message = request.message {
					Text(message)
				}
			}
	}
}
