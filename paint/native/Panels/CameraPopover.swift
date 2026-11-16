import SwiftUI

#if os(iOS)
// The camera button over the canvas, its popover the old Camera menu
struct CameraButton: View {
	var body: some View {
		Button {
			PadPopovers.shared.toggle("Camera")
		} label: {
			Image(systemName: "camera")
				.frame(width: 44, height: 44)
		}
		.glassEffect(.regular.interactive(), in: .circle)
		.foregroundStyle(.primary)
		.accessibilityLabel("Camera")
		.padPopover("Camera") {
			CameraPopover()
				.frame(width: 320, height: 560)
		}
	}
}
#endif

// Reset, the six views, orbit steps, zoom, field of view, pivot, mode and projection
struct CameraPopover: View {
	private var store = AppStore.shared

	var body: some View {
		NavigationStack {
			Form {
				Section {
					Button("Reset", systemImage: "arrow.counterclockwise") { command(APP_COMMAND_VIEW_RESET) }
					Button("Frame Model", systemImage: "viewfinder") { command(APP_COMMAND_VIEW_FRAME) }
				}
				Section("View") {
					LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 3), spacing: 8) {
						// app_view_face: 0 right, 1 left, 2 back, 3 front, 4 top, 5 bottom
						ForEach([(3, "Front"), (2, "Back"), (4, "Top"), (1, "Left"), (0, "Right"), (5, "Bottom")], id: \.0) { face in
							Button(face.1) {
								store.perform { app_view_face(Int32(face.0)) }
							}
							.buttonStyle(.bordered)
							.frame(maxWidth: .infinity)
						}
					}
				}
				Section("Orbit") {
					HStack {
						orbit("arrow.left", "Orbit Left", -.pi / 12, 0)
						orbit("arrow.right", "Orbit Right", .pi / 12, 0)
						orbit("arrow.up", "Orbit Up", 0, -.pi / 12)
						orbit("arrow.down", "Orbit Down", 0, .pi / 12)
						Button("Opposite", systemImage: "arrow.trianglehead.2.clockwise.rotate.90") { command(APP_COMMAND_ORBIT_OPPOSITE) }
							.labelStyle(.iconOnly)
							.buttonStyle(.bordered)
					}
					HStack {
						Button("Zoom In", systemImage: "plus.magnifyingglass") { command(APP_COMMAND_ZOOM_IN) }
						Spacer()
						Button("Zoom Out", systemImage: "minus.magnifyingglass") { command(APP_COMMAND_ZOOM_OUT) }
					}
					.buttonStyle(.borderless)
				}
				Section("Field of View") {
					TipSlider(title: "Field of View", value: store.cameraFov, range: 0.3...1.4,
					          format: { "\(Int(($0 * 180 / .pi).rounded()))°" }) { app_set_camera_fov($0) }
						.disabled(store.cameraType == 1)
				}
				Section {
					picker("Pivot", store.cameraPivot, ["Cursor", "Center"]) { app_set_camera_pivot($0) }
					picker("Mode", store.cameraControls, ["Orbit", "Rotate", "Fly"]) { app_set_camera_controls($0) }
					picker("Type", store.cameraType, ["Perspective", "Orthographic"]) { app_set_camera_type($0) }
				}
			}
			.formStyle(.grouped)
			.navigationTitle("Camera")
#if os(iOS)
			.navigationBarTitleDisplayMode(.inline)
#endif
		}
	}

	private func command(_ c: app_command_t) {
		store.perform { app_command(Int32(c.rawValue)) }
	}

	private func orbit(_ symbol: String, _ title: String, _ x: Float, _ y: Float) -> some View {
		Button(title, systemImage: symbol) {
			store.perform { app_view_orbit(x, y) }
		}
		.labelStyle(.iconOnly)
		.buttonStyle(.bordered)
	}

	private func picker(_ title: String, _ value: Int32, _ options: [String], set: @escaping (Int32) -> Void) -> some View {
		Picker(title, selection: Binding(get: { value }, set: { v in store.perform { set(v) } })) {
			ForEach(options.indices, id: \.self) { i in
				Text(options[i]).tag(Int32(i))
			}
		}
	}
}
