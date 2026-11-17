#include "../global.h"
#include "util_hash.h"

// Fill layers, as Painter's: every channel (base color, opacity, normal, roughness, metallic, height, occlusion,
// emission) takes its own source, a color or value, a project image, a pattern (nodes_material/pattern_nodes.c) between
// two colors or values, or that channel of a material. The sources are projected onto the surface by the layer's uv
// map, three planes (triplanar, hardness), a plane repeating along the decal box's axis (planar) or the decal box once
// (decal), then scaled (each axis on its own), rotated and offset.
//
// Live: the viewport evaluates fills in its shader (render/make_fill.c from make_mesh.c), at any zoom, and every number
// of a fill lives in the fill parameters texture (a row per layer), so editing one costs a texture upload, not a
// shader. The layer's own textures hold a raster of the fill only for what reads pixels (export, merge, apply mask,
// bake, previews, anchors): rasterized again a moment after the fill stops changing, or at once by fill_ensure.
//
// Materials as sources: a material's outputs are rendered once over the 0..1 square into tileable textures, its
// tile (base and opacity, normal and emission, occlusion roughness metallic height), sampled by the fill's projection as
// Painter samples a substance material. A tile renders again when the material's graph changes or its Material Maker
// buffers land at a better tier, and is freed when no fill uses the material. A graph that reads the mesh (position,
// normal, curvature) sees the flat square.
//
// A fill mask is a fill too: its base color as gray, rasterized into the mask (masks are read as textures).

#define FILL_PARAMS_WIDTH 64
#define FILL_SETTLE       0.25 // Seconds a fill stays unchanged before its textures are rasterized again
#define FILL_TILE_MAX     2048
#define FILL_TILES_MAX    32

vec4_t fill_uv_tile = {0.0, 0.0, 1.0, 1.0}; // _fill_uv_tile of the raster pass: u0, v0, 1 / width, 1 / height

// ---- Sources ----

static f32 fill_defaults[FILL_CHANNEL_COUNT][4] = {
    {0.8, 0.8, 0.8, 1.0}, // Base
    {1.0, 0.0, 0.0, 1.0}, // Opacity
    {0.5, 0.5, 1.0, 1.0}, // Normal
    {0.5, 0.0, 0.0, 1.0}, // Roughness
    {0.0, 0.0, 0.0, 1.0}, // Metallic
    {0.0, 0.0, 0.0, 1.0}, // Height
    {1.0, 0.0, 0.0, 1.0}, // Occlusion
    {0.0, 0.0, 0.0, 1.0}, // Emission
};

static void fill_pattern_defaults(fill_source_t *s, i32 pattern) {
	pattern    = pattern >= 0 && pattern < pattern_node_count() ? pattern : 0;
	s->pattern = pattern;
	f32 common[7] = {pattern_def_scale(pattern), 1.0, 0.0, 1.0, 0.0, 0.0, 4.0}; // Scale, stretch, angle, contrast, seed, distort, distort scale
	for (i32 i = 0; i < 7; ++i) {
		s->params[i] = common[i];
	}
	for (i32 j = 0; j < 6; ++j) {
		s->params[7 + j] = j < pattern_def_extras(pattern) ? pattern_def_extra_value(pattern, j) : 0.0;
	}
}

// The settings of s's pattern at their defaults
void fill_pattern_params_reset(fill_source_t *s) {
	fill_pattern_defaults(s, s->pattern);
}

// Source s of channel c as a new one of kind, its color the channel's default
void fill_source_reset(fill_source_t *s, i32 c, i32 kind) {
	*s = (fill_source_t){0};
	memcpy(s->value, fill_defaults[c], sizeof(s->value));
	memcpy(s->value2, fill_defaults[c], sizeof(s->value2));
	s->image         = -1;
	s->image_channel = 4;
	fill_pattern_defaults(s, 0);
	s->kind = FILL_SOURCE_COLOR;
	fill_source_set_kind(s, c, kind);
}

