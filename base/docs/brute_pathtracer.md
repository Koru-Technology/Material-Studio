Brute Pathtracer is a custom built path-tracing engine used in armorcore which trades correctness for speed. It is powered by hardware accelerated ray-tracing on Metal.

In Material Studio, it is used for rendering the path-traced viewport mode and for ray-traced baking features.

By default, the path-tracer supports 64 unique objects and up to 1024 instances. It has two modes - fast, which has most of the features disabled, and quality, which has features like translucency and emission enabled.

- [`shaders/raytrace`](../shaders/raytrace)
- [`render_path_raytrace.c`](../../paint/sources/render/render_path_raytrace.c)
- [`render_path_raytrace_bake.c`](../../paint/sources/render/render_path_raytrace_bake.c)
- [`iron_gpu.h`](../sources/iron_gpu.h)
