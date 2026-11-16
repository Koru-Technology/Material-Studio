#include "../global.h"

// The dab resolve pass: the dabs of a paint executor flush (paint_exec.c) from their screen points to world space,
// before their draws. A texel of texpaint_dabs_resolved a fragment, the layout of PAINT_DAB_TEXELS (functions.h). It reads
// the geometry-only paint depth, gbuffer0 for the normal and gbuffer2 for the uv, as the paint shaders read them before.

static char *make_dab_resolve_frag = "\
vec3 dab_dequantize(vec3 q) {\n\
	vec3 o = constants.aabb_min.xyz + q * (constants.aabb_size.xyz / " PAINT_DAB_QUANT_S ");\n\
	return (constants.obj * vec4(o, 1.0)).xyz;\n\
}\n\
float dab_depth(vec2 s) {\n\
	return textureLod(sampler2D(gbufferD, sampler_linear), s, 0.0).r;\n\
}\n\
float dab_off(vec2 s, float mode) {\n\
	float depth = dab_depth(s);\n\
	if (mode > 0.5) {\n\
		vec4 origin_clip = constants.VP * vec4(0.0, 0.0, 0.0, 1.0);\n\
		return (depth >= 1.0 && origin_clip.w > 0.0) ? 1.0 : 0.0;\n\
	}\n\
	return depth >= 1.0 ? 1.0 : 0.0;\n\
}\n\
vec4 dab_resolve(vec2 s, float mode) {\n\
	float depth = dab_depth(s);\n\
	bool on_mesh = depth < 1.0;\n\
	if (mode > 0.5) {\n\
		vec4 origin_clip = constants.VP * vec4(0.0, 0.0, 0.0, 1.0);\n\
		if (depth >= 1.0 && origin_clip.w > 0.0) { depth = origin_clip.z / origin_clip.w; }\n\
	}\n\
	vec4 w = vec4(vec2(s.x, 1.0 - s.y) * 2.0 - 1.0, depth, 1.0);\n\
	w = constants.invVP * w;\n\
	w.xyz = w.xyz / w.w;\n\
	if (on_mesh) {\n\
		vec3 o = (constants.obj_inv * vec4(w.xyz, 1.0)).xyz;\n\
		vec3 q = floor(clamp((o - constants.aabb_min.xyz) / constants.aabb_size.xyz, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)) * " PAINT_DAB_QUANT_S " + vec3(0.5, 0.5, 0.5));\n\
		w.xyz = dab_dequantize(q);\n\
	}\n\
	return w;\n\
}\n\
vec2 dab_uv(vec2 s) {\n\
	vec4 pg = texelFetch(sampler2D(gbuffer2, sampler_linear), ivec2(uint(s.x * constants.gbuffer_size.x), uint(s.y * constants.gbuffer_size.y)), 0);\n\
	return pg.ba + pg.rg;\n\
}\n\
#ifdef FRAG\n\
layout(location = 0) out vec4 frag_out;\n\
void main() {\n\
	ivec2 fc = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));\n\
	int t = fc.x % " PAINT_DAB_TEXELS_S ";\n\
	ivec2 base = ivec2(fc.x - t, fc.y);\n\
	vec4 i0 = texelFetch(sampler2D(dabsin, sampler_linear), base, 0);\n\
	float mode = texelFetch(sampler2D(dabsin, sampler_linear), base + ivec2(1, 0), 0).x;\n\
	if (t == 0) { frag_out = dab_resolve(i0.xy, mode); }\n\
	else if (t == 1) { frag_out = dab_resolve(i0.zw, mode); }\n\
	else if (t == 2) {\n\
		vec2 g0 = textureLod(sampler2D(gbuffer0, sampler_linear), i0.xy, 0.0).rg;\n\
		g0 = floor(clamp(g0, vec2(-1.0, -1.0), vec2(1.0, 1.0)) * 32767.0 + vec2(0.5, 0.5)) / 32767.0;\n\
		frag_out = vec4(g0, dab_off(i0.xy, mode), dab_off(i0.zw, mode));\n\
	}\n\
	else if (t == 4) { frag_out = i0; }\n\
	else if (t == 5) { frag_out = vec4(dab_uv(i0.xy), dab_uv(i0.zw)); }\n\
	else { frag_out = texelFetch(sampler2D(dabsin, sampler_linear), fc, 0); }\n\
}\n\
#endif\n";

static shader_context_t *make_dab_resolve_scon = NULL;

static void make_dab_resolve_const(shader_context_t *scon, char *name, char *type, char *link) {
	any_array_push(scon->constants, ALLOC_INIT(shader_const_t, {.name = name, .type = type, .link = link}));
}

