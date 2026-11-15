
#include "../global.h"
#include <iron_profile.h>

// Strokes painted before a material's Material Maker buffers are full sharpen in place as better tiers land (tiny, low,
// full, see mm_buffers.c). A stroke never has to be replayed: after any dab of a mix stroke, every texel of the layer is
// blend(layer before the stroke, material, coverage), and the coverage of the stroke is texpaint_blend0 (layer
// resolution, camera independent). Mix strokes of one material compose: two strokes of coverage c1 and c2 are one of
// 1 - (1 - c1)(1 - c2). A record per painted layer keeps the layer as it was before its first pending stroke (S0) and
// the accumulated coverage (C); when a better tier lands, the material is rendered into a scratch layer through the fill
// path (M) and the layer becomes mix(S0, M, C), as make_paint.c's mix branch blends.
//
// Records start at a stroke's history step (history_paint) when the material samples buffers not full: mix blending,
// uv mapped brush or fill strokes on a paint layer. A stroke of another material, blending, channels or scale on the
// layer ends its record (those strokes keep the tier they reached), as do undo, redo, steps that change layers otherwise
// (clear, merge, ...), a resize and a new project. A stroke's coverage folds into its record when the stroke ended, before
// anything else paints at brush_time 0 (the live brush preview, fill layers) or clears it (render_path_paint_clear_blend).
// A stroke paints with the tier there is, tiny textures rendered on the spot for the missing ones, or none when the
// paint draws' allowance for that is spent (mm_buffers_texture): the record knows which.
// Sharpening while the last stroke's tiled history step is still pending (nothing pushed since) is part of that step:
// its tiles are taken at the next push, one undo removes the stroke and its sharpening. Otherwise a paint step is
// pushed, linked to the stroke's step when it is the last one.
//
// Fill layers whose material is not full are kept in a list and painted again when its tier goes up. Records and fill
// layers keep the paint shader they paint with, its pipeline compiled on a thread (stroke_refine_shader_t).

#define STROKE_REFINE_MAX      4  // Records at most, the oldest ends first
#define STROKE_REFINE_KEYS     32 // Buffer keys a material's paint shader samples, at most
#define STROKE_REFINE_FILLS    32
#define STROKE_REFINE_LAYER_ID 65000 // The scratch layer's id, its targets are texpaint65000...

typedef struct {
	slot_layer_t    *layer;
	slot_material_t *material;
	u64              keys[STROKE_REFINE_KEYS];
	i32              nkeys;
	i32              tier;    // The tier composited: the lowest the strokes painted with (folded ones)
	i32              stroke_tier; // The tier of the buffers when the stroke in progress started
	tool_type_t      tool;
	blend_type_t     blending;
	u32              channels; // Bits: base, opacity, normal, occlusion, roughness, metallic, height
	f32              scale;    // Texture scale the strokes sampled the material with
	f32              angle;
	bool             linear;   // Linear blending
	gpu_texture_t   *s0[3];    // The layer before the first stroke: base, normal, pack
	gpu_texture_t   *cov;      // Accumulated coverage, R16
	i32              w;
	i32              h;
	i32              format;
	history_step_t  *step;      // The last stroke's history step
	bool             folding;   // A stroke in progress: its coverage folds in when it ends
	i32              fold_wait; // Frames since the stroke ended, not folded yet
	i32              skipped;   // mm_buffers_rush_skipped() when the stroke started
	make_material_async_t *shader; // The material's paint shader into the scratch layer, from the first sharpening
} stroke_refine_t;

typedef struct {
	slot_layer_t                *layer;
	slot_material_t             *material;
	u64                          keys[STROKE_REFINE_KEYS];
	i32                          nkeys;
	i32                          tier;
	make_material_async_t       *shader; // Its fill shader, from its first repaint
} stroke_refine_fill_t;

// A paint shader kept for a record or a fill layer: parsed once (a heavy material's parse is a long frame), its buffer
// links resolve to whichever tier is there. Its GLSL translates and its pipeline compiles on a thread (hundreds of ms on
// the iPad for a heavy material, make_material_async_start), the sharpening waits for it.
typedef make_material_async_t stroke_refine_shader_t;

static void stroke_refine_shader_free(stroke_refine_shader_t *sh) {
	make_material_async_free(sh);
}

// The paint shader of the context as it is now (the caller sets it up): parsed, translated and compiled on a thread
static stroke_refine_shader_t *stroke_refine_shader_start(void) {
	return make_material_async_start();
}

// True once its pipeline is compiled and bound, false when the parse made none (failed)
static bool stroke_refine_shader_ready(stroke_refine_shader_t *sh) {
	return make_material_async_started(sh) && make_material_async_ready(sh);
}

#define STROKE_REFINE_FAILED  -1
#define STROKE_REFINE_PENDING 0 // Its shader compiles: again next frame
#define STROKE_REFINE_DONE    1

static stroke_refine_t     *stroke_refine_recs[STROKE_REFINE_MAX];
static i32                  stroke_refine_count       = 0;
static stroke_refine_fill_t stroke_refine_fills[STROKE_REFINE_FILLS];
static i32                  stroke_refine_nfills      = 0;
static slot_layer_t        *stroke_refine_scratch     = NULL;
static bool                 stroke_refine_registered  = false;
static i32                  stroke_refine_landed      = -1;
static bool                 stroke_refine_busy        = false; // Sharpening: its history step is not a stroke
static i32                  stroke_refine_stats[8];            // See stroke_refine_stat

static void stroke_refine_fill_remove(i32 i);

static bool stroke_refine_timing(void) {
	static int timing = -1;
	if (timing < 0) {
		timing = getenv("IRON_SHADER_TIMING") != NULL;
	}
	return timing == 1;
}

