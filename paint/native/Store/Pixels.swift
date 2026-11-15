import CoreGraphics
import Foundation

// Reads the pixels of an app_api thumbnail function (app_layer_thumbnail, app_material_thumbnail, ...) into an image.
// Those write RGBA8 rows top to bottom into the buffer given and return the byte count written: 0 none, -1 none to come.
final class PixelReader {
	private var buffer: [UInt8]

	init(capacity: Int = 0) {
		buffer = [UInt8](repeating: 0, count: capacity)
	}

	// The byte count read and the image when there is one. need: the buffer grows to that first. adjust: changes the
	// pixels before the image is made of them.
	func read(need: Int = 0, premultiplied: Bool, adjust: ((inout [UInt8]) -> Void)? = nil,
	          _ fn: (UnsafeMutablePointer<UInt8>?, Int32, inout Int32, inout Int32) -> Int32) -> (bytes: Int32, image: CGImage?) {
		if buffer.count < need {
			buffer = [UInt8](repeating: 0, count: need)
		}
		var w: Int32 = 0
		var h: Int32 = 0
		let bytes = buffer.withUnsafeMutableBufferPointer {
			fn($0.baseAddress, Int32($0.count), &w, &h)
		}
		guard bytes > 0 else {
			return (bytes, nil)
		}
		var pixels = Array(buffer[0..<Int(bytes)])
		adjust?(&pixels)
		return (bytes, makeImage(pixels, width: Int(w), height: Int(h), premultiplied: premultiplied))
	}
}

// RGBA8 rows top to bottom
func makeImage(_ bytes: [UInt8], width: Int, height: Int, premultiplied: Bool = true) -> CGImage? {
	guard let provider = CGDataProvider(data: Data(bytes) as CFData), let space = CGColorSpace(name: CGColorSpace.sRGB) else {
		return nil
	}
	let alpha = premultiplied ? CGImageAlphaInfo.premultipliedLast : CGImageAlphaInfo.last
	return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4, space: space,
	               bitmapInfo: CGBitmapInfo(rawValue: alpha.rawValue), provider: provider, decode: nil, shouldInterpolate: true,
	               intent: .defaultIntent)
}