// Another kind of source, keeping what carries over: a pattern goes from a dark shade of the color to the color (a
// value from 0 to 1, a normal is a bump of strength 1), back from a pattern the color is its high end
void fill_source_set_kind(fill_source_t *s, i32 c, i32 kind) {
	if (s->kind == kind) {
		return;
	}
	if (kind == FILL_SOURCE_PROCEDURAL) {
		if (c == FILL_CHANNEL_NORMAL) {
			s->value[0] = 1.0;
		}
		else if (c == FILL_CHANNEL_BASE) {
			for (i32 i = 0; i < 3; ++i) {
				s->value2[i] = s->value[i];
				s->value[i]  = s->value[i] * 0.25;
			}
		}
		else {
			s->value[0]  = 0.0;
			s->value2[0] = 1.0;
		}
	}
	else if (s->kind == FILL_SOURCE_PROCEDURAL) {
		if (c == FILL_CHANNEL_NORMAL) {
			memcpy(s->value, fill_defaults[c], sizeof(s->value));
		}
		else {
			memcpy(s->value, s->value2, sizeof(s->value));
		}
	}
	s->kind = kind;
}

fill_t *fill_create(i32 projection) {
	fill_t *f     = calloc(1, sizeof(fill_t));
	f->projection = projection;
	f->scale[0]   = 1.0;
	f->scale[1]   = 1.0;
	f->hardness   = 0.5;
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_reset(&f->ch[c], c, FILL_SOURCE_COLOR);
	}
	f->ch[FILL_CHANNEL_ROUGHNESS].value[0] = layers_default_rough;
	return f;
}

fill_t *fill_copy(fill_t *f) {
	if (f == NULL) {
		return NULL;
	}
	fill_t *c = malloc(sizeof(fill_t));
	*c        = *f;
	return c;
}

// Every channel from material m, its uv scale and rotation the fill's
void fill_set_material(fill_t *f, slot_material_t *m) {
	for (i32 c = 0; m != NULL && c < FILL_CHANNEL_COUNT; ++c) {
		f->ch[c].kind     = FILL_SOURCE_MATERIAL;
		f->ch[c].material = m->uid;
	}
}

// A new fill of m in every channel, scaled and turned as the material paints (its uv scale and rotation)
fill_t *fill_from_material(slot_material_t *m, i32 projection) {
	fill_t *f = fill_create(projection);
	if (m != NULL) {
		fill_set_material(f, m);
		f->scale[0] = f->scale[1] = m->uv_scale > 0.0 ? m->uv_scale : 1.0;
		f->angle                  = m->uv_angle;
	}
	return f;
}

// Materials a replay renders with in place of the project's (timeline_ops.c: a fill's material as it was recorded),
// under uids no project material has
#define FILL_STAND_INS 8
static u32              fill_stand_in_uids[FILL_STAND_INS];
static slot_material_t *fill_stand_in_materials[FILL_STAND_INS];
static i32              fill_stand_in_count = 0;

void fill_stand_in_set(u32 uid, slot_material_t *m) {
	if (m == NULL) {
		for (i32 i = 0; i < fill_stand_in_count; ++i) {
			if (fill_stand_in_uids[i] == uid) {
				fill_stand_in_uids[i]      = fill_stand_in_uids[--fill_stand_in_count];
				fill_stand_in_materials[i] = fill_stand_in_materials[fill_stand_in_count];
				return;
			}
		}
		return;
	}
	if (fill_stand_in_count < FILL_STAND_INS) {
		fill_stand_in_uids[fill_stand_in_count]        = uid;
		fill_stand_in_materials[fill_stand_in_count++] = m;
	}
}

static slot_material_t *fill_material_find(u32 uid) {
	for (i32 i = 0; i < fill_stand_in_count; ++i) {
		if (fill_stand_in_uids[i] == uid) {
			return fill_stand_in_materials[i];
		}
	}
	return slot_material_find_uid(uid);
}

static bool fill_is_stand_in(u32 uid) {
	for (i32 i = 0; i < fill_stand_in_count; ++i) {
		if (fill_stand_in_uids[i] == uid) {
			return true;
		}
	}
	return false;
}

slot_material_t *fill_source_material(fill_source_t *s) {
	return s->kind == FILL_SOURCE_MATERIAL ? fill_material_find(s->material) : NULL;
}

asset_t *fill_source_asset(fill_source_t *s) {
	if (s->kind != FILL_SOURCE_TEXTURE || s->image < 0) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (a->id == s->image) {
			return a;
		}
	}
	return NULL;
}

// The material most of the fill comes from (its base color's first), NULL when none
slot_material_t *fill_main_material(fill_t *f) {
	if (f == NULL) {
		return NULL;
	}
	slot_material_t *m = fill_source_material(&f->ch[FILL_CHANNEL_BASE]);
	for (i32 c = 0; m == NULL && c < FILL_CHANNEL_COUNT; ++c) {
		m = fill_source_material(&f->ch[c]);
	}
	return m;
}

