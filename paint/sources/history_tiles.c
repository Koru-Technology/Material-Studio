
#include "global.h"

// Tiled undo for paint steps: the layer is cut into a 64 x 64 grid and a step keeps only the tiles its stroke changed.
// The stroke starts with a copy of the whole layer (the snapshot). Once the stroke is over, a pass compares the layer with
// the snapshot per tile and the changed tiles are copied from the snapshot into pages of a tile pool. Undo and redo swap
// the tiles of the layer with the ones in the pool, the snapshot holds the layer tiles in between.
// The pool grows a page at a time up to the undo memory budget, past it the oldest steps are dropped.
// Masks and gpus without region copies keep a whole layer copy per step (history_copy_to_undo).

#ifdef IRON_METAL
void gpu_copy_regions_begin(void);
void gpu_copy_region(gpu_texture_t *dst, int dx, int dy, gpu_texture_t *src, int sx, int sy, int w, int h);
void gpu_copy_regions_end(void);
#endif

#define HISTORY_TILES_GRID      64 // Tiles per layer side
#define HISTORY_TILES_PAGE_GRID 16 // Tiles per page side
#define HISTORY_TILES_PAGE      (HISTORY_TILES_PAGE_GRID * HISTORY_TILES_PAGE_GRID)

i32 history_tiles_size       = 0; // Texels per tile side
i32 history_tiles_block      = 0; // Texels per side of a block the change pass checks in one thread
i32 history_tiles_blocks     = 0; // Blocks per layer side
i32 history_tiles_generation = 1; // Steps of an older generation lost their tiles (layer size or format changed)

typedef struct {
	gpu_texture_t *tex[3]; // Base, normal, pack
} history_tiles_page_t;

static history_tiles_page_t *history_tiles_pages      = NULL;
static i32                   history_tiles_page_count = 0;
static i32_array_t          *history_tiles_free       = NULL; // Free pool slots
static i32                   history_tiles_layer_w    = 0;
static i32                   history_tiles_layer_h    = 0;
static i32                   history_tiles_format     = -1;
static history_step_t       *history_tiles_pending    = NULL; // Step whose tiles are still to be taken from the snapshot
static slot_layer_t         *history_tiles_pending_l  = NULL;

static char *history_tiles_snap_names[3] = {"undo_snap0", "undo_snap1", "undo_snap2"};

static bool history_tiles_enabled = true; // Off for comparisons, see history_tiles_set_enabled

void history_tiles_set_enabled(bool on) {
	history_tiles_finalize();
	history_tiles_enabled = on;
}

bool history_tiles_supported(slot_layer_t *l) {
#ifdef IRON_METAL
	if (!history_tiles_enabled || l == NULL || !slot_layer_is_layer(l) || l->texpaint == NULL || l->texpaint_nor == NULL || l->texpaint_pack == NULL) {
		return false;
	}
	return l->texpaint->width % HISTORY_TILES_GRID == 0 && l->texpaint->height % HISTORY_TILES_GRID == 0 &&
	       l->texpaint->format == l->texpaint_nor->format && l->texpaint->format == l->texpaint_pack->format;
#else
	return false;
#endif
}

// Draw passes in progress end for the copies and continue after
static bool           history_tiles_resume  = false;
static gpu_texture_t *history_tiles_resume_target = NULL;

static void history_tiles_pause_draw() {
	history_tiles_resume        = gpu_in_use;
	history_tiles_resume_target = _draw_current;
	if (history_tiles_resume) {
		draw_end();
	}
}

static void history_tiles_resume_draw() {
	if (history_tiles_resume) {
		draw_begin(history_tiles_resume_target, false, 0);
	}
}

static char *history_tiles_format_name(gpu_texture_format_t f) {
	return f == GPU_TEXTURE_FORMAT_RGBA64 ? "RGBA64" : f == GPU_TEXTURE_FORMAT_RGBA128 ? "RGBA128" : "RGBA32";
}

static i32 history_tiles_bytes_per_texel() {
	return history_tiles_format == GPU_TEXTURE_FORMAT_RGBA64 ? 8 : history_tiles_format == GPU_TEXTURE_FORMAT_RGBA128 ? 16 : 4;
}

