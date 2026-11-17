#include "global.h"

// Undo history saved with v31 projects: the step list as an armpack map
//   {version, serial_next, undos, redos, steps: [step map, ..]}
// A step map holds the step's scalars, uids and tl_moves; mask_effects, swatches, path props, layer_name and image_file inline; its
// node canvas as an HSTORE_CANVAS blob; the layers of a delete or merge step as their layer records (history_swap.c:
// props and tile map, filled on undo by the record restore) with merge_parents. Hashes are i32 arrays of 4 (lo, hi).
// GPU undo tiles are not saved: loaded steps undo from timelines.
// The blobs the steps hold (records, canvases) are history_io_roots; timelines and their blobs are saved with them
// (timeline_io.c), never dropped by thinning.

#define HISTORY_IO_VERSION 1

// Canvases put by the last encode, a reference each: the next encode or a clear lets them go
static hash128_t *history_io_canvases      = NULL;
static i32        history_io_canvas_count  = 0;
static u8        *history_io_canvas_buf    = NULL;
#define HISTORY_IO_CANVAS_BUF (16 * 1024 * 1024)

static void history_io_canvases_release() {
	for (i32 i = 0; i < history_io_canvas_count; ++i) {
		hstore_unref(history_io_canvases[i]);
	}
	free(history_io_canvases);
	history_io_canvases     = NULL;
	history_io_canvas_count = 0;
}

static hash128_t history_io_canvas_put(ui_node_canvas_t *c) {
	if (history_io_canvas_buf == NULL) {
		history_io_canvas_buf = malloc(HISTORY_IO_CANVAS_BUF);
	}
	armpack_encode_start(history_io_canvas_buf);
	ui_node_canvas_encode(c);
	u32 len = armpack_encode_end();
	return hstore_put(HSTORE_CANVAS, history_io_canvas_buf, len);
}

// A step the saved history can hold: layers out of the project as records
static bool history_io_savable(history_step_t *s) {
	return s->merge_layers == NULL || s->merge_layers->length == 0 || s->records != NULL;
}

// Steps [first, end) of history_steps are saved, the step before first is the base
static void history_io_range(i32 *first, i32 *end, i32 *undos, i32 *redos) {
	i32 n    = history_steps->length;
	i32 done = n - 1 - history_redos; // Last step done
	*first   = 1;
	*end     = n;
	for (i32 i = done; i >= *first; --i) {
		if (!history_io_savable(history_steps->buffer[i])) {
			*first = i + 1;
			break;
		}
	}
	for (i32 i = done + 1; i < *end; ++i) {
		if (!history_io_savable(history_steps->buffer[i])) {
			*end = i;
			break;
		}
	}
	*undos = done + 1 - *first;
	*redos = *end - (done + 1);
	if (*undos < 0) { // Nothing saved on the done side, the redo branch neither
		*undos = 0;
		*redos = 0;
		*end   = *first;
	}
}

static void hio_key(char *k) {
	armpack_encode_string(k);
}

static void hio_i32(char *k, i32 v) {
	hio_key(k);
	armpack_encode_i32(v);
}

static void hio_f32(char *k, f32 v) {
	hio_key(k);
	armpack_encode_f32(v);
}

static void hio_str(char *k, char *v) {
	hio_key(k);
	if (v != NULL) {
		armpack_encode_string(v);
	}
	else {
		armpack_encode_null();
	}
}

static void hio_f32s(char *k, f32 *v, i32 n) {
	f32_array_t a = {.buffer = v, .length = n, .capacity = n};
	hio_key(k);
	armpack_encode_array_f32(&a);
}

static void hio_i32s(char *k, i32_array_t *a) {
	hio_key(k);
	if (a != NULL) {
		armpack_encode_array_i32(a);
	}
	else {
		armpack_encode_null();
	}
}

static void hio_hashes(char *k, hash128_t *h, i32 n) {
	i32_array_t *a = i32_array_create(n * 4);
	for (i32 i = 0; i < n; ++i) {
		memcpy(&a->buffer[i * 4], &h[i].lo, 8);
		memcpy(&a->buffer[i * 4 + 2], &h[i].hi, 8);
	}
	hio_i32s(k, a);
	array_free(a);
	free(a);
}

static void hio_f32_array(char *k, f32_array_t *a) {
	hio_key(k);
	if (a != NULL) {
		armpack_encode_array_f32(a);
	}
	else {
		armpack_encode_null();
	}
}

