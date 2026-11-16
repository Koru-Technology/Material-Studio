import SwiftUI

extension Binding {
	// Shown while the value is set, dismissed clears it: an alert or a sheet about an optional item
	func isPresent<Wrapped>() -> Binding<Bool> where Value == Wrapped? {
		Binding<Bool>(get: { wrappedValue != nil }, set: { if !$0 { wrappedValue = nil } })
	}
}

#if os(iOS)
import UIKit

// A one line text field without Scribble. Around a focused SwiftUI text field, Scribble takes the Pencil's touches on a
// large area (the popover's segmented control, the field's clear button), which can not be tapped with the Pencil then.
// The on-screen keyboard types into it.
struct PlainTextField: UIViewRepresentable {
	let placeholder: String
	@Binding var text: String
	@Binding var focused: Bool

	func makeUIView(context: Context) -> UITextField {
		let field                               = UITextField()
		field.placeholder                       = placeholder
		field.autocorrectionType                = .no
		field.autocapitalizationType            = .none
		field.spellCheckingType                 = .no
		field.returnKeyType                     = .search
		field.font                              = .preferredFont(forTextStyle: .body)
		field.adjustsFontForContentSizeCategory = true
		field.delegate                          = context.coordinator
		field.addTarget(context.coordinator, action: #selector(Coordinator.changed(_:)), for: .editingChanged)
		field.addInteraction(UIScribbleInteraction(delegate: context.coordinator))
		field.setContentHuggingPriority(.defaultLow, for: .horizontal)
		field.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
		field.setContentHuggingPriority(.required, for: .vertical) // One line tall, as a SwiftUI text field
		return field
	}

	// Its own one line height, the width it is offered
	func sizeThatFits(_ proposal: ProposedViewSize, uiView field: UITextField, context: Context) -> CGSize? {
		CGSize(width: proposal.width ?? field.intrinsicContentSize.width, height: field.intrinsicContentSize.height)
	}

	func updateUIView(_ field: UITextField, context: Context) {
		context.coordinator.parent = self
		if field.text != text {
			field.text = text
		}
		if focused != field.isFirstResponder {
			let focus = focused
			DispatchQueue.main.async { // Not while SwiftUI updates the views
				_ = focus ? field.becomeFirstResponder() : field.resignFirstResponder()
			}
		}
	}

	func makeCoordinator() -> Coordinator {
		Coordinator(self)
	}

	final class Coordinator: NSObject, UITextFieldDelegate, UIScribbleInteractionDelegate {
		var parent: PlainTextField

		init(_ parent: PlainTextField) {
			self.parent = parent
		}

		@objc func changed(_ field: UITextField) {
			parent.text = field.text ?? ""
		}

		func textFieldDidBeginEditing(_ field: UITextField) {
			parent.focused = true
		}

		func textFieldDidEndEditing(_ field: UITextField) {
			parent.focused = false
		}

		func textFieldShouldReturn(_ field: UITextField) -> Bool {
			field.resignFirstResponder()
			return false
		}

		func scribbleInteraction(_ interaction: UIScribbleInteraction, shouldBeginAt location: CGPoint) -> Bool {
			false
		}
	}
}
#endif