// Passes: fullscreen triangles reading texels at the fragment's position (every texture at the layer's size)

#define STROKE_REFINE_GLSL_HEAD                                                                                                                             \
	"#version 450\n"                                                                                                                                         \
	"layout(set = 0, binding = 0, std140) uniform Constants {\n\tvec4 a;\n\tvec4 b;\n} constants;\n"                                                       \
	"layout(set = 0, binding = 1) uniform sampler sampler_linear;\n"

#define STROKE_REFINE_GLSL_VERT                                                                                                                             \
	"#ifdef VERT\n"                                                                                                                                          \
	"layout(location = 0) in vec2 in_pos;\n"                                                                                                                 \
	"void main() {\n\tgl_Position = vec4(in_pos.xy, 0.0, 1.0);\n}\n"                                                                                        \
	"#endif\n"

// The layer's three textures copied
static char *stroke_refine_copy_glsl = STROKE_REFINE_GLSL_HEAD "layout(set = 0, binding = 2) uniform texture2D tex0;\n"
                                                               "layout(set = 0, binding = 3) uniform texture2D tex1;\n"
                                                               "layout(set = 0, binding = 4) uniform texture2D tex2;\n" STROKE_REFINE_GLSL_VERT
                                                               "#ifdef FRAG\n"
                                                               "layout(location = 0) out vec4 frag_out[3];\n"
                                                               "void main() {\n"
                                                               "\tivec2 p = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));\n"
                                                               "\tfrag_out[0] = texelFetch(sampler2D(tex0, sampler_linear), p, 0) * constants.a.x;\n"
                                                               "\tfrag_out[1] = texelFetch(sampler2D(tex1, sampler_linear), p, 0) * constants.a.x;\n"
                                                               "\tfrag_out[2] = texelFetch(sampler2D(tex2, sampler_linear), p, 0) * constants.a.x;\n"
                                                               "}\n"
                                                               "#endif\n";

// A stroke's coverage (tex0) folded into the accumulated one: blended as b + c * (1 - b), the pipeline's blending
static char *stroke_refine_fold_glsl = STROKE_REFINE_GLSL_HEAD "layout(set = 0, binding = 2) uniform texture2D tex0;\n" STROKE_REFINE_GLSL_VERT
                                                               "#ifdef FRAG\n"
                                                               "layout(location = 0) out vec4 frag_out;\n"
                                                               "void main() {\n"
                                                               "\tivec2 p = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));\n"
                                                               "\tfloat b = clamp(texelFetch(sampler2D(tex0, sampler_linear), p, 0).r * constants.a.x, 0.0, 1.0);\n"
                                                               "\tfrag_out = vec4(b, 0.0, 0.0, b);\n"
                                                               "}\n"
                                                               "#endif\n";

// layer = mix(S0 (tex0-2), M (tex3-5), C (tex6)) per painted channel, as make_paint.c's mix branch (and its linear
// blending branch) blends; a = (base, opacity, normal, linear), b = (occlusion, roughness, metallic, height)
static char *stroke_refine_composite_glsl =
    STROKE_REFINE_GLSL_HEAD "layout(set = 0, binding = 2) uniform texture2D tex0;\n"
                            "layout(set = 0, binding = 3) uniform texture2D tex1;\n"
                            "layout(set = 0, binding = 4) uniform texture2D tex2;\n"
                            "layout(set = 0, binding = 5) uniform texture2D tex3;\n"
                            "layout(set = 0, binding = 6) uniform texture2D tex4;\n"
                            "layout(set = 0, binding = 7) uniform texture2D tex5;\n"
                            "layout(set = 0, binding = 8) uniform texture2D tex6;\n" STROKE_REFINE_GLSL_VERT
                            "#ifdef FRAG\n"
                            "layout(location = 0) out vec4 frag_out[3];\n"
                            "void main() {\n"
                            "\tivec2 p = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));\n"
                            "\tvec4 s0 = texelFetch(sampler2D(tex0, sampler_linear), p, 0);\n"
                            "\tvec4 s1 = texelFetch(sampler2D(tex1, sampler_linear), p, 0);\n"
                            "\tvec4 s2 = texelFetch(sampler2D(tex2, sampler_linear), p, 0);\n"
                            "\tfloat c = clamp(texelFetch(sampler2D(tex6, sampler_linear), p, 0).r, 0.0, 1.0);\n"
                            "\tif (c <= 0.0) {\n"
                            "\t\tfrag_out[0] = s0;\n"
                            "\t\tfrag_out[1] = s1;\n"
                            "\t\tfrag_out[2] = s2;\n"
                            "\t\treturn;\n"
                            "\t}\n"
                            "\tvec4 m0 = texelFetch(sampler2D(tex3, sampler_linear), p, 0);\n"
                            "\tvec4 m1 = texelFetch(sampler2D(tex4, sampler_linear), p, 0);\n"
                            "\tvec4 m2 = texelFetch(sampler2D(tex5, sampler_linear), p, 0);\n"
                            "\tfloat out_a = c + s0.a * (1.0 - c);\n"
                            "\tvec3 rgb = (m0.rgb * c + s0.rgb * s0.a * (1.0 - c)) / max(out_a, 0.0000001);\n"
                            "\tif (constants.a.w > 0.5) {\n"
                            "\t\tvec3 undo_lin = pow(max(s0.rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2));\n"
                            "\t\tvec3 base_lin = pow(max(m0.rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2));\n"
                            "\t\tvec3 out_lin = (base_lin * c + undo_lin * s0.a * (1.0 - c)) / max(out_a, 0.0000001);\n"
                            "\t\trgb = pow(max(out_lin, vec3(0.0, 0.0, 0.0)), vec3(0.4545, 0.4545, 0.4545));\n"
                            "\t}\n"
                            "\tvec4 o0 = s0;\n"
                            "\tif (constants.a.x > 0.5) {\n"
                            "\t\to0.x = rgb.x;\n"
                            "\t\to0.y = rgb.y;\n"
                            "\t\to0.z = rgb.z;\n"
                            "\t}\n"
                            "\tif (constants.a.y > 0.5) {\n"
                            "\t\to0.w = out_a;\n"
                            "\t}\n"
                            "\tvec3 n = mix(s1.rgb, m1.rgb, c);\n"
                            "\tvec4 o1 = vec4(s1.x, s1.y, s1.z, m1.w);\n"
                            "\tif (constants.a.z > 0.5) {\n"
                            "\t\to1.x = n.x;\n"
                            "\t\to1.y = n.y;\n"
                            "\t\to1.z = n.z;\n"
                            "\t}\n"
                            "\tvec4 k = mix(s2, m2, c);\n"
                            "\tvec4 o2 = s2;\n"
                            "\tif (constants.b.x > 0.5) {\n"
                            "\t\to2.x = k.x;\n"
                            "\t}\n"
                            "\tif (constants.b.y > 0.5) {\n"
                            "\t\to2.y = k.y;\n"
                            "\t}\n"
                            "\tif (constants.b.z > 0.5) {\n"
                            "\t\to2.z = k.z;\n"
                            "\t}\n"
                            "\tif (constants.b.w > 0.5) {\n"
                            "\t\to2.w = k.w;\n"
                            "\t}\n"
                            "\tfrag_out[0] = o0;\n"
                            "\tfrag_out[1] = o1;\n"
                            "\tfrag_out[2] = o2;\n"
                            "}\n"
                            "#endif\n";