static f32_array_t *hio_get_f32_array(any_map_t *m, char *k) {
	f32_array_t *a = any_map_get(m, k);
	if (a == NULL) {
		return NULL;
	}
	f32_array_t *c = f32_array_create(a->length);
	if (a->length > 0) {
		memcpy(c->buffer, a->buffer, sizeof(f32) * a->length);
	}
	return c;
}

static void hio_swatch(swatch_color_t *s, f32 *out) {
	f32 v[9] = {*(f32 *)&s->base, s->opacity, s->occlusion, s->roughness, s->metallic, *(f32 *)&s->normal, s->emission, s->height, s->subsurface};
	memcpy(out, v, sizeof(v));
}

#define HISTORY_IO_STEP_KEYS 44

static void history_io_encode_step(history_step_t *s, hash128_t canvas) {
	armpack_encode_map(HISTORY_IO_STEP_KEYS);
	hio_str("name", s->name);
	hio_i32("action", s->action);
	hio_i32("layer", s->layer);
	hio_i32("layer_type", s->layer_type);
	hio_i32("layer_parent", s->layer_parent);
	hio_i32("object", s->object);
	hio_i32("material", s->material);
	hio_i32("brush", s->brush);
	hio_f32("layer_opacity", s->layer_opacity);
	hio_i32("layer_object", s->layer_object);
	hio_i32("layer_blending", s->layer_blending);
	hio_i32("prev_order", s->prev_order);
	hio_i32("canvas_type", s->canvas_type);
	hio_i32("canvas_group", s->canvas_group);
	hio_str("layer_name", s->layer_name);
	hio_f32_array("mask_effects", s->mask_effects);
	hio_f32_array("fill", s->fill);
	hio_i32("swatch", s->swatch);
	f32 sw[9] = {0};
	if (s->swatch_color != NULL) {
		hio_swatch(s->swatch_color, sw);
	}
	hio_f32s("swatch_color", sw, s->swatch_color != NULL ? 9 : 0);
	i32 nsw  = s->swatch_colors != NULL ? s->swatch_colors->length : -1;
	f32 *all = calloc(nsw > 0 ? nsw * 9 : 1, sizeof(f32));
	for (i32 i = 0; i < nsw; ++i) {
		hio_swatch(s->swatch_colors->buffer[i], all + i * 9);
	}
	hio_f32s("swatch_colors", all, nsw > 0 ? nsw * 9 : 0);
	free(all);
	hio_i32("swatch_colors_n", nsw);
	hio_f32s("object_loc", &s->object_loc.x, 4);
	hio_f32s("object_rot", &s->object_rot.x, 4);
	hio_f32s("object_scale", &s->object_scale.x, 4);
	hio_i32("merge_start", s->merge_start);
	hio_i32("merge_length", s->merge_length);
	i32 nm = s->merge_layers != NULL ? s->merge_layers->length : -1;
	hio_i32("merge_n", nm);
	hio_hashes("records", s->records, s->records != NULL && nm > 0 ? nm : 0);
	hio_i32s("merge_parents", s->merge_parents);
	hio_i32("with_prev", s->with_prev);
	hio_i32("serial", (i32)s->serial);
	hio_i32("layer_uid", (i32)s->layer_uid);
	hio_i32("parent_uid", (i32)s->parent_uid);
	hio_i32("material_uid", (i32)s->material_uid);
	hio_str("image_file", s->image_file);
	hio_i32s("tl_moves", s->tl_moves);
	hio_hashes("canvas", &canvas, hash128_is_none(canvas) ? 0 : 1);
	slot_layer_t *p         = s->path; // Path props: points, world, camera, parents; curved, text, tool, material uid
	f32_array_t  *pf[3]     = {p != NULL ? p->path_points : NULL, p != NULL ? p->path_points_world : NULL, p != NULL ? p->path_points_camera : NULL};
	char         *pk[3]     = {"path_points", "path_world", "path_camera"};
	for (i32 i = 0; i < 3; ++i) {
		hio_key(pk[i]);
		if (pf[i] != NULL) {
			armpack_encode_array_f32(pf[i]);
		}
		else {
			armpack_encode_null();
		}
	}
	hio_i32s("path_parents", p != NULL ? p->path_points_parent : NULL);
	i32         pp[4] = {p != NULL ? p->path_curved : 0, p != NULL ? p->path_text : 0, p != NULL ? p->path_tool : -1,
	                     p != NULL && p->path_material != NULL ? (i32)p->path_material->uid : 0};
	i32_array_t pa    = {.buffer = pp, .length = 4, .capacity = 4};
	hio_i32s("path_props", p != NULL ? &pa : NULL);
	hio_f32_array("fill_mask", s->fills[0]);
	hio_f32_array("fill_layer", s->fills[1]);
}