bool fill_uses_material(fill_t *f, slot_material_t *m) {
	for (i32 c = 0; f != NULL && m != NULL && c < FILL_CHANNEL_COUNT; ++c) {
		if (f->ch[c].kind == FILL_SOURCE_MATERIAL && f->ch[c].material == m->uid) {
			return true;
		}
	}
	return false;
}

// A material is gone: the channels it was the source of keep their color
void fill_material_removed(slot_material_t *m) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (!fill_uses_material(l->fill, m)) {
			continue;
		}
		for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
			if (l->fill->ch[c].kind == FILL_SOURCE_MATERIAL && l->fill->ch[c].material == m->uid) {
				l->fill->ch[c].kind = FILL_SOURCE_COLOR;
			}
		}
		fill_reshaped(l);
	}
}

// ---- Encoding ----
// A fill as floats, to save it with the project, the history, smart materials and timelines. by_index: materials
// and images by their place in the project (files), else by uid and asset id (the session).

#define FILL_ENCODE_VERSION 1

f32_array_t *fill_encode(fill_t *f, bool by_index) {
	if (f == NULL) {
		return NULL;
	}
	f32_array_t *a = f32_array_create(FILL_ENCODE_LENGTH);
	f32         *p = a->buffer;
	*p++           = FILL_ENCODE_VERSION;
	*p++           = f->projection;
	*p++           = f->scale[0];
	*p++           = f->scale[1];
	*p++           = f->offset[0];
	*p++           = f->offset[1];
	*p++           = f->angle;
	*p++           = f->hardness;
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_t *s = &f->ch[c];
		*p++             = s->kind;
		memcpy(p, s->value, sizeof(f32) * 4);
		p += 4;
		memcpy(p, s->value2, sizeof(f32) * 4);
		p += 4;
		if (by_index) {
			asset_t *a = fill_source_asset(s);
			*p++       = a != NULL ? array_index_of(g_project->_->assets, a) : -1;
		}
		else {
			*p++ = s->image;
		}
		*p++ = s->image_channel;
		*p++ = s->pattern;
		memcpy(p, s->params, sizeof(f32) * FILL_PATTERN_PARAMS);
		p += FILL_PATTERN_PARAMS;
		if (by_index) {
			slot_material_t *m = slot_material_find_uid(s->material);
			*p++               = m != NULL ? array_index_of(g_project->_->materials, m) : -1;
		}
		else {
			*p++ = (f32)s->material;
		}
	}
	return a;
}

// by_index: materials are indices into materials (the ones being loaded), images into the project's assets
fill_t *fill_decode_with(f32_array_t *a, bool by_index, slot_material_t_array_t *materials) {
	if (a == NULL || a->length < FILL_ENCODE_LENGTH) {
		return NULL;
	}
	fill_t *f     = fill_create(FILL_PROJECTION_UV);
	f32    *p     = a->buffer + 1;
	f->projection = (i32)*p++;
	if (f->projection < 0 || f->projection >= FILL_PROJECTION_COUNT) {
		f->projection = FILL_PROJECTION_UV;
	}
	f->scale[0]  = *p++;
	f->scale[1]  = *p++;
	f->offset[0] = *p++;
	f->offset[1] = *p++;
	f->angle     = *p++;
	f->hardness  = *p++;
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_t *s = &f->ch[c];
		s->kind          = (i32)*p++;
		if (s->kind < 0 || s->kind >= FILL_SOURCE_COUNT) {
			s->kind = FILL_SOURCE_COLOR;
		}
		memcpy(s->value, p, sizeof(f32) * 4);
		p += 4;
		memcpy(s->value2, p, sizeof(f32) * 4);
		p += 4;
		i32 image = (i32)*p++;
		if (by_index) {
			s->image = image >= 0 && image < g_project->_->assets->length ? g_project->_->assets->buffer[image]->id : -1;
		}
		else {
			s->image = image;
		}
		s->image_channel = (i32)*p++;
		s->pattern       = (i32)*p++;
		if (s->pattern < 0 || s->pattern >= pattern_node_count()) {
			s->pattern = 0;
		}
		memcpy(s->params, p, sizeof(f32) * FILL_PATTERN_PARAMS);
		p += FILL_PATTERN_PARAMS;
		f32 m = *p++;
		if (by_index) {
			i32 mi      = (i32)m;
			s->material = materials != NULL && mi >= 0 && mi < materials->length ? materials->buffer[mi]->uid : 0;
		}
		else {
			s->material = (u32)m;
		}
	}
	return f;
}