static void make_dab_resolve_init() {
	shader_context_t *scon = ALLOC_INIT(shader_context_t, {
	                                                          .name               = "dab_resolve",
	                                                          .depth_write        = false,
	                                                          .compare_mode       = "always",
	                                                          .cull_mode          = "none",
	                                                          .shader_from_source = true,
	                                                          .color_attachments  = any_array_create_from_raw((void *[]){"RGBA128"}, 1),
	                                                          .depth_attachment   = "NONE",
	                                                          .vertex_elements    = any_array_create_from_raw(
                                                                  (void *[]){ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "float2"})}, 1),
	                                                          .constants     = any_array_create_from_raw((void *[]){}, 0),
	                                                          .texture_units = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  ALLOC_INIT(tex_unit_t, {.name = "dabsin", .link = "_texpaintdabsin"}),
	                                                                  ALLOC_INIT(tex_unit_t, {.name = "gbufferD"}),
	                                                                  ALLOC_INIT(tex_unit_t, {.name = "gbuffer0"}),
	                                                                  ALLOC_INIT(tex_unit_t, {.name = "gbuffer2"}),
	                                                              },
	                                                              4),
	                                                      });
	make_dab_resolve_const(scon, "invVP", "float4x4", "_inv_view_proj_matrix");
	make_dab_resolve_const(scon, "VP", "float4x4", "_view_proj_matrix");
	make_dab_resolve_const(scon, "obj", "float4x4", "_paint_dab_obj");
	make_dab_resolve_const(scon, "obj_inv", "float4x4", "_paint_dab_obj_inv");
	make_dab_resolve_const(scon, "aabb_min", "float4", "_paint_dab_aabb_min");
	make_dab_resolve_const(scon, "aabb_size", "float4", "_paint_dab_aabb_size");
	make_dab_resolve_const(scon, "gbuffer_size", "float2", "_gbuffer_size");

	// The constants block, offsets as the engine computes them (render_pass.c)
	char *consts = "";
	i32   offset = 0;
	for (i32 i = 0; i < scon->constants->length; ++i) {
		shader_const_t *c    = scon->constants->buffer[i];
		i32             size = shader_context_type_size(c->type);
		offset += shader_context_type_pad(offset, size);
		char *t = string_equals(c->type, "float4x4") ? "mat4" : string_equals(c->type, "float2") ? "vec2" : "vec4";
		consts  = string("%s\tlayout(offset = %d) %s %s;\n", consts, offset, t, c->name);
		offset += size;
	}
	char *glsl = string("#version 450\n\
layout(set = 0, binding = 0, std140) uniform Constants {\n%s} constants;\n\
layout(set = 0, binding = 1) uniform sampler sampler_linear;\n\
layout(set = 0, binding = 2) uniform texture2D dabsin;\n\
layout(set = 0, binding = 3) uniform texture2D gbufferD;\n\
layout(set = 0, binding = 4) uniform texture2D gbuffer0;\n\
layout(set = 0, binding = 5) uniform texture2D gbuffer2;\n\
#ifdef VERT\n\
layout(location = 0) in vec2 in_pos;\n\
void main() {\n\
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);\n\
}\n\
#endif\n\
%s",
	                    consts, make_dab_resolve_frag);

	scon->_ = calloc(1, sizeof(shader_context_runtime_t));
	gpu_create_shaders_from_glsl(glsl, &scon->vertex_shader, &scon->fragment_shader, &scon->_->vertex_shader_size, &scon->_->fragment_shader_size);
	shader_context_load(scon);
	make_dab_resolve_scon = scon;
	if (_render_path_cached_shader_contexts == NULL) {
		_render_path_cached_shader_contexts = any_map_create();
	}
	any_map_set(_render_path_cached_shader_contexts, "_dab_resolve", ALLOC_INIT(cached_shader_context_t, {.context = scon}));
	render_path_add_target("texpaint_dabs_resolved", PAINT_DAB_ROW * PAINT_DAB_TEXELS, PAINT_DAB_MAX / PAINT_DAB_ROW, "RGBA128", 1.0);
}

// Resolves count dabs of the executor's input (paint_exec_dabs_in) into texpaint_dabs_resolved
void make_dab_resolve_run(i32 count) {
	if (make_dab_resolve_scon == NULL) {
		make_dab_resolve_init();
	}
	i32 rows = (count + PAINT_DAB_ROW - 1) / PAINT_DAB_ROW;
	render_path_set_target("texpaint_dabs_resolved", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_scissor(0, 0, PAINT_DAB_ROW * PAINT_DAB_TEXELS, rows);
	render_path_bind_target(render_path_base_has_paint_depth() ? "main_paint" : "main", "gbufferD");
	render_path_bind_target("gbuffer0", "gbuffer0");
	render_path_bind_target("gbuffer2", "gbuffer2");
	render_path_draw_shader_keep_target("_dab_resolve");
	gpu_disable_scissor();
	render_path_end();
}

// Texel t of resolved dab k, k a GLSL int expression
char *make_brush_dab(char *k, i32 t) {
	return string_tmp("texelFetch(sampler2D(texpaintdabs, sampler_linear), ivec2(uint((%s) %% %d * %d + %d), uint((%s) / %d)), 0)", k, PAINT_DAB_ROW,
	                  PAINT_DAB_TEXELS, t, k, PAINT_DAB_ROW);
}