i64 history_tiles_page_bytes() {
	i64 side = (i64)HISTORY_TILES_PAGE_GRID * history_tiles_size;
	return side * side * history_tiles_bytes_per_texel() * 3;
}

i64 history_tiles_memory() {
	return (i64)history_tiles_page_count * history_tiles_page_bytes();
}

static gpu_texture_t *history_tiles_snapshot(i32 i) {
	render_target_t *rt = any_map_get(render_path_render_targets, history_tiles_snap_names[i]);
	return rt != NULL ? rt->_image : NULL;
}

static void history_tiles_free_snapshot() {
	for (i32 i = 0; i < 3; ++i) {
		render_target_t *rt = any_map_get(render_path_render_targets, history_tiles_snap_names[i]);
		if (rt != NULL) {
			gpu_delete_texture(rt->_image);
			map_delete(render_path_render_targets, history_tiles_snap_names[i]);
		}
	}
}

static void history_tiles_ensure_snapshot() {
	for (i32 i = 0; i < 3; ++i) {
		render_target_t *rt = any_map_get(render_path_render_targets, history_tiles_snap_names[i]);
		if (rt != NULL && (rt->width != history_tiles_layer_w || rt->height != history_tiles_layer_h ||
		                   !string_equals(rt->format, history_tiles_format_name(history_tiles_format)))) {
			gpu_delete_texture(rt->_image);
			map_delete(render_path_render_targets, history_tiles_snap_names[i]);
			rt = NULL;
		}
		if (rt == NULL) {
			render_target_t *t = render_target_create();
			t->name            = history_tiles_snap_names[i];
			t->width           = history_tiles_layer_w;
			t->height          = history_tiles_layer_h;
			t->format          = history_tiles_format_name(history_tiles_format);
			render_path_create_render_target(t);
		}
	}
}

// Drops every tile, steps kept so far can no longer be undone
void history_tiles_reset() {
	for (i32 i = 0; i < history_tiles_page_count; ++i) {
		for (i32 j = 0; j < 3; ++j) {
			gpu_delete_texture(history_tiles_pages[i].tex[j]);
		}
	}
	free(history_tiles_pages);
	history_tiles_pages      = NULL;
	history_tiles_page_count = 0;
	if (history_tiles_free != NULL) {
		history_tiles_free->length = 0;
	}
	history_tiles_pending   = NULL;
	history_tiles_pending_l = NULL;
	history_tiles_generation++;
}

// Pool slot to texel position in its page
static void history_tiles_slot_pos(i32 slot, gpu_texture_t **tex, i32 *x, i32 *y) {
	history_tiles_page_t *page = &history_tiles_pages[slot / HISTORY_TILES_PAGE];
	i32                   s    = slot % HISTORY_TILES_PAGE;
	for (i32 i = 0; i < 3; ++i) {
		tex[i] = page->tex[i];
	}
	*x = (s % HISTORY_TILES_PAGE_GRID) * history_tiles_size;
	*y = (s / HISTORY_TILES_PAGE_GRID) * history_tiles_size;
}

static void history_tiles_add_page() {
	i32 side            = HISTORY_TILES_PAGE_GRID * history_tiles_size;
	history_tiles_pages = realloc(history_tiles_pages, sizeof(history_tiles_page_t) * (history_tiles_page_count + 1));
	for (i32 i = 0; i < 3; ++i) {
		history_tiles_pages[history_tiles_page_count].tex[i] = gpu_create_render_target(side, side, history_tiles_format);
	}
	if (history_tiles_free == NULL) {
		history_tiles_free = i32_array_create(0);
	}
	for (i32 s = HISTORY_TILES_PAGE - 1; s >= 0; --s) {
		i32_array_push(history_tiles_free, history_tiles_page_count * HISTORY_TILES_PAGE + s);
	}
	history_tiles_page_count++;
}

void history_tiles_release(history_step_t *step) {
	if (step->undo_tiles != NULL && step->undo_tiles_gen == history_tiles_generation && history_tiles_free != NULL) {
		for (i32 i = 1; i < step->undo_tiles->length; i += 2) {
			i32_array_push(history_tiles_free, step->undo_tiles->buffer[i]);
		}
	}
	if (step->undo_tiles != NULL) {
		array_free(step->undo_tiles);
		free(step->undo_tiles);
		step->undo_tiles = NULL;
	}
	if (history_tiles_pending == step) {
		history_tiles_pending   = NULL;
		history_tiles_pending_l = NULL;
	}
}