fill_t *fill_decode(f32_array_t *a, bool by_index) {
	return fill_decode_with(a, by_index, g_project->_->materials);
}

// ---- Changes ----

static bool fill_params_dirty = true;
static f64  fill_changed_at   = 0.0;

// What the shaders of l's fill depend on: projection, the kind of each source, its pattern, image or material
u64 fill_shape(slot_layer_t *l) {
	u64 h = 1469598103934665603ull;
	if (l == NULL || l->fill == NULL) {
		return h;
	}
	fill_t *f = l->fill;
	h         = hash_fnv1a_i32(h, f->projection);
	h         = hash_fnv1a_i32(h, l->id);
	h         = hash_fnv1a_i32(h, l->uv_map);
	h         = hash_fnv1a_i32(h, slot_layer_is_mask(l));
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_t *s = &f->ch[c];
		h                = hash_fnv1a_i32(h, s->kind);
		if ((c == FILL_CHANNEL_EMISSION || c == FILL_CHANNEL_HEIGHT) && s->kind == FILL_SOURCE_COLOR) {
			h = hash_fnv1a_i32(h, s->value[0] != 0.0); // Zero switches the channel off in the viewport (make_fill_uses)
		}
		if (s->kind == FILL_SOURCE_TEXTURE) {
			h = hash_fnv1a_i32(h, fill_source_asset(s) != NULL ? s->image : -1);
			h = hash_fnv1a_i32(h, s->image_channel);
		}
		else if (s->kind == FILL_SOURCE_PROCEDURAL) {
			h = hash_fnv1a_i32(h, s->pattern);
		}
		else if (s->kind == FILL_SOURCE_MATERIAL) {
			h = hash_fnv1a_i32(h, fill_source_material(s) != NULL ? (i32)s->material : 0);
		}
	}
	return h;
}

// A value of l's fill changed: the viewport reads it from the parameters texture, the textures follow later. A change of
// its shape (fill_shape) makes the viewport's shader again.
void fill_changed(slot_layer_t *l) {
	if (l == NULL || l->fill == NULL) {
		return;
	}
	u64 shape = fill_shape(l);
	if (shape != l->fill_shaped) {
		l->fill_shaped = shape;
		make_material_parse_mesh_material();
	}
	l->fill_dirty                   = true;
	fill_params_dirty               = true;
	fill_changed_at                 = iron_time();
	g_context->ddirty               = math_max(g_context->ddirty, 2);
	g_context->rtdirty              = 1;
	g_context->layers_preview_dirty = true;
	render_path_base_redraw();
}

// Its shape may have changed: the viewport's shader again in any case
void fill_reshaped(slot_layer_t *l) {
	if (l != NULL) {
		l->fill_shaped = 0;
	}
	fill_changed(l);
}

// ---- Parameters texture ----

static gpu_texture_t *fill_params_tex = NULL;

static f32 fill_clampf(f32 v, f32 lo, f32 hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

static void fill_params_row(f32 *row, slot_layer_t *l) {
	fill_t *f = l->fill;
	f32    *t;
	t    = row + 0 * 4;
	t[0] = f->scale[0];
	t[1] = f->scale[1];
	t[2] = f->offset[0];
	t[3] = f->offset[1];
	t    = row + 1 * 4;
	f32 a = f->angle * (math_pi() / 180.0);
	t[0]  = math_cos(a);
	t[1]  = math_sin(a);
	f32 h = fill_clampf(f->hardness, 0.0, 1.0);
	t[2]  = 1.0 + 63.0 * h * h; // Exponent of the plane weights
	mat4_t m = mat4_inv(l->decal_mat);
	f32   *w = row + 2 * 4; // World to decal box, three rows
	w[0]     = m.m00, w[1] = m.m10, w[2] = m.m20, w[3] = m.m30;
	w[4]     = m.m01, w[5] = m.m11, w[6] = m.m21, w[7] = m.m31;
	w[8]     = m.m02, w[9] = m.m12, w[10] = m.m22, w[11] = m.m32;
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_t *s  = &f->ch[c];
		f32           *ch = row + (FILL_TEXELS_HEAD + c * FILL_TEXELS_CHANNEL) * 4;
		memcpy(ch + 0, s->value, sizeof(f32) * 4);
		memcpy(ch + 4, s->value2, sizeof(f32) * 4);
		f32 *p = s->params;
		f32  pt[4] = {math_max(p[0], 0.01), math_max(p[1], 0.01), p[2] * 0.0174533, p[4]};
		f32  pd[4] = {p[5], p[6], p[3], 0.0};
		f32  pa[4] = {p[7], p[8], p[9], p[10]};
		f32  pb[4] = {p[11], p[12], 0.0, 0.0};
		memcpy(ch + 8, pt, sizeof(pt));
		memcpy(ch + 12, pd, sizeof(pd));
		memcpy(ch + 16, pa, sizeof(pa));
		memcpy(ch + 20, pb, sizeof(pb));
	}
}

