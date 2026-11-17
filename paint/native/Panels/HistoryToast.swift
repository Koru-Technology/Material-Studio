import SwiftUI

// "Brush Undone", "New Layer Redone": a short notice when the user undoes or redoes a step, from any source (buttons,
// shortcuts, the pencil, the quick menu). Undos the app makes itself, a stroke a second finger cancels, show none.
struct HistoryToast: Equatable {
	let id: UInt32
	let name: String
	let redo: Bool

	var text: String {
		let done = redo ? "Redone" : "Undone"
		return name.isEmpty ? done : "\(name) \(done)"
	}
}

struct HistoryToastView: View {
	private var store = AppStore.shared
	@State private var shown: HistoryToast?
	@State private var hide: Task<Void, Never>?

	var body: some View {
		VStack {
			if let toast = shown {
				Text(toast.text)
					.font(.callout.weight(.medium))
					.padding(.horizontal, 14)
					.padding(.vertical, 8)
					.background(.regularMaterial, in: Capsule())
					.shadow(color: .black.opacity(0.15), radius: 6, y: 2)
					.transition(.opacity.combined(with: .scale(scale: 0.96)))
					.id(toast.id)
			}
			Spacer()
		}
		.padding(.top, 12)
		.frame(maxWidth: .infinity)
		.allowsHitTesting(false)
		.onChange(of: store.historyToast) { _, toast in
			guard let toast else {
				return
			}
			withAnimation(.easeOut(duration: 0.12)) {
				shown = toast
			}
			hide?.cancel()
			hide = Task { @MainActor in
				try? await Task.sleep(for: .milliseconds(1200))
				guard !Task.isCancelled else {
					return
				}
				withAnimation(.easeIn(duration: 0.25)) {
					shown = nil
				}
			}
		}
	}
}
