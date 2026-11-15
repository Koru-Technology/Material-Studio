import SwiftUI

extension Binding {
	// Shown while the value is set, dismissed clears it: an alert or a sheet about an optional item
	func isPresent<Wrapped>() -> Binding<Bool> where Value == Wrapped? {
		Binding<Bool>(get: { wrappedValue != nil }, set: { if !$0 { wrappedValue = nil } })
	}
}