// Rows by layer id, every fill layer and fill mask of the project
gpu_texture_t *fill_params_texture(void) {
	if (!fill_params_dirty && fill_params_tex != NULL) {
		return fill_params_tex;
	}
	i32 rows = 1;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill != NULL && l->id + 1 > rows) {
			rows = l->id + 1;
		}
	}
	buffer_t *b = buffer_create(FILL_PARAMS_WIDTH * rows * 16);
	memset(b->buffer, 0, b->length);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill != NULL) {
			fill_params_row((f32 *)b->buffer + l->id * FILL_PARAMS_WIDTH * 4, l);
		}
	}
	if (fill_params_tex != NULL) {
		gpu_delete_texture(fill_params_tex); // Freed once the gpu is done with it
	}
	fill_params_tex   = gpu_create_texture_from_bytes_raw(b, FILL_PARAMS_WIDTH, rows, GPU_TEXTURE_FORMAT_RGBA128);
	fill_params_dirty = false;
	free(b->buffer);
	free(b);
	return fill_params_tex;
}

gpu_texture_t *fill_image_texture(i32 asset_id) {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (a->id == asset_id) {
			gpu_texture_t *t = project_get_image(a);
			return t != NULL ? t : render_path_empty_white();
		}
	}
	return render_path_empty_white();
}

// ---- Material tiles ----

typedef struct fill_tile {
	u32               uid;
	u64               sig;      // Of the graph rendered
	u64               shader_sig; // Of the graph its shader was parsed from
	shader_context_t *scon;
	gpu_texture_t    *tex[3];
	i32               res;
	i32               landed; // mm_buffers_landed_count when rendered
	bool              full;   // Its Material Maker buffers were full then
	u64               keys[64];
	i32               nkeys;
} fill_tile_t;

static fill_tile_t fill_tiles[FILL_TILES_MAX];
static bool        fill_tile_baking = false; // A tile's own bakes, see fill_tile_render
static i32         fill_tile_count   = 0;
static i32         fill_tile_renders = 0; // fill_stat(3)

static fill_tile_t *fill_tile_find(u32 uid) {
	for (i32 i = 0; i < fill_tile_count; ++i) {
		if (fill_tiles[i].uid == uid) {
			return &fill_tiles[i];
		}
	}
	return NULL;
}

static void fill_tile_free(i32 i) {
	fill_tile_t *e = &fill_tiles[i];
	for (i32 k = 0; k < 3; ++k) {
		if (e->tex[k] != NULL) {
			gpu_delete_texture(e->tex[k]);
		}
	}
	if (e->scon != NULL) {
		make_material_delete_context(e->scon);
	}
	fill_tiles[i] = fill_tiles[--fill_tile_count];
}

gpu_texture_t *fill_tile_texture(u32 uid, i32 k) {
	fill_tile_t *e = fill_tile_find(uid);
	return e != NULL && e->tex[k] != NULL ? e->tex[k] : render_path_empty_black();
}

static i32 fill_tile_res(void) {
	i32 r = math_min(config_get_texture_res_x(), config_get_texture_res_y());
#ifdef IRON_IOS
	i32 cap = FILL_TILE_MAX / 2;
#else
	i32 cap = FILL_TILE_MAX;
#endif
	return r > cap ? cap : r;
}

// The material's tile shader, parsed as the material it is (nodes that read the current material see it)
static void fill_tile_parse(fill_tile_t *e, slot_material_t *m) {
	if (e->scon != NULL) {
		make_material_delete_context(e->scon);
		e->scon = NULL;
	}
	slot_material_t *_material = g_context->material;
	g_context->material        = m;
	material_t            *sd  = ALLOC_INIT(material_t, {.name = "Material", .canvas = m->canvas});
	node_shader_context_t *con = make_fill_tile_run(sd, m, gpu_texture_format_name(layers_format(base_bits)));
	g_context->material        = _material;
	shader_context_load(con->data);
	e->scon = con->data;
	node_shader_context_free(con);
	free(sd);
	e->nkeys      = e->scon != NULL ? mm_buffers_paint_keys(e->scon, e->keys, 64) : 0;
	e->shader_sig = e->sig;
}

