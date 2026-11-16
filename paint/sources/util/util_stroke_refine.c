#include "../global.h"
#include <iron_profile.h>
#include <stdatomic.h>

// Fill layers painted before their material's Material Maker buffers are full paint again as better tiers land (tiny,
// low, full, see mm_buffers.c): the material renders into a scratch layer through the fill path, in bands of rows over
// frames, then the fill layer takes it. Strokes sharpen by replay (paint_refine.c).
//
// A fill layer whose material is not full is kept in a list (stroke_refine_fill_track, from layers_update_fill_layer)
// with the paint shader it paints with, its pipeline compiled on a thread (stroke_refine_shader_t).

#define STROKE_REFINE_KEYS     32 // Buffer keys a material's paint shader samples, at most
#define STROKE_REFINE_FILLS    32
#define STROKE_REFINE_LAYER_ID 65000 // The scratch layer's id, its targets are texpaint65000...

typedef struct {
	slot_layer_t                *layer;
	slot_material_t             *material;
	u64                          keys[STROKE_REFINE_KEYS];
	i32                          nkeys;
	i32                          tier;
	make_material_async_t       *shader; // Its fill shader, from its first repaint
} stroke_refine_fill_t;

// A paint shader kept for a fill layer: parsed once (a heavy material's parse is a long frame), its buffer
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
#define STROKE_REFINE_BANDING 2 // A band of the material rendered, more to come next frame

// The material renders into the scratch layer in bands of rows, one a frame (a heavy material takes 800 ms of GPU for a
// whole 2048 layer on the iPad), sized from the GPU time of the previous band. One fill layer has the scratch
// layer at a time. Another tier landing meanwhile, the rows left render with it: the whole counts as the tier it
// started with, the next sharpening brings the rows before up.
#define STROKE_REFINE_BAND_MS 20.0

static void      *stroke_refine_band_owner = NULL; // The fill layer whose bands are in the scratch layer
static i32        stroke_refine_band_tier  = -1; // The tier of the first band: the tier composited
static i32        stroke_refine_band_row   = 0;  // Next band's first row
static i32        stroke_refine_band_rows  = 16; // Next band's rows
static i32        stroke_refine_band_timed = 0;  // Rows of the band whose GPU time comes
static f32        stroke_refine_band_peak  = 0;  // Most GPU ms a row took, this material
static atomic_int stroke_refine_band_us    = -1;


static stroke_refine_fill_t stroke_refine_fills[STROKE_REFINE_FILLS];
static i32                  stroke_refine_nfills     = 0;
static slot_layer_t        *stroke_refine_scratch    = NULL;
static bool                 stroke_refine_registered = false;
static i32                  stroke_refine_landed     = -1;
static i32                  stroke_refine_stats[4]; // See stroke_refine_stat

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
static draw_saved_t stroke_refine_saved_draw = {0};

static void stroke_refine_gpu_begin(void) {
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	stroke_refine_saved_draw = draw_suspend();
	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}
}

