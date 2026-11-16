#include "global.h"

// Tiled undo for paint steps: the layer is cut into a 64 x 64 grid and a step keeps only the tiles its stroke changed.
// The stroke starts with a copy of the whole layer (the snapshot). Once the stroke is over, a pass compares the layer with
// the snapshot per tile and the changed tiles are copied from the snapshot into pages of a tile pool. Undo and redo swap
// the tiles of the layer with the ones in the pool, the snapshot holds the layer tiles in between.
// Two pools: paint layers (base, normal, pack) and masks (one texture). They grow a page at a time up to the undo memory
// budget together. Past it the tiles of the oldest steps go, those steps undo from their timeline then (timeline.c).
// The snapshot is also what the paint shader blends over (_texpaint_undo, history_tiles_stroke_source).

#define HISTORY_TILES_GRID      64 // Tiles per layer side
#define HISTORY_TILES_PAGE_GRID 16 // Tiles per page side
#define HISTORY_TILES_PAGE      (HISTORY_TILES_PAGE_GRID * HISTORY_TILES_PAGE_GRID)

i32 history_tiles_size   = 0; // Texels per tile side, of the pool the change pass runs for
i32 history_tiles_block  = 0; // Texels per side of a block the change pass checks in one thread
i32 history_tiles_blocks = 0; // Blocks per layer side

typedef struct {
	gpu_texture_t *tex[3]; // Base, normal, pack (a mask: base)
} history_tiles_page_t;

typedef struct history_tiles_pool {
	i32                   channels;
	char                 *snap[3]; // Snapshot targets
	char                 *diff;    // Change pass target
	history_tiles_page_t *pages;
	i32                   page_count;
	i32_array_t          *free; // Free slots
	i32                   layer_w;
	i32                   layer_h;
	i32                   format;
	i32                   size; // Texels per tile side
	i32                   block;
	i32                   blocks;
	i32                   generation; // Steps of an older generation lost their tiles (layer size or format changed)
} history_tiles_pool_t;

static history_tiles_pool_t history_tiles_pools[2] = {
    {.channels = 3, .snap = {"undo_snap0", "undo_snap1", "undo_snap2"}, .diff = "undo_tiles_diff", .format = -1, .generation = 1},
    {.channels = 1, .snap = {"undo_snap_mask", NULL, NULL}, .diff = "undo_tiles_diff_mask", .format = -1, .generation = 1},
};

static history_step_t *history_tiles_pending   = NULL; // Step whose tiles are still to be taken from the snapshot
static slot_layer_t   *history_tiles_pending_l = NULL;

static gpu_texture_t *history_tiles_source[3] = {NULL, NULL, NULL}; // Set: what the paint shader blends over instead

static history_tiles_pool_t *history_tiles_pool_of(history_step_t *step) {
	return &history_tiles_pools[step->layer_type == LAYER_SLOT_TYPE_MASK ? 1 : 0];
}

bool history_tiles_supported(slot_layer_t *l) {
	if (l == NULL || l->texpaint == NULL) {
		return false;
	}
	if (l->texpaint->width % HISTORY_TILES_GRID != 0 || l->texpaint->height % HISTORY_TILES_GRID != 0) {
		return false;
	}
	if (slot_layer_is_mask(l)) {
		return true;
	}
	return slot_layer_is_layer(l) && l->texpaint_nor != NULL && l->texpaint_pack != NULL && l->texpaint->format == l->texpaint_nor->format &&
	       l->texpaint->format == l->texpaint_pack->format;
}

// Draw passes in progress end for the copies and continue after
static draw_saved_t history_tiles_saved_draw = {0};

static void history_tiles_pause_draw() {
	history_tiles_saved_draw = draw_suspend();
}

static void history_tiles_resume_draw() {
	draw_resume(history_tiles_saved_draw);
}

static i64 history_tiles_pool_page_bytes(history_tiles_pool_t *p) {
	i64 side = (i64)HISTORY_TILES_PAGE_GRID * p->size;
	return side * side * (p->format >= 0 ? gpu_texture_format_size(p->format) : 4) * p->channels;
}

i64 history_tiles_page_bytes() {
	return history_tiles_pool_page_bytes(&history_tiles_pools[0]);
}

// The pools in KB and the steps holding tiles, for tests
i32 history_tiles_test_memory_kb() {
	return (i32)(history_tiles_memory() / 1024);
}

i32 history_tiles_test_steps() {
	i32 n = 0;
	for (i32 i = 0; i < history_steps->length; ++i) {
		n += history_steps->buffer[i]->undo_tiles != NULL ? 1 : 0;
	}
	return n;
}