static void fill_tile_render(fill_tile_t *e, slot_material_t *m) {
	i32 res = fill_tile_res();
	if (e->tex[0] == NULL || e->res != res) {
		for (i32 k = 0; k < 3; ++k) {
			if (e->tex[k] != NULL) {
				gpu_delete_texture(e->tex[k]);
			}
			e->tex[k] = gpu_create_render_target(res, res, layers_format(base_bits));
		}
		e->res = res;
	}
	if (e->scon == NULL || e->shader_sig != e->sig) {
		fill_tile_parse(e, m);
	}
	fill_tile_renders++;
	e->landed = mm_buffers_landed_count();
	e->full   = e->nkeys == 0 || mm_buffers_links_tier(e->keys, e->nkeys) >= MM_BUFFERS_FULL;
	if (e->scon == NULL || e->scon->_->pipe == NULL) {
		return;
	}
	if (util_render_screen_aligned_full_vb == NULL) {
		util_render_create_screen_aligned_full_data();
	}
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	static string_array_t *empty = NULL;
	if (empty == NULL) {
		empty = any_array_create_from_raw((void *[]){""}, 1);
	}
	draw_saved_t     saved     = draw_suspend();
	slot_material_t *_material = g_context->material;
	g_context->material        = m;
	fill_tile_baking           = true; // Bake nodes (curvature, blur, warp) of the material, it may not be the selected one
	make_material_bake_node_previews();
	fill_tile_baking = false;
	// What is there, the rest lands later and renders the tile again. A replay's stand-in renders once: full.
	i32 need = mm_buffers_require(fill_is_stand_in(e->uid) ? MM_BUFFERS_FULL : 0);
	gpu_begin(e->tex, 3, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_set_pipeline(e->scon->_->pipe);
	_mesh_object_last_pipeline = NULL;
	uniforms_set_context_consts(e->scon, empty);
	uniforms_set_obj_consts(e->scon, g_context->paint_object->base);
	gpu_set_vertex_buffer(util_render_screen_aligned_full_vb);
	gpu_set_index_buffer(util_render_screen_aligned_full_ib);
	gpu_draw();
	gpu_end();
	mm_buffers_require(need);
	g_context->material = _material;
	draw_resume(saved);
}

// Tiles of the materials fills use: made, rendered again when stale, the others freed. True when one rendered.
static bool fill_tiles_update(void) {
	u32 used[FILL_TILES_MAX];
	i32 nused = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		for (i32 c = 0; l->fill != NULL && c < FILL_CHANNEL_COUNT; ++c) {
			slot_material_t *m = fill_source_material(&l->fill->ch[c]);
			if (m == NULL) {
				continue;
			}
			bool seen = false;
			for (i32 j = 0; j < nused; ++j) {
				seen = seen || used[j] == m->uid;
			}
			if (!seen && nused < FILL_TILES_MAX) {
				used[nused++] = m->uid;
			}
		}
	}
	for (i32 i = 0; i < fill_tile_count; ++i) {
		bool keep = false;
		for (i32 j = 0; j < nused; ++j) {
			keep = keep || used[j] == fill_tiles[i].uid;
		}
		if (!keep) {
			fill_tile_free(i--);
		}
	}
	bool rendered = false;
	for (i32 j = 0; j < nused; ++j) {
		slot_material_t *m = fill_material_find(used[j]);
		fill_tile_t     *e = fill_tile_find(used[j]);
		if (e == NULL) {
			e      = &fill_tiles[fill_tile_count++];
			*e     = (fill_tile_t){0};
			e->uid = used[j];
		}
		u64  sig   = material_cache_graph_signature(m);
		bool stale = e->tex[0] == NULL || sig != e->sig || e->res != fill_tile_res() ||
		             (!e->full && mm_buffers_landed_count() != e->landed);
		if (!stale) {
			continue;
		}
		e->sig = sig;
		fill_tile_render(e, m);
		rendered = true;
		for (i32 i = 0; i < g_project->_->layers->length; ++i) { // Not an edit: rasterized without waiting to settle
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (fill_uses_material(l->fill, m)) {
				l->fill_dirty                   = true;
				g_context->layers_preview_dirty = true;
			}
		}
		g_context->ddirty = math_max(g_context->ddirty, 2);
		render_path_base_redraw();
	}
	return rendered;
}

