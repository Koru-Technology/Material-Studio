[amake](../tools/amake) is the Material Studio build tool (based on [kmake](https://github.com/Kode/kmake)). It consists of several parts:

`aimage.c`: Converts common image formats like `.jpg` / `.png` / `.hdr` into a custom `.k` format, which is faster to load. `.k` is a simple format which contains an image header and lz4 compressed pixel data. It also turns the `icon.png` file into the app icons.

[`ashader.c`](ashader.md): Compiles a `.glsl` shader into Metal shading language.

`make.c`: Evaluates `project.c` files with the embedded [minic](../sources/libs/minic.c) interpreter, then exports assets and shaders. The api available to `project.c` files is documented in [`amake.h`](../tools/amake/amake.h).

`script.c`: `amake --c <file.c> [args]` runs a C file with minic, e.g. [`extract_locales.c`](../tools/extract_locales.c).

`exporters.c`: Creates the Xcode project for macOS or iOS (the exporters for other platforms are unused).