// A free pool slot, older steps are dropped while the pool is at the budget
static i32 history_tiles_alloc(history_step_t *step) {
	i64 budget = (i64)g_config->undo_memory_mb * 1024 * 1024;
	while (history_tiles_free == NULL || history_tiles_free->length == 0) {
		bool can_drop = history_steps->length > 1 && history_steps->buffer[0] != step;
		if (history_tiles_memory() + history_tiles_page_bytes() <= budget || !can_drop) {
			history_tiles_add_page();
			break;
		}
		history_drop_oldest();
	}
	return i32_array_pop(history_tiles_free);
}

// Takes the changed tiles of the pending step from the snapshot
void history_tiles_finalize() {
#ifdef IRON_METAL
	history_step_t *step = history_tiles_pending;
	slot_layer_t   *l    = history_tiles_pending_l;
	if (step == NULL) {
		return;
	}
	history_tiles_pending   = NULL;
	history_tiles_pending_l = NULL;
	if (l->texpaint == NULL || l->texpaint->width != history_tiles_layer_w || l->texpaint->height != history_tiles_layer_h) {
		return; // Layer resized or deleted since, the step stays without tiles
	}
	history_tiles_pause_draw();

	render_target_t *diff = any_map_get(render_path_render_targets, "undo_tiles_diff");
	if (diff != NULL && diff->width != history_tiles_blocks) {
		gpu_delete_texture(diff->_image);
		map_delete(render_path_render_targets, "undo_tiles_diff");
		diff = NULL;
	}
	if (diff == NULL) {
		diff         = render_target_create();
		diff->name   = "undo_tiles_diff";
		diff->width  = history_tiles_blocks;
		diff->height = history_tiles_blocks;
		diff->format = "RGBA32";
		render_path_create_render_target(diff);
	}
	render_path_set_target("undo_tiles_diff", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	render_path_bind_target("undo_snap0", "tex0");
	render_path_bind_target("undo_snap1", "tex1");
	render_path_bind_target("undo_snap2", "tex2");
	render_path_bind_target(string_tmp("texpaint%d", l->id), "tex3");
	render_path_bind_target(string_tmp("texpaint_nor%d", l->id), "tex4");
	render_path_bind_target(string_tmp("texpaint_pack%d", l->id), "tex5");
	render_path_draw_shader("Scene/undo_tiles_pass/undo_tiles_pass");
	buffer_t *changed = gpu_get_texture_pixels(diff->_image);

	step->undo_tiles     = i32_array_create(0);
	step->undo_tiles_gen = history_tiles_generation;
	i32 t                = history_tiles_size;
	i32 per_tile         = history_tiles_size / history_tiles_block; // Blocks per tile side
	for (i32 tile = 0; tile < HISTORY_TILES_GRID * HISTORY_TILES_GRID; ++tile) {
		i32  bx0 = (tile % HISTORY_TILES_GRID) * per_tile;
		i32  by0 = (tile / HISTORY_TILES_GRID) * per_tile;
		bool hit = false;
		for (i32 by = by0; by < by0 + per_tile && !hit; ++by) {
			for (i32 bx = bx0; bx < bx0 + per_tile; ++bx) {
				if (buffer_get_u8(changed, (by * history_tiles_blocks + bx) * 4 + 1) > 127) {
					hit = true;
					break;
				}
			}
		}
		if (hit) {
			i32_array_push(step->undo_tiles, tile);
			i32_array_push(step->undo_tiles, -1);
		}
	}
	gpu_release_texture_pixels(diff->_image);

	for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
		step->undo_tiles->buffer[i + 1] = history_tiles_alloc(step);
	}
	if (step->undo_tiles_gen == history_tiles_generation) { // Not reset while allocating
		gpu_copy_regions_begin();
		for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
			i32            tile = step->undo_tiles->buffer[i];
			gpu_texture_t *page[3];
			i32            px, py;
			history_tiles_slot_pos(step->undo_tiles->buffer[i + 1], page, &px, &py);
			for (i32 j = 0; j < 3; ++j) {
				gpu_copy_region(page[j], px, py, history_tiles_snapshot(j), (tile % HISTORY_TILES_GRID) * t, (tile / HISTORY_TILES_GRID) * t, t, t);
			}
		}
		gpu_copy_regions_end();
	}
	history_tiles_resume_draw();