// The Material Maker passes the tiles sample (export renders them full, the tiles follow)
i32 fill_tiles_mm_keys(u64 *out, i32 max) {
	i32 n = 0;
	for (i32 i = 0; i < fill_tile_count; ++i) {
		for (i32 k = 0; k < fill_tiles[i].nkeys && n < max; ++k) {
			out[n++] = fill_tiles[i].keys[k];
		}
	}
	return n;
}

// A tile renders again: its material reads what changed (layers, mesh maps, the ID map), NULL every tile
void fill_tile_invalidate(slot_material_t *m) {
	if (fill_tile_baking) {
		return;
	}
	for (i32 i = 0; i < fill_tile_count; ++i) {
		if (m == NULL || fill_tiles[i].uid == m->uid) {
			fill_tiles[i].sig        = 0;
			fill_tiles[i].shader_sig = 0; // What it reads can change its shader too (a mesh map in place of a bake)
		}
	}
}

// The parameters texture is made again before it is read next (a layer took another fill for a while)
void fill_params_invalidate(void) {
	fill_params_dirty = true;
}

// A new project: tiles and shaders of the last one go
void fill_reset(void) {
	while (fill_tile_count > 0) {
		fill_tile_free(0);
	}
	fill_params_dirty = true;
}

// ---- Rasterizing ----

typedef struct fill_raster {
	u32               uid; // Layer
	u64               shape;
	i32               format;
	shader_context_t *scon;
} fill_raster_t;

static fill_raster_t *fill_rasters      = NULL;
static i32            fill_raster_count = 0;
static i32            fill_raster_cap   = 0;

static shader_context_t *fill_raster_scon(slot_layer_t *l, gpu_texture_format_t format) {
	u64 shape = fill_shape(l);
	for (i32 i = 0; i < fill_raster_count; ++i) {
		fill_raster_t *r = &fill_rasters[i];
		if (r->uid == l->uid) {
			if (r->shape == shape && r->format == (i32)format) {
				return r->scon;
			}
			make_material_delete_context(r->scon);
			fill_rasters[i] = fill_rasters[--fill_raster_count];
			break;
		}
	}
	material_t            *sd  = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con = make_fill_raster_run(sd, l, gpu_texture_format_name(format));
	shader_context_load(con->data);
	shader_context_t *scon = con->data;
	node_shader_context_free(con);
	free(sd);
	if (fill_raster_count == fill_raster_cap) {
		fill_raster_cap = fill_raster_cap == 0 ? 8 : fill_raster_cap * 2;
		fill_rasters    = realloc(fill_rasters, sizeof(fill_raster_t) * fill_raster_cap);
	}
	fill_rasters[fill_raster_count++] = (fill_raster_t){.uid = l->uid, .shape = shape, .format = (i32)format, .scon = scon};
	return scon;
}

// The paint objects l covers: its object mask's, an atlas's, else all
static bool fill_draws_object(slot_layer_t *l, i32 i) {
	i32 mask  = slot_layer_get_object_mask(l);
	i32 count = g_project->_->paint_objects->length;
	if (mask <= 0) {
		return true;
	}
	if (mask <= count) {
		return i == mask - 1;
	}
	mesh_object_t_array_t *atlas = project_get_atlas_objects(mask);
	return atlas != NULL && array_index_of(atlas, g_project->_->paint_objects->buffer[i]) >= 0;
}