// Upper bound of a step's encoded bytes
static u32 history_io_step_size(history_step_t *s) {
	u32 n = 4096;
	n += s->mask_effects != NULL ? s->mask_effects->length * 4 : 0;
	n += (FILL_ENCODE_LENGTH * 4 + 16) * 3;
	n += s->swatch_colors != NULL ? s->swatch_colors->length * 40 : 0;
	n += s->merge_layers != NULL ? s->merge_layers->length * 24 : 0;
	n += s->merge_parents != NULL ? s->merge_parents->length * 4 : 0;
	n += s->tl_moves != NULL ? s->tl_moves->length * 4 : 0;
	if (s->path != NULL) {
		void *pa[4] = {s->path->path_points, s->path->path_points_world, s->path->path_points_camera, s->path->path_points_parent};
		for (i32 i = 0; i < 4; ++i) {
			n += pa[i] != NULL ? ((f32_array_t *)pa[i])->length * 4 : 0; // i32 and f32 arrays alike
		}
	}
	n += s->name != NULL ? string_length(s->name) : 0;
	n += s->layer_name != NULL ? string_length(s->layer_name) : 0;
	n += s->image_file != NULL ? string_length(s->image_file) : 0;
	return n;
}

u8 *history_io_encode(u32 *len) {
	timeline_finalize();
	i32 first, end, undos, redos;
	history_io_range(&first, &end, &undos, &redos);
	u32 size = 1024;
	for (i32 i = first; i < end; ++i) {
		size += history_io_step_size(history_steps->buffer[i]);
	}
	hash128_t *old       = history_io_canvases; // Released once the new ones hold their references
	i32        old_count = history_io_canvas_count;
	history_io_canvases     = NULL;
	history_io_canvas_count = 0;

	// Canvases first: their encoding uses the encoder too
	hash128_t *canvases = calloc(end - first + 1, sizeof(hash128_t));
	for (i32 i = first; i < end; ++i) {
		history_step_t *s = history_steps->buffer[i];
		if (s->canvas != NULL) {
			canvases[i - first]                            = history_io_canvas_put(s->canvas);
			history_io_canvases                            = realloc(history_io_canvases, sizeof(hash128_t) * (history_io_canvas_count + 1));
			history_io_canvases[history_io_canvas_count++] = canvases[i - first];
		}
	}
	u8 *b = malloc(size);
	armpack_encode_start(b);
	armpack_encode_map(5);
	hio_i32("version", HISTORY_IO_VERSION);
	hio_i32("serial_next", (i32)history_serial_peek());
	hio_i32("undos", undos);
	hio_i32("redos", redos);
	hio_key("steps");
	armpack_encode_array(end - first);
	for (i32 i = first; i < end; ++i) {
		history_io_encode_step(history_steps->buffer[i], canvases[i - first]);
	}
	*len = armpack_encode_end();
	free(canvases);
	for (i32 i = 0; i < old_count; ++i) {
		hstore_unref(old[i]);
	}
	free(old);
	free(history_io_canvas_buf);
	history_io_canvas_buf = NULL;
	return b;
}

static hash128_t hio_get_hash(any_map_t *m, char *k, i32 i) {
	i32_array_t *a = any_map_get(m, k);
	hash128_t    h = HASH128_NONE;
	if (a != NULL && a->length >= (i + 1) * 4) {
		memcpy(&h.lo, &a->buffer[i * 4], 8);
		memcpy(&h.hi, &a->buffer[i * 4 + 2], 8);
	}
	return h;
}

static i32_array_t *hio_get_i32s(any_map_t *m, char *k) {
	i32_array_t *a = any_map_get(m, k);
	if (a == NULL) {
		return NULL;
	}
	i32_array_t *c = i32_array_create(a->length);
	if (a->length > 0) {
		memcpy(c->buffer, a->buffer, sizeof(i32) * a->length);
	}
	return c;
}