typedef struct {
	char                *glsl;
	gpu_texture_format_t format;
	i32                  targets;
	bool                 blend; // b + c * (1 - b)
	gpu_pipeline_t       pipe;
	bool                 ok;
} stroke_refine_pipe_t;

#define STROKE_REFINE_PIPES 8

static stroke_refine_pipe_t   stroke_refine_pipes[STROKE_REFINE_PIPES];
static i32                    stroke_refine_npipes = 0;
static gpu_vertex_structure_t stroke_refine_structure;

static stroke_refine_pipe_t *stroke_refine_pipe(char *glsl, gpu_texture_format_t format, i32 targets, bool blend) {
	for (i32 i = 0; i < stroke_refine_npipes; ++i) {
		stroke_refine_pipe_t *p = &stroke_refine_pipes[i];
		if (p->glsl == glsl && p->format == format) {
			return p;
		}
	}
	if (stroke_refine_npipes == STROKE_REFINE_PIPES) {
		return NULL;
	}
	if (stroke_refine_structure.size == 0) {
		gpu_vertex_structure_add(&stroke_refine_structure, "pos", GPU_VERTEX_DATA_F32_2X);
	}
	stroke_refine_pipe_t *p = &stroke_refine_pipes[stroke_refine_npipes++];
	memset(p, 0, sizeof(stroke_refine_pipe_t));
	p->glsl    = glsl;
	p->format  = format;
	p->targets = targets;
	p->blend   = blend;
	char *vs = NULL, *fs = NULL;
	i32   vs_size = 0, fs_size = 0;
	gpu_create_shaders_from_glsl(glsl, &vs, &fs, &vs_size, &fs_size);
	if (vs == NULL || fs == NULL) {
		iron_log("[refine] pass did not translate");
		return p;
	}
	gpu_pipeline_init(&p->pipe);
	p->pipe.input_layout           = &stroke_refine_structure;
	p->pipe.vertex_shader          = gpu_create_shader_from_source(vs, vs_size, GPU_SHADER_TYPE_VERTEX);
	p->pipe.fragment_shader        = gpu_create_shader_from_source(fs, fs_size, GPU_SHADER_TYPE_FRAGMENT);
	p->pipe.color_attachment_count = targets;
	for (i32 i = 0; i < targets; ++i) {
		p->pipe.color_attachment[i] = format;
	}
	if (blend) {
		p->pipe.blend_source            = GPU_BLEND_ONE;
		p->pipe.blend_destination       = GPU_BLEND_INV_SOURCE_ALPHA;
		p->pipe.alpha_blend_source      = GPU_BLEND_ONE;
		p->pipe.alpha_blend_destination = GPU_BLEND_INV_SOURCE_ALPHA;
	}
	gpu_pipeline_compile(&p->pipe);
	p->ok = p->pipe.impl.pipeline != NULL;
	if (!p->ok) {
		iron_log("[refine] pass did not compile");
	}
	return p;
}

// The passes need their own render passes: the render path's or the 2D drawing in progress ends meanwhile
static bool           stroke_refine_saved_draw   = false;
static gpu_texture_t *stroke_refine_saved_target = NULL;

static void stroke_refine_gpu_begin(void) {
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	stroke_refine_saved_draw = gpu_in_use;
	if (stroke_refine_saved_draw) {
		stroke_refine_saved_target = _draw_current;
		draw_end();
	}
	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}
}

static void stroke_refine_gpu_end(void) {
	if (stroke_refine_saved_draw) {
		draw_begin(stroke_refine_saved_target, false, 0);
		stroke_refine_saved_draw = false;
	}
}

static bool stroke_refine_pass(stroke_refine_pipe_t *p, gpu_texture_t **targets, gpu_texture_t **inputs, i32 n, f32 *a, f32 *b) {
	if (p == NULL || !p->ok) {
		return false;
	}
	gpu_begin(targets, p->targets, NULL, GPU_CLEAR_NONE, 0, 0.0f);
	gpu_set_pipeline(&p->pipe);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	for (i32 i = 0; i < n; ++i) {
		gpu_set_texture(i, inputs[i]);
	}
	gpu_set_float4(0, a[0], a[1], a[2], a[3]);
	gpu_set_float4(16, b[0], b[1], b[2], b[3]);
	gpu_draw();
	gpu_end();
	return true;
}

