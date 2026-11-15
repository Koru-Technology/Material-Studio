import Darwin

// Proves Swift is compiled, linked and callable from C, and that C is visible to Swift through the bridging header.
@_cdecl("native_hello")
nonisolated public func nativeHello() {
	fputs("[native] Swift linked, app_api version \(app_api_version())\n", stderr)
}
