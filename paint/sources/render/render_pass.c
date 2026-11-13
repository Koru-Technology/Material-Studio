#include "../global.h"

// Fullscreen passes created by scripts. The frag code (GLSL) writes vec4 output, blended over the target by its alpha.
// It reads tex (screen uv), depth, wposition (world position from depth) and constants.<name>: eye, time, or a vec4
// set with render_pass_set_vec4.

static any_array_t *render_pass_list   = NULL;
static any_map_t   *render_pass_values = NULL; // Constant link -> vec4_t *
static i32          render_pass_ids    = 0;

static char *render_pass_builtins[][3] = {
    {"invVP", "float4x4", "_inv_view_proj_matrix"},  {"eye", "float3", "_camera_pos"}, {"eye_look", "float3", "_camera_look"},
    {"camera_proj", "float2", "_camera_plane_proj"}, {"time", "float", "_time"},
};

static i32 render_pass_builtin(char *name) {
	for (i32 i = 0; i < sizeof(render_pass_builtins) / sizeof(render_pass_builtins[0]); ++i) {
		if (string_equals(render_pass_builtins[i][0], name)) {
			return i;
		}
	}
	return -1;
}

static void render_pass_add_const(shader_context_t *scon, char *name, char *type, char *link) {
	shader_const_t *c = ALLOC_INIT(shader_const_t, {.name = string_copy(name), .type = type, .link = string_copy(link)});
	any_array_push(scon->constants, c);
}

render_pass_t *render_pass_create(char *frag, render_pass_stage_t stage) {
	render_pass_t *p = ALLOC_INIT(render_pass_t, {.stage = stage, .id = render_pass_ids++});

	shader_context_t *scon =
	    ALLOC_INIT(shader_context_t,
	               {
	                   .name                    = "pass",
	                   .depth_write             = false,
	                   .compare_mode            = "always",
	                   .cull_mode               = "none",
	                   .shader_from_source      = true,
	                   .blend_source            = "source_alpha",
	                   .blend_destination       = "inverse_source_alpha",
	                   .alpha_blend_source      = "blend_zero",
	                   .alpha_blend_destination = "blend_one",
	                   .color_attachments       = any_array_create_from_raw((void *[]){"RGBA64"}, 1),
	                   .depth_attachment        = "NONE",
	                   .vertex_elements         = any_array_create_from_raw((void *[]){ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "float2"})}, 1),
	                   .constants               = any_array_create_from_raw((void *[]){}, 0),
	                   .texture_units           = any_array_create_from_raw((void *[]){ALLOC_INIT(tex_unit_t, {.name = "gbufferD"})}, 1),
	               });

	// The wrapper reads these, the rest only when the code does
	for (i32 i = 0; i < 4; ++i) {
		render_pass_add_const(scon, render_pass_builtins[i][0], render_pass_builtins[i][1], render_pass_builtins[i][2]);
	}
	char *s = frag;
	while ((s = strstr(s, "constants.")) != NULL) {
		s += 10;
		i32 len = 0;
		while (isalnum(s[len]) || s[len] == '_') {
			len++;
		}
		char *name  = substring(s, 0, len);
		bool  found = false;
		for (i32 i = 0; i < scon->constants->length; ++i) {
			found = found || string_equals(((shader_const_t *)scon->constants->buffer[i])->name, name);
		}
		if (len == 0 || found) {
			continue;
		}
		i32 b = render_pass_builtin(name);
		if (b >= 0) {
			render_pass_add_const(scon, name, render_pass_builtins[b][1], render_pass_builtins[b][2]);
		}
		else {
			render_pass_add_const(scon, name, "float4", string_tmp("_pass%d_%s", p->id, name));
		}
	}

	// The constants block, offsets as the engine computes them
	char *consts = "";
	i32   offset = 0;
	for (i32 i = 0; i < scon->constants->length; ++i) {
		shader_const_t *c    = scon->constants->buffer[i];
		i32             size = shader_context_type_size(c->type);
		offset += shader_context_type_pad(offset, size);
		char *t = string_equals(c->type, "float4x4") ? "mat4" : string_equals(c->type, "float3") ? "vec3" : string_equals(c->type, "float2") ? "vec2"
		        : string_equals(c->type, "float4")   ? "vec4"
		                                             : c->type;
		consts  = string("%s\tlayout(offset = %d) %s %s;\n", consts, offset, t, c->name);
		offset += size;
	}

	// The code writes output, a GLSL keyword
	buffer_t code;
	string_buffer_init(&code);
	node_shader_rename(&code, frag, "output", "frag_out");

	char *glsl = string("#version 450\n\
layout(set = 0, binding = 0, std140) uniform Constants {\n%s} constants;\n\
layout(set = 0, binding = 1) uniform sampler sampler_linear;\n\
layout(set = 0, binding = 2) uniform texture2D gbufferD;\n\
layout(location = 0) VARYING Varyings { vec2 tex; vec3 view_ray; } VARYING_NAME;\n\
#ifdef VERT\n\
layout(location = 0) in vec2 in_pos;\n\
void main() {\n\
	vout.tex = in_pos.xy * 0.5 + 0.5;\n\
	vout.tex.y = 1.0 - vout.tex.y;\n\
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);\n\
	vec4 v = constants.invVP * vec4(in_pos.xy, 1.0, 1.0);\n\
	vout.view_ray = v.xyz / v.w - constants.eye;\n\
}\n\
#endif\n\
#ifdef FRAG\n\
layout(location = 0) out vec4 frag_out;\n\
void main() {\n\
	vec2 tex = vin.tex;\n\
	float depth = textureLod(sampler2D(gbufferD, sampler_linear), tex, 0.0).r;\n\
	vec3 view_ray = normalize(vin.view_ray);\n\
	float linear_depth = constants.camera_proj.y / (depth - constants.camera_proj.x);\n\
	vec3 wposition = constants.eye + view_ray * (linear_depth / dot(constants.eye_look, view_ray));\n\
	frag_out = vec4(0.0, 0.0, 0.0, 0.0);\n\
%s\n\
}\n\
#endif\n",
	                    consts, string_buffer_get(&code));
	string_buffer_free(&code);

	scon->_ = calloc(1, sizeof(shader_context_runtime_t));
	gpu_create_shaders_from_glsl(glsl, &scon->vertex_shader, &scon->fragment_shader, &scon->_->vertex_shader_size, &scon->_->fragment_shader_size);
	shader_context_load(scon);

	p->scon                     = scon;
	p->handle                   = string("_pass%d", p->id);
	cached_shader_context_t *cc = ALLOC_INIT(cached_shader_context_t, {.context = scon});
	if (_render_path_cached_shader_contexts == NULL) {
		_render_path_cached_shader_contexts = any_map_create();
	}
	any_map_set(_render_path_cached_shader_contexts, p->handle, cc);

	if (render_pass_list == NULL) {
		render_pass_list   = any_array_create_from_raw((void *[]){}, 0);
		render_pass_values = any_map_create();
	}
	any_array_push(render_pass_list, p);
	g_context->ddirty = 2;
	return p;
}