static f32 stroke_refine_one[4] = {1.0f, 1.0f, 1.0f, 1.0f};

static bool stroke_refine_copy(gpu_texture_t **dst, gpu_texture_t **src) {
	return stroke_refine_pass(stroke_refine_pipe(stroke_refine_copy_glsl, dst[0]->format, 3, false), dst, src, 3, stroke_refine_one, stroke_refine_one);
}

// Records

static void stroke_refine_free(stroke_refine_t *r) {
	for (i32 i = 0; i < 3; ++i) {
		if (r->s0[i] != NULL) {
			gpu_delete_texture(r->s0[i]);
		}
	}
	if (r->cov != NULL) {
		gpu_delete_texture(r->cov);
	}
	stroke_refine_shader_free(r->shader);
	free(r);
}

static void stroke_refine_scratch_free(void) {
	if (stroke_refine_scratch != NULL) {
		slot_layer_unload(stroke_refine_scratch);
		stroke_refine_scratch = NULL;
	}
}

// The record goes; full: it reached the full tier, else its strokes keep the tier they reached
static void stroke_refine_end(i32 i, bool full) {
	if (!full) {
		stroke_refine_stats[6]++;
	}
	stroke_refine_free(stroke_refine_recs[i]);
	for (i32 j = i; j < stroke_refine_count - 1; ++j) {
		stroke_refine_recs[j] = stroke_refine_recs[j + 1];
	}
	stroke_refine_count--;
	if (stroke_refine_count == 0) {
		stroke_refine_scratch_free();
	}
}

// Undo, redo: the strokes keep the tier they reached
void stroke_refine_end_all(void) {
	while (stroke_refine_count > 0) {
		stroke_refine_end(stroke_refine_count - 1, false);
	}
}

// A history step changes layers' texels otherwise than a stroke does (their records' S0 would bring the old texels
// back): the records end, the strokes keep the tier they reached
void stroke_refine_history_pushed(history_action_t action) {
	if (stroke_refine_count == 0) {
		return;
	}
	switch (action) {
	case HISTORY_ACTION_DELETE_LAYER:
	case HISTORY_ACTION_CLEAR_LAYER:
	case HISTORY_ACTION_MERGE_LAYERS:
	case HISTORY_ACTION_APPLY_MASK:
	case HISTORY_ACTION_APPLY_FILTER:
	case HISTORY_ACTION_TO_FILL_LAYER:
	case HISTORY_ACTION_TO_FILL_MASK:
	case HISTORY_ACTION_TO_PAINT_LAYER:
	case HISTORY_ACTION_TO_PAINT_MASK:
	case HISTORY_ACTION_LAYER_FILL_MATERIAL:
	case HISTORY_ACTION_SMART_MATERIAL:
		stroke_refine_end_all();
		break;
	default:
		break;
	}
}

// A new project, a resize: records and fill layers go
void stroke_refine_reset(void) {
	stroke_refine_end_all();
	while (stroke_refine_nfills > 0) {
		stroke_refine_fill_remove(stroke_refine_nfills - 1);
	}
	stroke_refine_scratch_free();
}

static stroke_refine_t *stroke_refine_find(slot_layer_t *l, i32 *index) {
	for (i32 i = 0; i < stroke_refine_count; ++i) {
		if (stroke_refine_recs[i]->layer == l) {
			*index = i;
			return stroke_refine_recs[i];
		}
	}
	return NULL;
}

static u32 stroke_refine_channels(void) {
	slot_material_t *m = g_context->material;
	return (m->paint_base ? 1 : 0) | (m->paint_opac ? 2 : 0) | (m->paint_nor ? 4 : 0) | (m->paint_occ ? 8 : 0) | (m->paint_rough ? 16 : 0) |
	       (m->paint_met ? 32 : 0) | (m->paint_height ? 64 : 0);
}

// The current stroke paints the material through the mix blend at its uv coordinates, the coverage tells it all
static bool stroke_refine_eligible(slot_layer_t *l) {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_FILL) {
		return false;
	}
	if (tool == TOOL_TYPE_FILL && (g_context->fill_type == FILL_TYPE_ANGLE || g_context->fill_type == FILL_TYPE_MESH || g_context->fill_type == FILL_TYPE_SUBMESH)) {
		return false; // These pick the mesh at their stroke's start: painted as they were
	}
	if (l == NULL || !slot_layer_is_layer(l) || slot_layer_is_path(l) || l->fill_material != NULL || l->texpaint == NULL || l->texpaint_nor == NULL ||
	    l->texpaint_pack == NULL) {
		return false;
	}
	if (g_context->brush_blending != BLEND_TYPE_MIX || g_context->brush_paint != UV_TYPE_UVMAP || context_is_decal() || g_context->picker_material_mask ||
	    g_context->picker_color_mask || util_mesh_udim_layer(l) || history_undo_layers == NULL) {
		return false;
	}
	return (i32)l->texpaint->width == config_get_texture_res_x() && (i32)l->texpaint->height == config_get_texture_res_y();
}

static f32 stroke_refine_scale(void) {
	return g_context->material->uv_scale * g_context->brush_nodes_scale;
}

static void stroke_refine_on_update(void *_);

static void stroke_refine_register(void) {
	if (!stroke_refine_registered) {
		stroke_refine_registered = true;
		sys_notify_on_update(stroke_refine_on_update, NULL);
	}
}