i64 history_tiles_memory() {
	i64 bytes = 0;
	for (i32 i = 0; i < 2; ++i) {
		bytes += (i64)history_tiles_pools[i].page_count * history_tiles_pool_page_bytes(&history_tiles_pools[i]);
	}
	return bytes;
}

static gpu_texture_t *history_tiles_snapshot(history_tiles_pool_t *p, i32 i) {
	return render_path_find_image(p->snap[i]);
}

static void history_tiles_free_snapshot() {
	for (i32 k = 0; k < 2; ++k) {
		for (i32 i = 0; i < history_tiles_pools[k].channels; ++i) {
			render_path_remove_target(history_tiles_pools[k].snap[i]);
		}
	}
}

static void history_tiles_ensure_snapshot(history_tiles_pool_t *p) {
	for (i32 i = 0; i < p->channels; ++i) {
		render_path_ensure_target(p->snap[i], p->layer_w, p->layer_h, gpu_texture_format_name(p->format));
	}
}

static void history_tiles_pool_reset(history_tiles_pool_t *p) {
	for (i32 i = 0; i < p->page_count; ++i) {
		for (i32 j = 0; j < p->channels; ++j) {
			gpu_delete_texture(p->pages[i].tex[j]);
		}
	}
	free(p->pages);
	p->pages      = NULL;
	p->page_count = 0;
	if (p->free != NULL) {
		p->free->length = 0;
	}
	p->generation++;
}

// Drops every tile, steps kept so far can no longer be undone from tiles
void history_tiles_reset() {
	for (i32 i = 0; i < 2; ++i) {
		history_tiles_pool_reset(&history_tiles_pools[i]);
	}
	history_tiles_pending   = NULL;
	history_tiles_pending_l = NULL;
}

// Pool slot to texel position in its page
static void history_tiles_slot_pos(history_tiles_pool_t *p, i32 slot, gpu_texture_t **tex, i32 *x, i32 *y) {
	history_tiles_page_t *page = &p->pages[slot / HISTORY_TILES_PAGE];
	i32                   s    = slot % HISTORY_TILES_PAGE;
	for (i32 i = 0; i < p->channels; ++i) {
		tex[i] = page->tex[i];
	}
	*x = (s % HISTORY_TILES_PAGE_GRID) * p->size;
	*y = (s / HISTORY_TILES_PAGE_GRID) * p->size;
}

static void history_tiles_add_page(history_tiles_pool_t *p) {
	i32 side = HISTORY_TILES_PAGE_GRID * p->size;
	p->pages = realloc(p->pages, sizeof(history_tiles_page_t) * (p->page_count + 1));
	memset(&p->pages[p->page_count], 0, sizeof(history_tiles_page_t));
	for (i32 i = 0; i < p->channels; ++i) {
		p->pages[p->page_count].tex[i] = gpu_create_render_target(side, side, p->format);
	}
	if (p->free == NULL) {
		p->free = i32_array_create(0);
	}
	for (i32 s = HISTORY_TILES_PAGE - 1; s >= 0; --s) {
		i32_array_push(p->free, p->page_count * HISTORY_TILES_PAGE + s);
	}
	p->page_count++;
}

// Whether undo or redo of step can swap its tiles into l
bool history_tiles_valid(history_step_t *step, slot_layer_t *l) {
	history_tiles_pool_t *p = history_tiles_pool_of(step);
	return step->undo_tiles != NULL && step->undo_tiles_gen == p->generation && l != NULL && l->texpaint != NULL && l->texpaint->width == p->layer_w &&
	       l->texpaint->height == p->layer_h && (i32)l->texpaint->format == p->format;
}