static f32 *hio_get_f32s(any_map_t *m, char *k, i32 n) {
	f32_array_t *a = any_map_get(m, k);
	return a != NULL && a->length >= n && n > 0 ? a->buffer : NULL;
}

static char *hio_get_str(any_map_t *m, char *k) {
	char *s = any_map_get(m, k);
	return s != NULL ? string_copy(s) : NULL;
}

static swatch_color_t *hio_swatch_from(f32 *v) {
	swatch_color_t *s = ALLOC_INIT(swatch_color_t, {0});
	memcpy(&s->base, &v[0], 4);
	s->opacity    = v[1];
	s->occlusion  = v[2];
	s->roughness  = v[3];
	s->metallic   = v[4];
	memcpy(&s->normal, &v[5], 4);
	s->emission   = v[6];
	s->height     = v[7];
	s->subsurface = v[8];
	return s;
}

static history_step_t *history_io_decode_step(any_map_t *m) {
	history_step_t *s = ALLOC_INIT(history_step_t, {0});
	s->name           = hio_get_str(m, "name");
	s->action         = armpack_map_get_i32(m, "action");
	s->layer          = armpack_map_get_i32(m, "layer");
	s->layer_type     = armpack_map_get_i32(m, "layer_type");
	s->layer_parent   = armpack_map_get_i32(m, "layer_parent");
	s->object         = armpack_map_get_i32(m, "object");
	s->material       = armpack_map_get_i32(m, "material");
	s->brush          = armpack_map_get_i32(m, "brush");
	s->layer_opacity  = armpack_map_get_f32(m, "layer_opacity");
	s->layer_object   = armpack_map_get_i32(m, "layer_object");
	s->layer_blending = armpack_map_get_i32(m, "layer_blending");
	s->prev_order     = armpack_map_get_i32(m, "prev_order");
	s->canvas_type    = armpack_map_get_i32(m, "canvas_type");
	s->canvas_group   = armpack_map_get_i32(m, "canvas_group");
	s->layer_name     = hio_get_str(m, "layer_name");
	f32_array_t *me = any_map_get(m, "mask_effects");
	if (me != NULL) {
		s->mask_effects = f32_array_create(me->length);
		if (me->length > 0) {
			memcpy(s->mask_effects->buffer, me->buffer, sizeof(f32) * me->length);
		}
	}
	i32_array_t *pp = any_map_get(m, "path_props");
	if (pp != NULL && pp->length == 4) {
		slot_layer_t t = {.path_points = any_map_get(m, "path_points"), .path_points_world = any_map_get(m, "path_world"),
		                  .path_points_camera = any_map_get(m, "path_camera"), .path_points_parent = any_map_get(m, "path_parents"),
		                  .path_curved = pp->buffer[0] != 0, .path_text = pp->buffer[1] != 0, .path_tool = pp->buffer[2],
		                  .path_material = slot_material_find_uid((u32)pp->buffer[3])};
		s->path = history_path_copy(&t); // Copies out of the decoded map
	}
	s->swatch = armpack_map_get_i32(m, "swatch");
	f32 *sc   = hio_get_f32s(m, "swatch_color", 9);
	if (sc != NULL) {
		s->swatch_color = hio_swatch_from(sc);
	}
	i32 nsw = armpack_map_get_i32(m, "swatch_colors_n");
	if (nsw >= 0 && any_map_get(m, "swatch_colors_n") != NULL) {
		s->swatch_colors = any_array_create_from_raw((void *[]){}, 0);
		f32 *all         = hio_get_f32s(m, "swatch_colors", nsw * 9);
		for (i32 i = 0; all != NULL && i < nsw; ++i) {
			any_array_push(s->swatch_colors, hio_swatch_from(all + i * 9));
		}
	}
	f32 *v = hio_get_f32s(m, "object_loc", 4);
	if (v != NULL) {
		s->object_loc = (vec4_t){v[0], v[1], v[2], v[3]};
	}
	v = hio_get_f32s(m, "object_rot", 4);
	if (v != NULL) {
		s->object_rot = (quat_t){v[0], v[1], v[2], v[3]};
	}
	v = hio_get_f32s(m, "object_scale", 4);
	if (v != NULL) {
		s->object_scale = (vec4_t){v[0], v[1], v[2], v[3]};
	}
	s->merge_start   = armpack_map_get_i32(m, "merge_start");
	s->merge_length  = armpack_map_get_i32(m, "merge_length");
	s->merge_parents = hio_get_i32s(m, "merge_parents");
	i32 nm           = armpack_map_get_i32(m, "merge_n");
	if (nm >= 0 && any_map_get(m, "merge_n") != NULL) {
		s->merge_layers = any_array_create_from_raw((void *[]){}, 0);
		s->records      = calloc(nm > 0 ? nm : 1, sizeof(hash128_t));
		for (i32 i = 0; i < nm; ++i) {
			s->records[i] = hio_get_hash(m, "records", i);
			hstore_ref(s->records[i]); // The step's own, as history_records_take holds them
			slot_layer_t *l = history_record_layer(s->records[i]);
			any_array_push(s->merge_layers, l != NULL ? l : slot_layer_create("_", LAYER_SLOT_TYPE_GROUP, NULL)); // Missing: an empty group
		}
	}
	s->with_prev    = armpack_map_get_i32(m, "with_prev") != 0;
	s->serial       = (u32)armpack_map_get_i32(m, "serial");
	s->layer_uid    = (u32)armpack_map_get_i32(m, "layer_uid");
	s->parent_uid   = (u32)armpack_map_get_i32(m, "parent_uid");
	s->material_uid = (u32)armpack_map_get_i32(m, "material_uid");
	s->image_file   = hio_get_str(m, "image_file");
	s->tl_moves     = hio_get_i32s(m, "tl_moves");
	hash128_t c     = hio_get_hash(m, "canvas", 0);
	if (!hash128_is_none(c)) {
		u32 len  = 0;
		u8 *data = hstore_get(c, &len);
		if (data != NULL) {
			buffer_t b = {.buffer = data, .length = len};
			s->canvas  = ui_node_canvas_decode(&b);
			free(data);
		}
	}
	s->fill     = hio_get_f32_array(m, "fill");
	s->fills[0] = hio_get_f32_array(m, "fill_mask");
	s->fills[1] = hio_get_f32_array(m, "fill_layer");
	return s;
}