// The fill of l rendered for the uv rectangle (u0, v0, u1, v1) into out: base, normal and pack of a layer, the mask of
// a mask, all of one size and format. Cleared first, as an empty layer.
bool fill_render(slot_layer_t *l, gpu_texture_t **out, const f32 *uv_rect) {
	if (l == NULL || l->fill == NULL || out == NULL || out[0] == NULL) {
		return false;
	}
	bool mask  = slot_layer_is_mask(l);
	i32  count = mask ? 1 : 3;
	fill_params_texture(); // Up to date for the pass
	fill_tiles_update();
	shader_context_t *scon = fill_raster_scon(l, out[0]->format);
	if (scon == NULL || scon->_->pipe == NULL) {
		return false;
	}
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	draw_saved_t saved = draw_suspend();
	u32          clr[3] = {mask ? 0xff000000 : 0x00000000, color_from_floats(0.5, 0.5, 1.0, 0.0), color_from_floats(1.0, layers_default_rough, 0.0, 0.0)};
	for (i32 k = 0; k < count; ++k) {
		_gpu_begin(out[k], NULL, NULL, GPU_CLEAR_COLOR, clr[k], 0.0);
		gpu_end();
	}
	vec4_t _tile = fill_uv_tile;
	fill_uv_tile = (vec4_t){uv_rect[0], uv_rect[1], 1.0 / (uv_rect[2] - uv_rect[0]), 1.0 / (uv_rect[3] - uv_rect[1])};
	gpu_begin(out, count, NULL, GPU_CLEAR_NONE, 0, 0.0);
	_mesh_object_last_pipeline = NULL;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (fill_draws_object(l, i)) {
			mesh_object_draw(g_project->_->paint_objects->buffer[i], scon, NULL);
		}
	}
	gpu_end();
	_mesh_object_last_pipeline = NULL;
	fill_uv_tile               = _tile;
	draw_resume(saved);
	return true;
}

// The fill of l into its own textures, dilated over the uv seams as painted layers are
void fill_rasterize(slot_layer_t *l) {
	if (l == NULL || l->fill == NULL || l->texpaint == NULL) {
		return;
	}
	gpu_texture_t *out[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	f32            all[4] = {0.0, 0.0, 1.0, 1.0};
	l->fill_dirty         = false;
	if (!fill_render(l, out, all)) {
		return;
	}
	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = l;
	render_path_paint_dilate(true, !slot_layer_is_mask(l));
	g_context->layer = _layer;
	mask_stack_layer_changed(l); // Masks anchoring it
	if (slot_layer_is_mask(l)) {
		mask_stack_mark_dirty(l);
	}
	g_context->layers_preview_dirty = true;
	g_context->ddirty               = math_max(g_context->ddirty, 2);
}

// The textures of l hold its fill now: for whatever reads them next
void fill_ensure(slot_layer_t *l) {
	if (l != NULL && l->fill != NULL && l->fill_dirty) {
		fill_rasterize(l);
	}
}

void fill_ensure_all(void) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		fill_ensure(g_project->_->layers->buffer[i]);
	}
}

// Each frame before the viewport renders: tiles, parameters, and the rasters of fills that stopped changing. Fill
// masks rasterize at once, the viewport reads masks as textures.
void fill_update(void) {
	if (g_project == NULL || g_project->_->layers == NULL) {
		return;
	}
	bool any = false;
	for (i32 i = 0; !any && i < g_project->_->layers->length; ++i) {
		any = g_project->_->layers->buffer[i]->fill != NULL;
	}
	if (!any) {
		if (fill_tile_count > 0) {
			fill_reset();
		}
		return;
	}
	if (fill_tiles_update()) {
		g_context->ddirty = math_max(g_context->ddirty, 2);
	}
	fill_params_texture();
	bool settled = iron_time() - fill_changed_at > FILL_SETTLE;
	bool waiting = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill != NULL && l->fill_dirty && (settled || slot_layer_is_mask(l))) {
			fill_rasterize(l);
		}
		waiting = waiting || (l->fill != NULL && l->fill_dirty);
	}
	if (waiting) { // Frames keep coming until the fills settled and rasterized
		render_path_base_redraw();
		iron_delay_idle_sleep();
	}
}


// Scripts and tests: 0 material tiles, 1 tiles waiting for better Material Maker buffers, 2 fill layers waiting to
// be rasterized, 3 tile renders so far
i32 fill_stat(i32 what) {
	i32 n = 0;
	switch (what) {
	case 0:
		return fill_tile_count;
	case 1:
		for (i32 i = 0; i < fill_tile_count; ++i) {
			n += !fill_tiles[i].full;
		}
		return n;
	case 2:
		for (i32 i = 0; g_project != NULL && i < g_project->_->layers->length; ++i) {
			n += g_project->_->layers->buffer[i]->fill != NULL && g_project->_->layers->buffer[i]->fill_dirty;
		}
		return n;
	case 3:
		return fill_tile_renders;
	}
	return 0;
}

// Tests: "ok" when the raster shader of fill layer id compiled, else why not
char *fill_test_shader(i32 id) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->id != id) {
			continue;
		}
		if (l->fill == NULL) {
			return "no fill";
		}
		shader_context_t *scon = fill_raster_scon(l, l->texpaint->format);
		return scon != NULL && scon->_->pipe != NULL ? "ok" : "no pipeline";
	}
	return "no layer";
}