static void stroke_refine_gpu_end(void) {
	draw_resume(stroke_refine_saved_draw);
	stroke_refine_saved_draw.active = false;
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


static void stroke_refine_scratch_free(void) {
	stroke_refine_band_owner = NULL;
	if (stroke_refine_scratch != NULL) {
		slot_layer_unload(stroke_refine_scratch);
		stroke_refine_scratch = NULL;
	}
}

// A new project, a resize: fill layers go (and the notes of strokes to sharpen, the snapshots made for replays)
void stroke_refine_reset(void) {
	while (stroke_refine_nfills > 0) {
		stroke_refine_fill_remove(stroke_refine_nfills - 1);
	}
	stroke_refine_scratch_free();
	paint_refine_reset();
	paint_mat_snapshot_reset();
}

static void stroke_refine_on_update(void *_);

static void stroke_refine_register(void) {
	if (!stroke_refine_registered) {
		stroke_refine_registered = true;
		sys_notify_on_update(stroke_refine_on_update, NULL);
	}
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

// What the material renders with into the scratch layer: a fill layer's own settings
typedef struct {
	void             *owner; // The fill layer
	slot_material_t  *material;
	f32               scale;
	f32               angle;
	uv_type_t         uv_type;
	i32               uv_map;
	i32               object_mask;
	make_material_async_t **shader;
} stroke_refine_render_t;

static void stroke_refine_band_done(void *data, double gpu_ms) {
	atomic_store(&stroke_refine_band_us, gpu_ms < 0.0 ? 0 : (i32)(gpu_ms * 1000.0));
}

// The GPU time of the last band sizes the next one: STROKE_REFINE_BAND_MS, rows cost unevenly (the densest seen so far
// counts), at most twice as many rows as the last
static void stroke_refine_band_poll(void) {
	i32 us = atomic_exchange(&stroke_refine_band_us, -1);
	if (us < 0 || stroke_refine_band_timed <= 0) {
		return;
	}
	f32 ms      = us / 1000.0f;
	f32 per_row = ms / stroke_refine_band_timed;
	if (per_row > stroke_refine_band_peak) {
		stroke_refine_band_peak = per_row;
	}
	f32 est                 = per_row > stroke_refine_band_peak * 0.75f ? per_row : stroke_refine_band_peak * 0.75f;
	i32 rows                = est > 0.0f ? (i32)(STROKE_REFINE_BAND_MS / est) : stroke_refine_band_timed * 2;
	rows                    = rows > stroke_refine_band_timed * 2 ? stroke_refine_band_timed * 2 : rows;
	stroke_refine_band_rows = rows < 8 ? 8 : rows;
	if (stroke_refine_timing()) {
		iron_log("[refine] band of %d rows: gpu %.2f ms", stroke_refine_band_timed, ms);
	}
	stroke_refine_band_timed = 0;
}

// The material painted into the scratch layer at the tier there is, through the fill path: a fill layer's texels. A band of rows a frame (BANDING until the last), pending while
// its shader compiles or another fill layer has the scratch layer, failed when the material changed since
// (other buffers).
static i32 stroke_refine_render_material(stroke_refine_render_t *m, i32 tier, slot_layer_t *s) {
	make_material_async_t **shader = m->shader;
	if (*shader != NULL && !stroke_refine_shader_ready(*shader)) {
		return make_material_async_started(*shader) ? STROKE_REFINE_PENDING : STROKE_REFINE_FAILED;
	}
	if (stroke_refine_band_owner != NULL && stroke_refine_band_owner != m->owner) {
		return STROKE_REFINE_PENDING; // Its turn comes when the other's bands are done
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

	s->fill_material = m->material;
	s->scale         = m->scale;
	s->angle         = m->angle;
	s->uv_type       = m->uv_type;
	s->uv_map        = m->uv_map;
	s->object_mask   = m->object_mask;

	g_context->layer                = s;
	g_context->material             = m->material;
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
	if (*shader == NULL) { // Parsed once a fill layer
		*shader = stroke_refine_shader_start();
		if (!make_material_async_started(*shader)) {
			result = STROKE_REFINE_FAILED;
		}
	}
	double t1   = iron_time();
	i32    rows = 0;
	i32    h    = (i32)s->texpaint->height;
	if (result != STROKE_REFINE_FAILED && stroke_refine_shader_ready(*shader)) {
		if (stroke_refine_band_owner != m->owner) { // From the first row
			stroke_refine_band_owner = m->owner;
			stroke_refine_band_tier  = tier;
			stroke_refine_band_row   = 0;
			stroke_refine_band_peak  = 0.0f;
		}
		stroke_refine_band_poll();
		i32 row = stroke_refine_band_row;
		rows    = stroke_refine_band_rows;
		rows    = row + rows > h ? h - row : rows;

		shader_context_t *brush  = make_material_paint_scon;
		make_material_paint_scon = make_material_async_scon(*shader);
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0); // The fill reads it as its coverage so far
		render_path_end();
		if (row == 0) {
			slot_layer_clear(s, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		gpu_commit(NULL, NULL); // The work so far apart: the band's GPU time is its own
		g_context->pdirty = 1;
		render_path_paint_set_band(row, (i32)s->texpaint->width, rows);
		render_path_paint_commands_paint(false);
		render_path_paint_set_band(0, 0, 0);
		g_context->brush_blend_dirty = true;
		render_path_paint_clear_blend();
		make_material_paint_scon = brush;
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		stroke_refine_band_timed = rows;
		atomic_store(&stroke_refine_band_us, -1);
		gpu_commit(stroke_refine_band_done, NULL);
		stroke_refine_band_row = row + rows;
		if (stroke_refine_band_row >= h) {
			result                   = STROKE_REFINE_DONE;
			stroke_refine_band_owner = NULL;
		}
		else {
			result = STROKE_REFINE_BANDING;
		}
	}
	if (stroke_refine_timing() && (t1 - t0 > 0.001 || result == STROKE_REFINE_DONE)) {
		iron_log("[refine] material parsed in %.1f ms, its last band (%d rows) painted in %.1f ms", (t1 - t0) * 1000.0, rows, (iron_time() - t1) * 1000.0);
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


// Fill layers

static void stroke_refine_fill_remove(i32 i) {
	if (stroke_refine_band_owner == stroke_refine_fills[i].layer) { // A fill layer's bands go by its layer
		stroke_refine_band_owner = NULL;
	}
	stroke_refine_shader_free(stroke_refine_fills[i].shader);
	stroke_refine_fills[i] = stroke_refine_fills[--stroke_refine_nfills];
	if (stroke_refine_nfills == 0) {
		stroke_refine_scratch_free();
	}
}

// The fill layer painted again with its material at the tier there is, as layers_update_fill_layer paints it: rendered
// into the scratch layer in bands (its own scale, angle, uv mapping and object mask), copied into the fill layer with
// the last one. Its shader is parsed once, compiled on a thread (pending meanwhile), its buffer links resolve to the
// better textures.
static i32 stroke_refine_fill_paint(stroke_refine_fill_t *f, i32 tier) {
	slot_layer_t *l = f->layer;
	if (stroke_refine_band_owner != NULL && stroke_refine_band_owner != l) {
		return STROKE_REFINE_PENDING; // Another one's bands are in the scratch layer
	}
	if (f->shader != NULL && !stroke_refine_shader_ready(f->shader)) {
		return make_material_async_started(f->shader) ? STROKE_REFINE_PENDING : STROKE_REFINE_FAILED;
	}
	slot_layer_t *s = stroke_refine_scratch_layer(l->texpaint->width, l->texpaint->height, l->texpaint->format);
	if (s == NULL) {
		return STROKE_REFINE_FAILED;
	}
	draw_saved_t saved_draw = draw_suspend();

	stroke_refine_render_t m = {l, f->material, l->scale, l->angle, l->uv_type, l->uv_map, l->object_mask, &f->shader};
	i32                    result = stroke_refine_render_material(&m, tier, s);
	if (result == STROKE_REFINE_DONE) {
		gpu_texture_t *dst[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
		gpu_texture_t *src[3] = {s->texpaint, s->texpaint_nor, s->texpaint_pack};
		stroke_refine_gpu_begin();
		result = stroke_refine_copy(dst, src) ? STROKE_REFINE_DONE : STROKE_REFINE_FAILED;
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
		render_path_base_redraw();
	}
	draw_resume(saved_draw);
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

// Each frame: once better buffers landed and no stroke is in progress, fill layers paint again
static void stroke_refine_on_update(void *_) {
	if (stroke_refine_nfills == 0) {
		return;
	}
	if (g_context->brush_time > 0 || history_steps == NULL) {
		return;
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
			i32 result = stroke_refine_fill_paint(f, tier);
			iron_profile_mark("refine fill end");
			if (result == STROKE_REFINE_BANDING) {
				stroke_refine_landed = -1; // The next band next frame
				return;                    // One a frame
			}
			if (result == STROKE_REFINE_PENDING) {
				again = true;
				continue;
			}
			if (result == STROKE_REFINE_FAILED) {
				stroke_refine_fill_remove(i--);
				continue;
			}
			f->tier = stroke_refine_band_tier < tier ? stroke_refine_band_tier : tier; // Its first band's
			stroke_refine_stats[3]++;
			if (stroke_refine_timing()) {
				iron_log("[refine] fill layer %s painted again at tier %d: %.1f ms", f->layer->name, tier, (iron_time() - t0) * 1000.0);
			}
			if (f->tier >= MM_BUFFERS_FULL) {
				stroke_refine_fill_remove(i);
			}
			return; // One a frame
		}
	}
	if (!again) {
		stroke_refine_landed = landed; // Nothing left to do for what landed so far
	}
}

// Tests: 1 fill layers waiting, 3 fill layers painted again
i32 stroke_refine_stat(i32 what) {
	if (what == 1) {
		return stroke_refine_nfills;
	}
	return what == 3 ? stroke_refine_stats[3] : 0;
}
