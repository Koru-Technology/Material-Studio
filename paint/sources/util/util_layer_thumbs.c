#include "../global.h"

// Layer thumbnails, what the layer list shows (app_layer_thumbnail): the layer previews (texpaint_preview) read back
// without waiting for the GPU, kept as RGBA texels on the layer (slot_layer_t.thumb), and saved with the project
// (layer_data_t.thumb, io/project_container.c) so that an open shows them before anything is drawn. The list read each
// preview back at once before: every read waited behind the GPU's queue, 4 s of Book 1's open on the iPad.
//
// A preview drawn (util_layer_draw_preview) queues its layer; once a frame the queued previews are copied into a transfer
// together, its done copies the texels to the layers still there (by uid) and the list reads them again.

typedef struct {
	u32       *uids;
	i32        count;
	i32        size;    // Preview width and height
	project_t *project; // Another one open by the time it lands: dropped
} layers_thumbs_batch_t;

static u32           *layers_thumbs_queue     = NULL;
static i32            layers_thumbs_queued    = 0;
static i32            layers_thumbs_cap       = 0;
static gpu_transfer_t *layers_thumbs_transfer = NULL;
static bool           layers_thumbs_inflight  = false;

static slot_layer_t *layers_thumbs_find(u32 uid) {
	for (i32 i = 0; g_project != NULL && g_project->_ != NULL && i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->uid == uid) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

void layers_thumb_queue(slot_layer_t *l) {
	for (i32 i = 0; i < layers_thumbs_queued; ++i) {
		if (layers_thumbs_queue[i] == l->uid) {
			return;
		}
	}
	if (layers_thumbs_queued == layers_thumbs_cap) {
		layers_thumbs_cap   = layers_thumbs_cap == 0 ? 32 : layers_thumbs_cap * 2;
		layers_thumbs_queue = realloc(layers_thumbs_queue, sizeof(u32) * layers_thumbs_cap);
	}
	layers_thumbs_queue[layers_thumbs_queued++] = l->uid;
}

// The layer's thumbnail texels (size x size RGBA, copied)
void layers_thumb_set(slot_layer_t *l, const u8 *rgba, i32 size) {
	if (l->thumb == NULL || l->thumb_size != size) {
		free(l->thumb);
		l->thumb      = malloc((size_t)size * size * 4);
		l->thumb_size = size;
	}
	memcpy(l->thumb, rgba, (size_t)size * size * 4);
}

static void layers_thumbs_done(gpu_transfer_t *t, void *ud) {
	layers_thumbs_batch_t *b     = ud;
	size_t                 bytes = (size_t)b->size * b->size * 4;
	u8                    *mem   = gpu_transfer_map(t, 0);
	u8                    *rgba  = malloc(bytes);
	for (i32 i = 0; i < b->count && b->project == g_project; ++i) {
		slot_layer_t *l = layers_thumbs_find(b->uids[i]);
		if (l == NULL) {
			continue;
		}
		memcpy(rgba, mem + bytes * i, bytes);
#ifdef IRON_BGRA
		for (size_t k = 0; k < bytes; k += 4) { // The preview is a render target: BGRA texels
			u8 r        = rgba[k];
			rgba[k]     = rgba[k + 2];
			rgba[k + 2] = r;
		}
#endif
		layers_thumb_set(l, rgba, b->size);
	}
	free(rgba);
	free(b->uids);
	free(b);
	layers_thumbs_inflight = false;
	app_notify_previews();
}

// Once a frame (util_layer_update_preview): the previews drawn since the last batch read back. One batch at a time: what
// is drawn meanwhile goes with the next.
void layers_thumbs_flush(void) {
	if (layers_thumbs_queued == 0 || layers_thumbs_inflight) {
		return;
	}
	i32                    size = util_render_layer_preview_size;
	layers_thumbs_batch_t *b    = calloc(1, sizeof(layers_thumbs_batch_t));
	b->uids                     = malloc(sizeof(u32) * layers_thumbs_queued);
	b->size                     = size;
	b->project                  = g_project;
	for (i32 i = 0; i < layers_thumbs_queued; ++i) {
		slot_layer_t *l = layers_thumbs_find(layers_thumbs_queue[i]);
		gpu_texture_t *p = l != NULL ? l->texpaint_preview : NULL;
		if (p != NULL && p->width == size && p->height == size && p->format == GPU_TEXTURE_FORMAT_RGBA32) {
			b->uids[b->count++] = l->uid;
		}
	}
	layers_thumbs_queued = 0;
	if (b->count == 0) {
		free(b->uids);
		free(b);
		return;
	}
	if (layers_thumbs_transfer == NULL) {
		layers_thumbs_transfer = gpu_transfer_create(0);
	}
	size_t       bytes = (size_t)size * size * 4;
	draw_saved_t saved = draw_suspend();
	gpu_transfer_map(layers_thumbs_transfer, bytes * b->count);
	gpu_copy_regions_begin();
	for (i32 i = 0; i < b->count; ++i) {
		gpu_copy_region_to_transfer(layers_thumbs_transfer, layers_thumbs_find(b->uids[i])->texpaint_preview, 0, 0, size, size, bytes * i);
	}
	gpu_copy_regions_end();
	draw_resume(saved);
	layers_thumbs_inflight = true;
	gpu_transfer_commit(layers_thumbs_transfer, layers_thumbs_done, b);
}

// Thumbnails waiting for their readback (tests, the idle sleep)
bool layers_thumbs_busy(void) {
	return layers_thumbs_queued > 0 || layers_thumbs_inflight;
}