void render_pass_set_vec4(render_pass_t *p, char *name, f32 x, f32 y, f32 z, f32 w) {
	char   *link = string_tmp("_pass%d_%s", p->id, name);
	vec4_t *v    = any_map_get(render_pass_values, link);
	if (v == NULL) {
		v = ALLOC_INIT(vec4_t, {0});
		any_map_set(render_pass_values, string_copy(link), v);
	}
	if (v->x != x || v->y != y || v->z != z || v->w != w) {
		*v                = (vec4_t){x, y, z, w};
		g_context->ddirty = 2;
	}
}

// NAN when the link is not a pass constant
vec4_t render_pass_get_vec4(char *link) {
	if (render_pass_values == NULL || !starts_with(link, "_pass")) {
		return vec4_nan();
	}
	vec4_t *v = any_map_get(render_pass_values, link);
	return v != NULL ? *v : (vec4_t){0.0, 0.0, 0.0, 0.0};
}

void render_pass_remove(render_pass_t *p) {
	array_remove(render_pass_list, p);
	g_context->ddirty = 2;
}

void render_pass_remove_all() {
	if (render_pass_list != NULL) {
		render_pass_list->length = 0;
	}
}

bool render_pass_any() {
	return render_pass_list != NULL && render_pass_list->length > 0;
}

void render_pass_draw(render_pass_stage_t stage, char *target) {
	if (render_pass_list == NULL) {
		return;
	}
	for (i32 i = 0; i < render_pass_list->length; ++i) {
		render_pass_t *p = render_pass_list->buffer[i];
		if (p->stage != stage) {
			continue;
		}
		render_path_set_target(target, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("main", "gbufferD");
		render_path_draw_shader(p->handle);
	}
}