// A step loaded is usable: the timelines it moves are there, a canvas step has its canvas, its layer is in the project
// or among the layers of a step
static bool history_io_step_ok(history_step_t *s, any_array_t *steps) {
	for (i32 i = 0; s->tl_moves != NULL && i + 2 < s->tl_moves->length; i += 3) {
		timeline_t *tl = timeline_find((u32)s->tl_moves->buffer[i]);
		if (tl == NULL || s->tl_moves->buffer[i + 1] > tl->count || s->tl_moves->buffer[i + 2] > tl->count) {
			return false;
		}
	}
	if (s->action == HISTORY_ACTION_EDIT_NODES && s->canvas == NULL) {
		return false;
	}
	if (s->layer_uid == 0 || slot_layer_find_uid(s->layer_uid) != NULL) {
		return true;
	}
	for (i32 i = 0; i < steps->length; ++i) {
		history_step_t *o = steps->buffer[i];
		for (i32 j = 0; o->merge_layers != NULL && j < o->merge_layers->length; ++j) {
			if (((slot_layer_t *)o->merge_layers->buffer[j])->uid == s->layer_uid) {
				return true;
			}
		}
	}
	return false;
}

bool history_io_decode(u8 *data, u32 len) {
	buffer_t   b = {.buffer = data, .length = len};
	any_map_t *m = data != NULL && len > 0 ? armpack_decode_to_map(&b) : NULL;
	if (m == NULL || armpack_map_get_i32(m, "version") != HISTORY_IO_VERSION) {
		return false;
	}
	any_array_t *list  = any_map_get(m, "steps");
	i32          undos = armpack_map_get_i32(m, "undos");
	i32          redos = armpack_map_get_i32(m, "redos");
	i32          n     = list != NULL ? list->length : 0;
	if (undos < 0 || redos < 0 || undos + redos != n) {
		return false;
	}
	any_array_t *steps = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < n; ++i) {
		any_array_push(steps, history_io_decode_step(list->buffer[i]));
	}
	// Layers out of the project: ids no layer of the project has, their render targets are named by them
	i32 next_id = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		i32 id  = g_project->_->layers->buffer[i]->id;
		next_id = id >= next_id ? id + 1 : next_id;
	}
	for (i32 i = 0; i < n; ++i) {
		history_step_t *s = steps->buffer[i];
		for (i32 j = 0; s->merge_layers != NULL && j < s->merge_layers->length; ++j) {
			slot_layer_t *l = s->merge_layers->buffer[j];
			l->id           = next_id++;
			l->ext          = string("%d", l->id);
		}
	}
	// Steps checked by uid: the history starts after the last done one that does not fit, the redo branch ends before
	// the first
	i32 first = 0;
	i32 end   = n;
	for (i32 i = undos - 1; i >= 0; --i) {
		if (!history_io_step_ok(steps->buffer[i], steps)) {
			first = i + 1;
			break;
		}
	}
	for (i32 i = undos; i < n; ++i) {
		if (!history_io_step_ok(steps->buffer[i], steps)) {
			end = i;
			break;
		}
	}
	any_array_t *kept = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < n; ++i) {
		history_step_t *s = steps->buffer[i];
		if (i >= first && i < end) {
			any_array_push(kept, s);
		}
		else {
			history_step_dispose(s);
		}
	}
	array_free(steps);
	free(steps);
	history_steps_replace(kept, undos - first, end > undos ? end - undos : 0, (u32)armpack_map_get_i32(m, "serial_next"));
	array_free(kept);
	free(kept);
	return true;
}