void history_tiles_release(history_step_t *step) {
	history_tiles_pool_t *p = history_tiles_pool_of(step);
	if (step->undo_tiles != NULL && step->undo_tiles_gen == p->generation && p->free != NULL) {
		for (i32 i = 1; i < step->undo_tiles->length; i += 2) {
			i32_array_push(p->free, step->undo_tiles->buffer[i]);
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

// The tiles of the oldest step holding some in pool p go, the step undoes from its timeline. False if none.
static bool history_tiles_release_oldest(history_tiles_pool_t *p, history_step_t *keep) {
	for (i32 i = 1; i < history_steps->length; ++i) {
		history_step_t *s = history_steps->buffer[i];
		if (s != keep && s->undo_tiles != NULL && history_tiles_pool_of(s) == p && s->undo_tiles_gen == p->generation) {
			history_tiles_release(s);
			return true;
		}
	}
	return false;
}

// Keeps the tiles of the newest steps only (memory warning)
void history_tiles_keep_newest(i32 steps) {
	for (i32 i = 0; i < history_steps->length - steps; ++i) {
		if (history_steps->buffer[i]->undo_tiles != NULL) {
			history_tiles_release(history_steps->buffer[i]);
		}
	}
}

// A free pool slot, past the budget the tiles of older steps go
static i32 history_tiles_alloc(history_tiles_pool_t *p, history_step_t *step) {
	i64 budget = (i64)g_config->undo_memory_mb * 1024 * 1024;
	while (p->free == NULL || p->free->length == 0) {
		bool over = history_tiles_memory() + history_tiles_pool_page_bytes(p) > budget;
		if (over && history_tiles_release_oldest(p, step)) {
			continue;
		}
		history_tiles_add_page(p); // Under budget, or no older tiles left
	}
	return i32_array_pop(p->free);
}

// Takes the changed tiles of the pending step from the snapshot
static void history_tiles_finalize_step();

// The last stroke is over: its tiles are taken, timelines capture the states after their last entries
void history_tiles_finalize() {
	history_tiles_finalize_step();
	timeline_finalize();
}

static void history_tiles_finalize_step() {
	history_step_t *step = history_tiles_pending;
	slot_layer_t   *l    = history_tiles_pending_l;
	if (step == NULL) {
		return;
	}
	history_tiles_pool_t *p = history_tiles_pool_of(step);
	history_tiles_pending   = NULL;
	history_tiles_pending_l = NULL;
	if (l->texpaint == NULL || l->texpaint->width != p->layer_w || l->texpaint->height != p->layer_h) {
		return; // Layer resized or deleted since, the step stays without tiles
	}
	history_tiles_pause_draw();

	history_tiles_size   = p->size;
	history_tiles_block  = p->block;
	history_tiles_blocks = p->blocks;
	render_target_t *diff = render_path_ensure_target(p->diff, p->blocks, p->blocks, "RGBA32");
	render_path_set_target(p->diff, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	if (p->channels == 3) {
		render_path_bind_target(p->snap[0], "tex0");
		render_path_bind_target(p->snap[1], "tex1");
		render_path_bind_target(p->snap[2], "tex2");
		render_path_bind_target(string_tmp("texpaint%d", l->id), "tex3");
		render_path_bind_target(string_tmp("texpaint_nor%d", l->id), "tex4");
		render_path_bind_target(string_tmp("texpaint_pack%d", l->id), "tex5");
	}
	else { // One texture compared three times
		char *names[3] = {"tex0", "tex1", "tex2"};
		char *layer[3] = {"tex3", "tex4", "tex5"};
		for (i32 i = 0; i < 3; ++i) {
			render_path_bind_target(p->snap[0], names[i]);
			render_path_bind_target(string_tmp("texpaint%d", l->id), layer[i]);
		}
	}
	render_path_draw_shader("Scene/undo_tiles_pass/undo_tiles_pass");
	buffer_t *changed = gpu_get_texture_pixels(diff->_image);

	step->undo_tiles     = i32_array_create(0);
	step->undo_tiles_gen = p->generation;
	i32 t                = p->size;
	i32 per_tile         = p->size / p->block; // Blocks per tile side
	for (i32 tile = 0; tile < HISTORY_TILES_GRID * HISTORY_TILES_GRID; ++tile) {
		i32  bx0 = (tile % HISTORY_TILES_GRID) * per_tile;
		i32  by0 = (tile / HISTORY_TILES_GRID) * per_tile;
		bool hit = false;
		for (i32 by = by0; by < by0 + per_tile && !hit; ++by) {
			for (i32 bx = bx0; bx < bx0 + per_tile; ++bx) {
				if (buffer_get_u8(changed, (by * p->blocks + bx) * 4 + 1) > 127) {
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
	timeline_stroke_dirty(step, step->undo_tiles); // The stroke's capture reads only these

	for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
		step->undo_tiles->buffer[i + 1] = history_tiles_alloc(p, step);
	}
	if (step->undo_tiles != NULL && step->undo_tiles_gen == p->generation) { // Not reset or released while allocating
		gpu_copy_regions_begin();
		for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
			i32            tile = step->undo_tiles->buffer[i];
			gpu_texture_t *page[3];
			i32            px, py;
			history_tiles_slot_pos(p, step->undo_tiles->buffer[i + 1], page, &px, &py);
			for (i32 j = 0; j < p->channels; ++j) {
				gpu_copy_region(page[j], px, py, history_tiles_snapshot(p, j), (tile % HISTORY_TILES_GRID) * t, (tile / HISTORY_TILES_GRID) * t, t, t);
			}
		}
		gpu_copy_regions_end();
	}
	history_tiles_resume_draw();
}

static void history_tiles_layer_textures(slot_layer_t *l, gpu_texture_t **tex) {
	tex[0] = l->texpaint;
	tex[1] = l->texpaint_nor;
	tex[2] = l->texpaint_pack;
}

// Paint step start: the layer is copied into the snapshot. Layers without tiles (a size off the tile grid) undo from
// their timeline only.
void history_tiles_begin(history_step_t *step, slot_layer_t *l) {
	history_tiles_finalize();
	if (!history_tiles_supported(l)) {
		return;
	}
	history_tiles_pool_t *p   = history_tiles_pool_of(step);
	gpu_texture_t        *tex = l->texpaint;
	if (tex->width != p->layer_w || tex->height != p->layer_h || (i32)tex->format != p->format) {
		history_tiles_pool_reset(p); // Tiles of another size or format can not come back
		p->layer_w = tex->width;
		p->layer_h = tex->height;
		p->format  = tex->format;
		p->size    = tex->width / HISTORY_TILES_GRID;
		p->block   = p->size < 8 ? p->size : (tex->width / 1024 > 8 ? tex->width / 1024 : 8);
		p->blocks  = tex->width / p->block;
	}
	step->undo_tiled     = true;
	step->undo_tiles     = NULL;
	step->undo_tiles_gen = p->generation;
	history_tiles_pause_draw();
	history_tiles_ensure_snapshot(p);
	gpu_texture_t *src[3];
	history_tiles_layer_textures(l, src);
	gpu_copy_regions_begin();
	for (i32 j = 0; j < p->channels; ++j) {
		gpu_copy_region(history_tiles_snapshot(p, j), 0, 0, src[j], 0, 0, tex->width, tex->height);
	}
	gpu_copy_regions_end();
	history_tiles_resume_draw();
	history_tiles_pending   = step;
	history_tiles_pending_l = l;
}

// The step whose tiles are still to be taken (the last stroke's, until the next push, undo or redo) and its layer
history_step_t *history_tiles_pending_step(slot_layer_t **layer) {
	if (layer != NULL) {
		*layer = history_tiles_pending_l;
	}
	return history_tiles_pending;
}

// What the paint shader blends over instead of the stroke's snapshot (live brush preview, fill renders), base NULL: the
// snapshot again
void history_tiles_source_set(gpu_texture_t *base, gpu_texture_t *nor, gpu_texture_t *pack) {
	history_tiles_source[0] = base;
	history_tiles_source[1] = nor;
	history_tiles_source[2] = pack;
}

// The layer as it was before the stroke, what the paint shader blends over (_texpaint_undo): the source set, the
// snapshot while a stroke paints the layer, empty otherwise
gpu_texture_t *history_tiles_stroke_source(i32 i) {
	if (history_tiles_source[0] != NULL) {
		return history_tiles_source[i] != NULL ? history_tiles_source[i] : history_tiles_source[0];
	}
	if (history_tiles_pending != NULL && history_tiles_pending_l == g_context->layer) {
		history_tiles_pool_t *p = history_tiles_pool_of(history_tiles_pending);
		if (i < p->channels) {
			return history_tiles_snapshot(p, i);
		}
	}
	return render_path_find_image("empty_black");
}

// Undo and redo: the tiles of the layer and the pool trade places
void history_tiles_swap(history_step_t *step, slot_layer_t *l) {
	history_tiles_finalize();
	if (!history_tiles_valid(step, l)) {
		return;
	}
	history_tiles_pool_t *p = history_tiles_pool_of(step);
	history_tiles_pause_draw();
	history_tiles_ensure_snapshot(p); // Holds the layer tiles meanwhile
	gpu_texture_t *layer[3];
	history_tiles_layer_textures(l, layer);
	i32 t = p->size;
	for (i32 phase = 0; phase < 3; ++phase) {
		gpu_copy_regions_begin();
		for (i32 i = 0; i < step->undo_tiles->length; i += 2) {
			i32            tile = step->undo_tiles->buffer[i];
			i32            lx   = (tile % HISTORY_TILES_GRID) * t;
			i32            ly   = (tile / HISTORY_TILES_GRID) * t;
			gpu_texture_t *page[3];
			i32            px, py;
			history_tiles_slot_pos(p, step->undo_tiles->buffer[i + 1], page, &px, &py);
			for (i32 j = 0; j < p->channels; ++j) {
				if (phase == 0) {
					gpu_copy_region(history_tiles_snapshot(p, j), lx, ly, layer[j], lx, ly, t, t);
				}
				else if (phase == 1) {
					gpu_copy_region(layer[j], lx, ly, page[j], px, py, t, t);
				}
				else {
					gpu_copy_region(page[j], px, py, history_tiles_snapshot(p, j), lx, ly, t, t);
				}
			}
		}
		gpu_copy_regions_end();
	}
	history_tiles_resume_draw();
}

// Memory warning: only the pools stay
void history_tiles_trim() {
	history_tiles_finalize();
	history_tiles_free_snapshot();
}
