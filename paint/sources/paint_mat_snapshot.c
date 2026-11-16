#include "global.h"

// Material snapshots (stroke_rec.h): what a material was when a stroke painted with it, so editing it later does not
// change past strokes. HSTORE_SNAPSHOT blob: a header of the paint settings, then the node canvas (armpack).
// Not yet: nested groups, image nodes by asset hash (they resolve by name), Material Maker graph data beyond the canvas.

#define PAINT_MAT_SNAPSHOT_MAGIC 0x31534d50 // "PMS1"

typedef struct paint_mat_snapshot_head {
	u32 magic;
	i32 id;
	f32 uv_scale;
	f32 uv_angle;
	u8  paint[9]; // base opac occ rough met nor height emis subs
	u8  pad[3];
	u32 canvas_len;
} paint_mat_snapshot_head_t;

#define PAINT_MAT_SNAPSHOT_SCRATCH (4 * 1024 * 1024)

static u8 *paint_mat_snapshot_scratch = NULL;

// Materials made from snapshots, and the project's own by the hash of what they are now
typedef struct {
	hash128_t        h;
	slot_material_t *m;
	bool             own; // Instantiated here
} paint_mat_snapshot_entry_t;

static paint_mat_snapshot_entry_t *paint_mat_snapshot_cache   = NULL;
static i32                         paint_mat_snapshot_count   = 0;
static i32                         paint_mat_snapshot_cap     = 0;

static u32 paint_mat_snapshot_encode(slot_material_t *m) {
	if (paint_mat_snapshot_scratch == NULL) {
		paint_mat_snapshot_scratch = malloc(PAINT_MAT_SNAPSHOT_SCRATCH);
	}
	paint_mat_snapshot_head_t h = {.magic = PAINT_MAT_SNAPSHOT_MAGIC, .id = m->id, .uv_scale = m->uv_scale, .uv_angle = m->uv_angle};
	bool flags[9] = {m->paint_base, m->paint_opac, m->paint_occ, m->paint_rough, m->paint_met, m->paint_nor, m->paint_height, m->paint_emis, m->paint_subs};
	for (i32 i = 0; i < 9; ++i) {
		h.paint[i] = flags[i];
	}
	armpack_encode_start(paint_mat_snapshot_scratch + sizeof(h));
	ui_node_canvas_encode(m->canvas);
	h.canvas_len = armpack_encode_end();
	memcpy(paint_mat_snapshot_scratch, &h, sizeof(h));
	return sizeof(h) + h.canvas_len;
}

static void paint_mat_snapshot_cache_add(hash128_t h, slot_material_t *m, bool own) {
	for (i32 i = 0; i < paint_mat_snapshot_count; ++i) {
		if (hash128_eq(paint_mat_snapshot_cache[i].h, h) && !own) {
			paint_mat_snapshot_cache[i].m = m; // The project's material as it is now
			return;
		}
	}
	if (paint_mat_snapshot_count == paint_mat_snapshot_cap) {
		paint_mat_snapshot_cap   = paint_mat_snapshot_cap == 0 ? 16 : paint_mat_snapshot_cap * 2;
		paint_mat_snapshot_cache = realloc(paint_mat_snapshot_cache, sizeof(paint_mat_snapshot_entry_t) * paint_mat_snapshot_cap);
	}
	paint_mat_snapshot_cache[paint_mat_snapshot_count++] = (paint_mat_snapshot_entry_t){.h = h, .m = m, .own = own};
}

// The snapshot of m as it is now, a reference added
hash128_t paint_mat_snapshot_intern(slot_material_t *m) {
	if (m == NULL || m->canvas == NULL) {
		return HASH128_NONE;
	}
	u32       len = paint_mat_snapshot_encode(m);
	hash128_t h   = hstore_put(HSTORE_SNAPSHOT, paint_mat_snapshot_scratch, len);
	paint_mat_snapshot_cache_add(h, m, false);
	return h;
}

// A material that paints as the snapshot did: the project's material when it is still the same, else a hidden one
slot_material_t *paint_mat_snapshot_instantiate(hash128_t h) {
	for (i32 i = 0; i < paint_mat_snapshot_count; ++i) {
		paint_mat_snapshot_entry_t *e = &paint_mat_snapshot_cache[i];
		if (!hash128_eq(e->h, h)) {
			continue;
		}
		if (e->own) {
			return e->m;
		}
		if (array_index_of(g_project->_->materials, e->m) >= 0 && e->m->canvas != NULL) { // Still what it was?
			u32 len = paint_mat_snapshot_encode(e->m);
			if (hash128_eq(hstore_hash(paint_mat_snapshot_scratch, len), h)) {
				return e->m;
			}
		}
	}
	u32 len  = 0;
	u8 *data = hstore_get(h, &len);
	if (data == NULL || len < sizeof(paint_mat_snapshot_head_t)) {
		free(data);
		return NULL;
	}
	paint_mat_snapshot_head_t head;
	memcpy(&head, data, sizeof(head));
	if (head.magic != PAINT_MAT_SNAPSHOT_MAGIC || sizeof(head) + head.canvas_len > len) {
		free(data);
		return NULL;
	}
	buffer_t          b      = {.buffer = data + sizeof(head), .length = head.canvas_len};
	ui_node_canvas_t *canvas = util_clone_canvas(armpack_decode(&b));
	free(data);
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, canvas);
	m->id              = head.id;
	m->uv_scale        = head.uv_scale;
	m->uv_angle        = head.uv_angle;
	bool *flags[9]     = {&m->paint_base, &m->paint_opac, &m->paint_occ, &m->paint_rough, &m->paint_met, &m->paint_nor, &m->paint_height, &m->paint_emis, &m->paint_subs};
	for (i32 i = 0; i < 9; ++i) {
		*flags[i] = head.paint[i] != 0;
	}
	hstore_ref(h);
	paint_mat_snapshot_cache_add(h, m, true);
	return m;
}

// New or opened project: materials made here belong to the old one
void paint_mat_snapshot_reset() {
	paint_mat_snapshot_count = 0;
}