#endif
}

// Paint step start: the layer is copied into the snapshot
void history_tiles_begin(history_step_t *step, slot_layer_t *l) {
#ifdef IRON_METAL
	history_tiles_finalize();
	gpu_texture_t *tex = l->texpaint;
	if (tex->width != history_tiles_layer_w || tex->height != history_tiles_layer_h || (i32)tex->format != history_tiles_format) {
		history_tiles_reset(); // Tiles of another size or format can not come back
		history_tiles_layer_w = tex->width;
		history_tiles_layer_h = tex->height;
		history_tiles_format  = tex->format;
		history_tiles_size    = tex->width / HISTORY_TILES_GRID;
		history_tiles_block   = history_tiles_size < 8 ? history_tiles_size : (tex->width / 1024 > 8 ? tex->width / 1024 : 8);
		history_tiles_blocks  = tex->width / history_tiles_block;
	}
	step->undo_tiled     = true;
	step->undo_tiles     = NULL;
	step->undo_tiles_gen = history_tiles_generation;
	history_tiles_pause_draw();
	history_tiles_ensure_snapshot();
	gpu_texture_t *src[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	gpu_copy_regions_begin();
	for (i32 j = 0; j < 3; ++j) {
		gpu_copy_region(history_tiles_snapshot(j), 0, 0, src[j], 0, 0, tex->width, tex->height);
	}
	gpu_copy_regions_end();
	history_tiles_resume_draw();
	history_tiles_pending   = step;
	history_tiles_pending_l = l;
#endif
}

// The step whose tiles are still to be taken (the last stroke's, until the next push, undo or redo) and its layer
history_step_t *history_tiles_pending_step(slot_layer_t **layer) {
	if (layer != NULL) {
		*layer = history_tiles_pending_l;
	}
	return history_tiles_pending;
}

// While a tiled stroke paints, the layer as it was before the stroke (blending reads it), NULL otherwise
gpu_texture_t *history_tiles_stroke_source(i32 i) {
	if (history_tiles_pending == NULL || history_tiles_pending_l != g_context->layer) {
		return NULL;
	}
	return history_tiles_snapshot(i);
}

// Undo and redo: the tiles of the layer and the pool trade places
void history_tiles_swap(history_step_t *step, slot_layer_t *l) {
#ifdef IRON_METAL
	history_tiles_finalize();
	if (step->undo_tiles == NULL || step->undo_tiles_gen != history_tiles_generation || l->texpaint == NULL ||
	    l->texpaint->width != history_tiles_layer_w || l->texpaint->height != history_tiles_layer_h) {
		return;
	}
	history_tiles_pause_draw();
	history_tiles_ensure_snapshot(); // Holds the layer tiles meanwhile
	gpu_texture_t *layer[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	i32            t        = history_tiles_size;
	for (i32 phase = 0; phase < 3; ++phase) {
		gpu_copy_regions_begin();
		for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
			i32            tile = step->undo_tiles->buffer[i];
			i32            lx   = (tile % HISTORY_TILES_GRID) * t;
			i32            ly   = (tile / HISTORY_TILES_GRID) * t;
			gpu_texture_t *page[3];
			i32            px, py;
			history_tiles_slot_pos(step->undo_tiles->buffer[i + 1], page, &px, &py);
			for (i32 j = 0; j < 3; ++j) {
				if (phase == 0) {
					gpu_copy_region(history_tiles_snapshot(j), lx, ly, layer[j], lx, ly, t, t);
				}
				else if (phase == 1) {
					gpu_copy_region(layer[j], lx, ly, page[j], px, py, t, t);
				}
				else {
					gpu_copy_region(page[j], px, py, history_tiles_snapshot(j), lx, ly, t, t);
				}
			}
		}
		gpu_copy_regions_end();
	}
	history_tiles_resume_draw();
#endif
}

// Memory warning: only the pool stays
void history_tiles_trim() {
	history_tiles_finalize();
	history_tiles_free_snapshot();
}