i32 history_io_roots(hash128_t **out) {
	i32 cap = 16, n = 0;
	*out    = malloc(sizeof(hash128_t) * cap);
	for (i32 i = 0; history_steps != NULL && i < history_steps->length; ++i) {
		history_step_t *s  = history_steps->buffer[i];
		i32             nm = s->records != NULL && s->merge_layers != NULL ? s->merge_layers->length : 0;
		for (i32 j = 0; j < nm + 1; ++j) {
			hash128_t h = HASH128_NONE;
			if (j < nm) {
				h = s->records[j];
			}
			else if (s->canvas != NULL) {
				h = history_io_canvas_put(s->canvas); // Stored by the last encode: a dedup hit
				hstore_unref(h);
				free(history_io_canvas_buf);
				history_io_canvas_buf = NULL;
			}
			if (hash128_is_none(h)) {
				continue;
			}
			if (n == cap) {
				cap *= 2;
				*out = realloc(*out, sizeof(hash128_t) * cap);
			}
			(*out)[n++] = h;
		}
	}
	return n;
}

// Bytes the store holds for history and timelines
static i64 history_io_bytes() {
	hstore_stats_t st = hstore_stats();
	return st.ram_bytes + st.disk_bytes - st.dead_bytes;
}

void history_io_thin(i64 budget) {
	timeline_finalize();
	hstore_trim(false);
	for (i32 round = 0; round < 16 && history_io_bytes() > budget; ++round) {
		if (timeline_thin_all(round) == 0) {
			break;
		}
		hstore_trim(false);
	}
	while (history_io_bytes() > budget && history_undos > 0 && history_steps->length > 1 + history_redos) {
		history_drop_oldest();
		hstore_trim(false);
	}
}

void history_io_clear() {
	timeline_finalize();
	while (history_redos > 0) {
		history_step_dispose(array_pop(history_steps));
		history_redos--;
	}
	timeline_truncate_all();
	while (history_steps->length > 1) {
		history_drop_oldest();
	}
	history_undos = 0;
	timeline_clear_states_all();
	history_io_canvases_release();
	hstore_trim(false);
}

// Scripts: the history encoded, dropped and decoded again as a project open does (the blobs it holds kept meanwhile).
// Encoded bytes, -1 when the decode failed.
i32 history_io_test_roundtrip(i32 _) {
	history_flush_restores(NULL);
	history_tiles_finalize();
	u32        len   = 0;
	u8        *b     = history_io_encode(&len);
	hash128_t *roots = NULL;
	i32        n     = history_io_roots(&roots);
	for (i32 i = 0; i < n; ++i) {
		hstore_ref(roots[i]);
	}
	any_array_t *none = any_array_create_from_raw((void *[]){}, 0);
	history_steps_replace(none, 0, 0, 0);
	array_free(none);
	free(none);
	bool ok = history_io_decode(b, len);
	for (i32 i = 0; i < n; ++i) {
		hstore_unref(roots[i]);
	}
	free(roots);
	free(b);
	return ok ? (i32)len : -1;
}
