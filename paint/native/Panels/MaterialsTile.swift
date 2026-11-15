import SwiftUI

// A material in the popover's grids: its sphere over a soft tile, the name under it on up to two lines
struct MaterialTile<Overlay: View>: View {
	let image: CGImage?
	let name: String
	var selected = false
	var loading  = false // No picture yet, it is coming
	var dimmed   = false
	@ViewBuilder var overlay: () -> Overlay

	var body: some View {
		VStack(spacing: 4) {
			ZStack {
				RoundedRectangle(cornerRadius: 12, style: .continuous)
					.fill(selected ? AnyShapeStyle(Color.accentColor.opacity(0.18)) : AnyShapeStyle(.fill.quaternary))
				if let image {
					Image(decorative: image, scale: 1)
						.resizable()
						.interpolation(.high)
						.aspectRatio(contentMode: .fit)
						.padding(4)
						.opacity(dimmed ? 0.45 : 1)
						.transition(.opacity)
				}
				else if loading {
					ProgressView()
						.controlSize(.small)
				}
				else {
					Image(systemName: "circle.dotted")
						.font(.title)
						.foregroundStyle(.tertiary)
				}
				overlay()
			}
			.aspectRatio(1, contentMode: .fit)
			.overlay {
				if selected {
					RoundedRectangle(cornerRadius: 12, style: .continuous)
						.strokeBorder(Color.accentColor, lineWidth: 2)
				}
			}
			.animation(.easeOut(duration: 0.15), value: image == nil)
			Text(name)
				.font(.caption2)
				.fontWeight(selected ? .semibold : .regular)
				.foregroundStyle(selected ? .primary : .secondary)
				.multilineTextAlignment(.center)
				.lineLimit(2, reservesSpace: true)
				.truncationMode(.tail)
		}
		.contentShape(.rect)
	}
}

extension MaterialTile where Overlay == EmptyView {
	init(image: CGImage?, name: String, selected: Bool = false, loading: Bool = false, dimmed: Bool = false) {
		self.init(image: image, name: name, selected: selected, loading: loading, dimmed: dimmed) {
			EmptyView()
		}
	}
}

// Download progress over a cloud item: a ring filling clockwise around an arrow
struct DownloadRing: View {
	let progress: Float

	var body: some View {
		ZStack {
			Circle()
				.fill(.black.opacity(0.35))
			Circle()
				.stroke(.white.opacity(0.3), lineWidth: 3)
				.padding(3)
			Circle()
				.trim(from: 0, to: CGFloat(max(0.02, progress)))
				.stroke(.white, style: StrokeStyle(lineWidth: 3, lineCap: .round))
				.rotationEffect(.degrees(-90))
				.padding(3)
				.animation(.linear(duration: 0.2), value: progress)
			Image(systemName: "arrow.down")
				.font(.caption.bold())
				.foregroundStyle(.white)
		}
		.frame(width: 34, height: 34)
	}
}

let materialColumns = [GridItem(.adaptive(minimum: 76, maximum: 110), spacing: 10, alignment: .top)]