// A stroke's history step was pushed (history_paint), the layer is as before the stroke
void stroke_refine_stroke_begin(history_step_t *step) {
	if (stroke_refine_busy) {
		return;
	}
	slot_layer_t *l  = g_context->layer;
	i32           ri = -1;
	stroke_refine_t *r        = stroke_refine_find(l, &ri);
	bool             eligible = stroke_refine_eligible(l);
	if (r != NULL) {
		bool same = eligible && r->material == g_context->material && r->tool == g_context->tool && r->channels == stroke_refine_channels() &&
		            r->scale == stroke_refine_scale() && r->angle == g_context->material->uv_angle && r->linear == g_project->linear_blending;
		if (same) {
			i32 tier       = mm_buffers_links_tier(r->keys, r->nkeys);
			r->step        = step;
			r->folding     = true;
			r->fold_wait   = 0;
			r->stroke_tier = tier < 0 ? 0 : tier;
			r->skipped     = mm_buffers_rush_skipped();
			return;
		}
		stroke_refine_end(ri, false);
	}
	if (!eligible) {
		return;
	}
	u64 keys[STROKE_REFINE_KEYS];
	i32 nkeys = mm_buffers_paint_keys(make_material_paint_scon, keys, STROKE_REFINE_KEYS);
	if (nkeys == 0) {
		return;
	}
	i32 tier = mm_buffers_links_tier(keys, nkeys);
	if (tier >= MM_BUFFERS_FULL || tier < 0) {
		return;
	}
	if (stroke_refine_count == STROKE_REFINE_MAX) {
		stroke_refine_end(0, false);
	}
	r           = calloc(1, sizeof(stroke_refine_t));
	r->layer    = l;
	r->material = g_context->material;
	memcpy(r->keys, keys, sizeof(u64) * nkeys);
	r->nkeys       = nkeys;
	r->tier        = MM_BUFFERS_FULL; // Until the stroke folds
	r->stroke_tier = tier;
	r->skipped     = mm_buffers_rush_skipped();
	r->tool     = g_context->tool;
	r->blending = g_context->brush_blending;
	r->channels = stroke_refine_channels();
	r->scale    = stroke_refine_scale();
	r->angle    = g_context->material->uv_angle;
	r->linear   = g_project->linear_blending;
	r->w        = l->texpaint->width;
	r->h        = l->texpaint->height;
	r->format   = l->texpaint->format;
	r->step     = step;
	r->folding  = true;
	for (i32 i = 0; i < 3; ++i) {
		r->s0[i] = gpu_create_render_target(r->w, r->h, r->format);
	}
	r->cov = gpu_create_render_target(r->w, r->h, GPU_TEXTURE_FORMAT_R16);
	stroke_refine_gpu_begin();
	gpu_texture_t *src[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	bool           ok     = stroke_refine_copy(r->s0, src);
	gpu_texture_t *cov[1] = {r->cov};
	gpu_begin(cov, 1, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0f);
	gpu_end();
	stroke_refine_gpu_end();
	if (!ok) {
		stroke_refine_free(r);
		return;
	}
	stroke_refine_recs[stroke_refine_count++] = r;
	stroke_refine_register();
	if (stroke_refine_timing()) {
		iron_log("[refine] stroke on %s, buffers at tier %d when it starts, %d buffers", l->name, r->stroke_tier, nkeys);
	}
}

// The stroke ended: its coverage (texpaint_blend0, about to be cleared) folds into its record's
void stroke_refine_fold(void) {
	for (i32 i = 0; i < stroke_refine_count; ++i) {
		stroke_refine_t *r = stroke_refine_recs[i];
		if (!r->folding) {
			continue;
		}
		r->folding = false;
		if (r->layer != g_context->layer) { // The coverage is another layer's stroke's: this one's was lost
			stroke_refine_end(i--, false);
			continue;
		}
		// The tier its dabs painted with: what there was when it started, tiny or better when every missing texture
		// rendered on the spot, as it was when one was skipped (the time box, see mm_buffers_texture)
		i32 tier = r->stroke_tier;
		if (mm_buffers_rush_skipped() == r->skipped && tier < MM_BUFFERS_TINY) {
			tier = MM_BUFFERS_TINY;
		}
		r->tier             = tier < r->tier ? tier : r->tier;
		render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_blend0");
		if (rt == NULL || (i32)rt->_image->width != r->w || (i32)rt->_image->height != r->h) {
			stroke_refine_end(i--, false);
			continue;
		}
		stroke_refine_gpu_begin();
		gpu_texture_t *targets[1] = {r->cov};
		gpu_texture_t *inputs[1]  = {rt->_image};
		bool           ok         = stroke_refine_pass(stroke_refine_pipe(stroke_refine_fold_glsl, GPU_TEXTURE_FORMAT_R16, 1, true), targets, inputs, 1,
		                                               stroke_refine_one, stroke_refine_one);
		stroke_refine_gpu_end();
		if (!ok) {
			stroke_refine_end(i--, false);
		}
	}
}

static bool stroke_refine_keys_equal(u64 *a, i32 na, u64 *b, i32 nb) {
	if (na != nb) {
		return false;
	}
	for (i32 i = 0; i < na; ++i) {
		bool found = false;
		for (i32 j = 0; j < nb && !found; ++j) {
			found = a[i] == b[j];
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

// The record's layer, material and size are still there
static bool stroke_refine_valid(stroke_refine_t *r) {
	slot_layer_t *l = r->layer;
	return array_index_of(g_project->_->layers, l) >= 0 && array_index_of(g_project->_->materials, r->material) >= 0 && slot_layer_is_layer(l) &&
	       l->fill_material == NULL && l->texpaint != NULL && (i32)l->texpaint->width == r->w && (i32)l->texpaint->height == r->h &&
	       (i32)l->texpaint->format == r->format && l->texpaint_nor != NULL && l->texpaint_pack != NULL;
}

static slot_layer_t *stroke_refine_scratch_layer(i32 w, i32 h, i32 format) {
	slot_layer_t *s = stroke_refine_scratch;
	if (s != NULL && (s->texpaint == NULL || (i32)s->texpaint->width != w || (i32)s->texpaint->height != h || (i32)s->texpaint->format != format)) {
		stroke_refine_scratch_free();
		s = NULL;
	}
	if (s == NULL) {
		s     = slot_layer_create(string("%d", STROKE_REFINE_LAYER_ID), LAYER_SLOT_TYPE_LAYER, NULL);
		s->id = STROKE_REFINE_LAYER_ID;
		if (s->texpaint == NULL || (i32)s->texpaint->width != w || (i32)s->texpaint->height != h || (i32)s->texpaint->format != format) {
			slot_layer_unload(s);
			return NULL;
		}
		stroke_refine_scratch = s;
	}
	return s;
}

// The material painted whole into the scratch layer at the tier there is, through the fill path: the base color, normal
// and pack the strokes blended in. Pending while its shader compiles, failed when the material changed since (other
// buffers).
static i32 stroke_refine_render_material(stroke_refine_t *r, slot_layer_t *s) {
	if (r->shader != NULL && !stroke_refine_shader_ready(r->shader)) {
		return make_material_async_started(r->shader) ? STROKE_REFINE_PENDING : STROKE_REFINE_FAILED;
	}
	slot_layer_t    *_layer       = g_context->layer;
	slot_material_t *_material    = g_context->material;
	tool_type_t      _tool        = g_context->tool;
	i32              _fill_type   = g_context->fill_type;
	i32              _pdirty      = g_context->pdirty;
	f32              _nodes_scale = g_context->brush_nodes_scale;
	bool             _pick_mat    = g_context->picker_material_mask;
	bool             _pick_col    = g_context->picker_color_mask;
	gpu_texture_t   *_stencil     = g_context->brush_stencil_image;
	blend_type_t     _blending    = g_context->brush_blending;

	s->fill_material = r->material;
	s->scale         = r->scale;
	s->angle         = r->angle;
	s->uv_type       = UV_TYPE_UVMAP;
	s->uv_map        = r->layer->uv_map;
	s->object_mask   = 0;

	g_context->layer                = s;
	g_context->material             = r->material;
	g_context->tool                 = TOOL_TYPE_FILL;
	g_context->fill_type            = FILL_TYPE_OBJECT;
	g_context->brush_nodes_scale    = 1.0;
	g_context->picker_material_mask = false;
	g_context->picker_color_mask    = false;
	g_context->brush_stencil_image  = NULL;
	g_context->brush_blending       = BLEND_TYPE_MIX;
	layers_set_object_mask();

	double t0     = iron_time();
	i32    result = STROKE_REFINE_PENDING;
	if (r->shader == NULL) { // Parsed once a record
		r->shader = stroke_refine_shader_start();
		u64 keys[STROKE_REFINE_KEYS];
		i32 nkeys = mm_buffers_paint_keys(make_material_async_scon(r->shader), keys, STROKE_REFINE_KEYS);
		if (!make_material_async_started(r->shader) || !stroke_refine_keys_equal(keys, nkeys, r->keys, r->nkeys)) {
			result = STROKE_REFINE_FAILED;
		}
	}
	double t1 = iron_time();
	if (result != STROKE_REFINE_FAILED && stroke_refine_shader_ready(r->shader)) {
		result                   = STROKE_REFINE_DONE;
		shader_context_t *brush  = make_material_paint_scon;
		make_material_paint_scon = make_material_async_scon(r->shader);
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0); // The fill reads it as its coverage so far
		render_path_end();
		slot_layer_clear(s, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		g_context->pdirty = 1;
		render_path_paint_commands_paint(false);
		g_context->brush_blend_dirty = true;
		render_path_paint_clear_blend();
		make_material_paint_scon = brush;
	}
	if (stroke_refine_timing() && (t1 - t0 > 0.001 || result == STROKE_REFINE_DONE)) {
		iron_log("[refine] material parsed in %.1f ms, painted in %.1f ms", (t1 - t0) * 1000.0, (iron_time() - t1) * 1000.0);
	}

	g_context->layer                = _layer;
	g_context->material             = _material;
	g_context->tool                 = _tool;
	g_context->fill_type            = _fill_type;
	g_context->pdirty               = _pdirty;
	g_context->brush_nodes_scale    = _nodes_scale;
	g_context->picker_material_mask = _pick_mat;
	g_context->picker_color_mask    = _pick_col;
	g_context->brush_stencil_image  = _stencil;
	g_context->brush_blending       = _blending;
	s->fill_material                = NULL;
	layers_set_object_mask();
	return result;
}

// The history: part of the last stroke's tiled step while it is pending, else a paint step of its own (linked to the
// stroke's when it is the last one)
static void stroke_refine_history(stroke_refine_t *r) {
	slot_layer_t   *pending_l = NULL;
	history_step_t *pending   = history_tiles_pending_step(&pending_l);
	if (pending != NULL && pending == r->step && pending_l == r->layer) {
		return;
	}
	if (stroke_refine_timing()) {
		iron_log("[refine] sharpening of %s in a history step of its own (%s)", r->layer->name,
		         history_steps->length > 0 && history_steps->buffer[history_steps->length - 1] == r->step ? "linked to the stroke's" : "not linked");
	}
	slot_layer_t   *_layer = g_context->layer;
	tool_type_t     _tool  = g_context->tool;
	bool            _push  = history_push_undo;
	history_step_t *last   = history_steps->length > 0 ? history_steps->buffer[history_steps->length - 1] : NULL;
	g_context->layer       = r->layer;
	g_context->tool        = r->tool;
	i32 before             = history_steps->length;
	history_paint();
	if (history_steps->length > before && last == r->step && history_steps->length > 1) {
		history_steps->buffer[history_steps->length - 1]->with_prev = true;
	}
	r->step           = history_steps->buffer[history_steps->length - 1];
	g_context->layer  = _layer;
	g_context->tool   = _tool;
	history_push_undo = _push;
}

// The record's strokes recomposited with the material at the tier: pending while its shader compiles, failed when it can
// not be (the record ends)
static i32 stroke_refine_sharpen(stroke_refine_t *r, i32 tier) {
	double t0 = iron_time();
	slot_layer_t *s = stroke_refine_scratch_layer(r->w, r->h, r->format);
	if (s == NULL) {
		return STROKE_REFINE_FAILED;
	}
	stroke_refine_busy = true;
	iron_profile_mark("refine sharpen start");
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	i32  result = stroke_refine_render_material(r, s);
	bool ok     = result == STROKE_REFINE_DONE;
	if (ok) {
		stroke_refine_history(r);
		slot_layer_t  *l          = r->layer;
		gpu_texture_t *targets[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
		gpu_texture_t *inputs[7]  = {r->s0[0], r->s0[1], r->s0[2], s->texpaint, s->texpaint_nor, s->texpaint_pack, r->cov};
		u32            c          = r->channels;
		f32            a[4]       = {(c & 1) ? 1.0f : 0.0f, (c & 2) ? 1.0f : 0.0f, (c & 4) ? 1.0f : 0.0f, r->linear ? 1.0f : 0.0f};
		f32            b[4]       = {(c & 8) ? 1.0f : 0.0f, (c & 16) ? 1.0f : 0.0f, (c & 32) ? 1.0f : 0.0f, (c & 64) ? 1.0f : 0.0f};
		stroke_refine_gpu_begin();
		ok     = stroke_refine_pass(stroke_refine_pipe(stroke_refine_composite_glsl, r->format, 3, false), targets, inputs, 7, a, b);
		result = ok ? STROKE_REFINE_DONE : STROKE_REFINE_FAILED;
		stroke_refine_gpu_end();
		slot_layer_t *_layer = g_context->layer;
		g_context->layer     = l;
		render_path_paint_dilate(true, true);
		g_context->layer = _layer;
		if (l == g_context->layer) {
			g_context->layer_preview_dirty = true;
		}
		g_context->layers_preview_dirty = true;
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		r->tier                         = tier;
		stroke_refine_stats[2]++;
	}
	if (in_use) {
		draw_begin(current, false, 0);
	}
	stroke_refine_busy = false;
	iron_profile_mark("refine sharpen end");
	double ms = (iron_time() - t0) * 1000.0;
	if (result == STROKE_REFINE_DONE) {
		stroke_refine_stats[5] = (i32)(ms * 1000.0);
	}
	if (stroke_refine_timing() && (result != STROKE_REFINE_PENDING || ms > 1.0)) {
		iron_log("[refine] %s %s tier %d: %.1f ms", r->layer->name,
		         result == STROKE_REFINE_DONE ? "sharpened to" : result == STROKE_REFINE_PENDING ? "waits for its shader to sharpen to" : "can not sharpen to", tier, ms);
	}
	return result;
}

// Fill layers

static void stroke_refine_fill_remove(i32 i) {
	stroke_refine_shader_free(stroke_refine_fills[i].shader);
	stroke_refine_fills[i] = stroke_refine_fills[--stroke_refine_nfills];
}

// The fill layer painted again with its material at the tier there is, as layers_update_fill_layer paints it; its shader
// is parsed once, compiled on a thread (pending meanwhile), its buffer links resolve to the better textures
static i32 stroke_refine_fill_paint(stroke_refine_fill_t *f) {
	if (f->shader != NULL && !stroke_refine_shader_ready(f->shader)) {
		return make_material_async_started(f->shader) ? STROKE_REFINE_PENDING : STROKE_REFINE_FAILED;
	}
	i32              result     = STROKE_REFINE_PENDING;
	slot_layer_t    *l          = f->layer;
	gpu_texture_t   *current    = _draw_current;
	bool             in_use     = gpu_in_use;
	slot_layer_t    *_layer     = g_context->layer;
	slot_material_t *_material  = g_context->material;
	tool_type_t      _tool      = g_context->tool;
	i32              _fill_type = g_context->fill_type;
	i32              _pdirty    = g_context->pdirty;
	if (in_use) {
		draw_end();
	}
	g_context->layer     = l;
	g_context->material  = f->material;
	g_context->tool      = TOOL_TYPE_FILL;
	g_context->fill_type = FILL_TYPE_OBJECT;
	layers_set_object_mask();
	if (f->shader == NULL) {
		f->shader = stroke_refine_shader_start();
		if (!make_material_async_started(f->shader)) {
			result = STROKE_REFINE_FAILED;
		}
	}
	if (result != STROKE_REFINE_FAILED && stroke_refine_shader_ready(f->shader)) {
		result                   = STROKE_REFINE_DONE;
		shader_context_t *brush  = make_material_paint_scon;
		make_material_paint_scon = make_material_async_scon(f->shader);
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_end();
		slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		g_context->pdirty = 1;
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
		g_context->brush_blend_dirty = true;
		render_path_paint_clear_blend();
		make_material_paint_scon = brush;
	}
	g_context->layer     = _layer;
	g_context->material  = _material;
	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	g_context->pdirty    = _pdirty;
	layers_set_object_mask();
	if (result == STROKE_REFINE_DONE) {
		if (l == g_context->layer) {
			g_context->layer_preview_dirty = true;
		}
		g_context->layers_preview_dirty = true;
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		render_path_base_redraw();
	}
	if (in_use) {
		draw_begin(current, false, 0);
	}
	return result;
}

// A fill layer was painted (layers_update_fill_layer): kept in the list while its material is not full
void stroke_refine_fill_track(slot_layer_t *l) {
	for (i32 i = 0; i < stroke_refine_nfills; ++i) {
		if (stroke_refine_fills[i].layer == l) {
			stroke_refine_fill_remove(i);
			break;
		}
	}
	if (l == NULL || l->fill_material == NULL) {
		return;
	}
	u64 keys[STROKE_REFINE_KEYS];
	i32 nkeys = mm_buffers_paint_keys(make_material_paint_scon, keys, STROKE_REFINE_KEYS);
	i32 tier  = nkeys > 0 ? mm_buffers_links_tier(keys, nkeys) : MM_BUFFERS_FULL;
	if (tier >= MM_BUFFERS_FULL || tier < 0 || stroke_refine_nfills == STROKE_REFINE_FILLS) {
		return;
	}
	stroke_refine_fill_t *f = &stroke_refine_fills[stroke_refine_nfills++];
	f->layer                = l;
	f->material             = l->fill_material;
	memcpy(f->keys, keys, sizeof(u64) * nkeys);
	f->nkeys  = nkeys;
	f->tier   = tier; // Painted just now with these (0: the time box skipped some, see mm_buffers_texture)
	f->shader = NULL;
	stroke_refine_register();
}

// Each frame: once better buffers landed and no stroke is in progress, fill layers paint again and records sharpen
static void stroke_refine_on_update(void *_) {
	if (stroke_refine_count == 0 && stroke_refine_nfills == 0) {
		return;
	}
	if (g_context->brush_time > 0 || history_steps == NULL) {
		return;
	}
	for (i32 i = 0; i < stroke_refine_count; ++i) {
		stroke_refine_t *r = stroke_refine_recs[i];
		if (!r->folding) {
			continue;
		}
		// The stroke ended: its coverage folds when the render clears it (render_path_paint_clear_blend). When no render
		// does after a few frames, it folds here, or the stroke is lost (cleared without folding: the record ends).
		if (++r->fold_wait > 3) {
			if (g_context->brush_blend_dirty) {
				render_path_paint_clear_blend();
			}
			else {
				stroke_refine_end(i, false);
			}
		}
		return;
	}
	for (i32 i = 0; i < stroke_refine_count; ++i) { // Gone layers, materials or buffers end their records at once
		stroke_refine_t *r = stroke_refine_recs[i];
		if (!stroke_refine_valid(r) || mm_buffers_links_tier(r->keys, r->nkeys) < 0) {
			stroke_refine_end(i--, false);
		}
	}
	i32 landed = mm_buffers_landed_count();
	if (landed == stroke_refine_landed) {
		return;
	}
	bool again = false; // A shader still compiles: again next frame

	for (i32 i = 0; i < stroke_refine_nfills; ++i) {
		stroke_refine_fill_t *f = &stroke_refine_fills[i];
		if (array_index_of(g_project->_->layers, f->layer) < 0 || f->layer->fill_material != f->material) {
			stroke_refine_fill_remove(i--);
			continue;
		}
		i32 tier = mm_buffers_links_tier(f->keys, f->nkeys);
		if (tier < 0) { // Its buffers are gone: painted again when its material compiles again
			stroke_refine_fill_remove(i--);
			continue;
		}
		if (tier > f->tier) {
			double t0 = iron_time();
			iron_profile_mark("refine fill start");
			i32 result = stroke_refine_fill_paint(f);
			iron_profile_mark("refine fill end");
			if (result == STROKE_REFINE_PENDING) {
				again = true;
				continue;
			}
			if (result == STROKE_REFINE_FAILED) {
				stroke_refine_fill_remove(i--);
				continue;
			}
			f->tier = tier;
			stroke_refine_stats[3]++;
			if (stroke_refine_timing()) {
				iron_log("[refine] fill layer %s painted again at tier %d: %.1f ms", f->layer->name, tier, (iron_time() - t0) * 1000.0);
			}
			if (tier >= MM_BUFFERS_FULL) {
				stroke_refine_fill_remove(i);
			}
			return; // One a frame
		}
	}

	for (i32 i = 0; i < stroke_refine_count; ++i) {
		stroke_refine_t *r = stroke_refine_recs[i];
		if (!stroke_refine_valid(r)) {
			stroke_refine_end(i--, false);
			continue;
		}
		i32 tier = mm_buffers_links_tier(r->keys, r->nkeys);
		if (tier < 0) { // Its buffers are gone
			stroke_refine_end(i--, false);
			continue;
		}
		if (tier <= r->tier) {
			continue;
		}
		i32 result = stroke_refine_sharpen(r, tier);
		if (result == STROKE_REFINE_PENDING) {
			again = true;
			continue;
		}
		if (result == STROKE_REFINE_FAILED) {
			stroke_refine_end(i, false);
			return;
		}
		if (tier >= MM_BUFFERS_FULL) {
			stroke_refine_end(i, true);
		}
		return; // One a frame
	}
	if (!again) {
		stroke_refine_landed = landed; // Nothing left to do for what landed so far
	}
}

// Tests: 0 records, 1 fill layers waiting, 2 sharpenings done, 3 fill layers painted again, 4 the current layer's
// record's tier (0 none), 5 the last sharpening's main thread time in microseconds, 6 records ended before full
i32 stroke_refine_stat(i32 what) {
	if (what == 0) {
		return stroke_refine_count;
	}
	if (what == 1) {
		return stroke_refine_nfills;
	}
	if (what == 4) {
		i32              i = -1;
		stroke_refine_t *r = stroke_refine_find(g_context->layer, &i);
		return r == NULL ? 0 : r->folding && r->stroke_tier < r->tier ? r->stroke_tier : r->tier;
	}
	return what >= 0 && what < 8 ? stroke_refine_stats[what] : 0;
}
