
## ashader

Compiles a `.glsl` shader into Metal shading language, using [glslang](https://github.com/KhronosGroup/glslang) (GLSL to SPIR-V) and [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (SPIR-V to MSL), both vendored in `sources/libs`. The same code (`sources/iron_glsl.c`) compiles the shaders the app generates at runtime.

A shader is one GLSL 450 file holding both stages: code for one stage goes in `#ifdef VERT` / `#ifdef FRAG`. `VARYING` is `out` in the vertex stage and `in` in the fragment stage, `VARYING_NAME` is `vout` / `vin`, so one varyings block can be declared for both stages:

```glsl
#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 tint;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

layout(location = 0) VARYING Varyings {
	vec2 tex;
} VARYING_NAME;

#ifdef VERT
layout(location = 0) in vec2 in_pos;

void main() {
	vout.tex    = in_pos * 0.5 + 0.5;
	gl_Position = vec4(in_pos, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = texture(sampler2D(tex, sampler_linear), vin.tex) * constants.tint;
}
#endif
```

Bindings follow the Metal backend: binding 0 is the constants block, binding 1 the sampler, binding 2 + i texture i. Every shader declares the constants block and the sampler. Constant offsets are the engine's (`shader_context_type_size` / `shader_context_type_pad` in `engine.c`): give a `vec3` followed by a smaller type an explicit `layout(offset = N)`.

`amake --ashader metal <from.glsl> <to> [-DNAME...]` writes `<to>.vert.metal` and `<to>.frag.metal`.
