#include "../global.h"
#include <unistd.h>

// Texture export in tiles (g_config->export_tiled), the live layers left as they are.
//
// The jobs (texture sets) and file names are the legacy export's (export_texture.c). A job's output (w x h) is cut into
// tiles of EXPORT_TILED_MAX_TILE or less (smaller while their targets exceed the budget), each rendered with a halo
// around its core: square targets of size + 2 halo, also past the edges of the output.
//
// Phase A, a tile at a time: each layer's source for the tile's rectangle (export_tiled_source: today the live layer's
// pixels; the plug-in point of timeline_materialize and fill layers rendered for a rectangle) and its masks (the
// evaluated stack, blur included, read for the rectangle) composite through export_texture_merge_layer. With padding,
// the near padding dilates the tile (dilate_pass, the uv coverage of the rectangle): the halo is wider than the pass
// reaches, the core comes out as from dilating the whole texture. The core is read back, lz4 compressed into the spool
// (a temporary file), and summed into a low resolution picture of the covered texels for the far padding. Without uv
// coverage within the halo a tile is skipped when padding.
//
// Phase B, an output file at a time: the cores come back from the spool, texels the near padding did not reach are
// filled from the push-pull pyramid of the low resolution picture, the preset's channels are packed (export_texture_pack)
// and the file encoded whole.

#ifdef IRON_IOS
#define EXPORT_TILED_MAX_TILE  2048
#define EXPORT_TILED_BUDGET_MB 256 // Tile targets
#else
#define EXPORT_TILED_MAX_TILE  4096
#define EXPORT_TILED_BUDGET_MB 1024
#endif
#define EXPORT_TILED_NEAR   16  // Near padding in texels (dilate pass), push-pull past it
#define EXPORT_TILED_PYR    512 // Size of the far padding's picture, at most
#define EXPORT_TILED_STEP_MS 40.0

typedef struct export_tiled_tile {
	i32  x; // Core in output texels
	i32  y;
	i32  w;
	i32  h;
	bool skip;     // No uv coverage near it (padding)
	i64  off[4];   // Spool: composites base, normal, pack, then the padding reach (1 byte a texel)
	i32  clen[4];
	i32  len[4];
} export_tiled_tile_t;

typedef struct export_tiled_job {
	slot_layer_t_array_t *layers;
	char                 *object_name;
} export_tiled_job_t;

// How a source maps into the output: identity, a udim tile's slot cropped to the whole output, the whole layer into a slot
typedef struct export_tiled_map {
	i32 mode; // 0 identity, 1 slot to output, 2 output to slot
	i32 slot;
} export_tiled_map_t;

static struct {
	bool                 active;
	char                *path;
	bool                 bake;
	slot_material_t     *bake_source;
	export_tiled_job_t  *jobs;
	i32                  njobs;
	i32                  njobs_cap;
	i32                  job;
	i32                  phase; // 0 start of a job, 1 tiles, 2 output files, 3 job done
	i32                  tile;
	i32                  tex;
	// The job
	export_tiled_tile_t *tiles;
	i32                  ntiles;
	i32                  w;
	i32                  h;
	i32                  size; // Tile core
	i32                  halo;
	i32                  bits;
	i32                  bpp; // Bytes a texel of a composite
	gpu_texture_format_t format;
	bool                 padding;
	bool                 height_coverage; // Height blended masks read the uv coverage
	char                *file;
	char                *ext;
	i32                  udim_slot;
	FILE                *spool;
	char                *spool_path;
	f32                 *pyr;   // Far padding picture: 12 channels (3 composites x 4) summed
	f32                 *pyr_w; // and the texels summed
	i32                  pw;
	i32                  ph;
	i32                  ps; // Output texels a picture texel
	f32                **levels; // Push-pull: filled colors per level
	i32                  nlevels;
	// Progress
	i32                  units_done;
	i32                  units_total;
	i64                  bytes_base;
	string_array_t      *written;
} et;

static gpu_texture_t  *et_acc[3];
static gpu_texture_t  *et_temp;
static gpu_texture_t  *et_src[3];
static gpu_texture_t  *et_mask;
static gpu_texture_t  *et_mask_temp;
static gpu_texture_t  *et_mask_src;
static gpu_texture_t  *et_cov;
static gpu_texture_t  *et_reach;
static gpu_pipeline_t *et_pipe_uv     = NULL;
static gpu_pipeline_t *et_pipe_dilate[4]; // RGBA32, RGBA64, RGBA128, R8
#ifdef IRON_METAL
static gpu_transfer_t *et_xfer = NULL;
#endif

static i32  et_test_tile   = 0;     // Tests: tile core size, 0 the default
static bool et_test_raster = false; // Tests: uv coverage rasterized per tile also at the working resolution
static bool et_test_mm     = false; // Tests: the Material Maker export cap also at the working resolution
static i64  et_stats[11]; // export_tiled_stat
static bool et_mm_capped = false;
static i32  et_res_w     = 0; // The export resolution, 0 the working one
static i32  et_res_h     = 0;

// Targets and passes

static i32 export_tiled_bytes_per_texel(gpu_texture_format_t f) {
	return gpu_texture_format_size(f);
}

static i64 export_tiled_budget() {
	return (i64)EXPORT_TILED_BUDGET_MB * 1024 * 1024;
}

// Bytes of the tile targets at core size s
static i64 export_tiled_target_bytes(i32 s, i32 halo, gpu_texture_format_t format) {
	i64 n = (i64)(s + 2 * halo) * (s + 2 * halo);
	i32 f = export_tiled_bytes_per_texel(format);
	return n * (4 * f + 3 * 16 + 3 * 4 + 2); // Composites and temp, layer sources (16 at most), masks, coverage and reach
}

static void export_tiled_targets(i32 s, i32 halo) {
	i32 n = s + 2 * halo;
	for (i32 k = 0; k < 3; ++k) {
		util_texture_ensure_target(&et_acc[k], n, n, et.format);
	}
	util_texture_ensure_target(&et_temp, n, n, et.format);
	util_texture_ensure_target(&et_mask, n, n, GPU_TEXTURE_FORMAT_RGBA32);
	util_texture_ensure_target(&et_mask_temp, n, n, GPU_TEXTURE_FORMAT_RGBA32);
	util_texture_ensure_target(&et_cov, n, n, GPU_TEXTURE_FORMAT_R8);
	util_texture_ensure_target(&et_reach, n, n, GPU_TEXTURE_FORMAT_R8);
}

static void export_tiled_release_targets() {
	for (i32 k = 0; k < 3; ++k) {
		util_texture_release_target(&et_acc[k]);
		util_texture_release_target(&et_src[k]);
	}
	util_texture_release_target(&et_temp);
	util_texture_release_target(&et_mask);
	util_texture_release_target(&et_mask_temp);
	util_texture_release_target(&et_mask_src);
	util_texture_release_target(&et_cov);
	util_texture_release_target(&et_reach);
#ifdef IRON_METAL
	gpu_transfer_destroy(et_xfer);
	et_xfer = NULL;
#endif
}

static gpu_pipeline_t *export_tiled_pipe_dilate(gpu_texture_format_t f) {
	i32 k = f == GPU_TEXTURE_FORMAT_RGBA64 ? 1 : f == GPU_TEXTURE_FORMAT_RGBA128 ? 2 : f == GPU_TEXTURE_FORMAT_R8 ? 3 : 0;
	if (et_pipe_dilate[k] == NULL) {
		gpu_pipeline_t *pipe       = gpu_create_pipeline();
		pipe->vertex_shader        = sys_get_shader("dilate_pass.vert");
		pipe->fragment_shader      = sys_get_shader("dilate_pass.frag");
		gpu_vertex_structure_t *vs = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
		pipe->input_layout           = vs;
		pipe->color_attachment_count = 1;
		pipe->color_attachment[0]    = f;
		gpu_pipeline_compile(pipe);
		et_pipe_dilate[k] = pipe;
	}
	return et_pipe_dilate[k];
}

// The near padding: texels of dst outside the uv islands (coverage) take the nearest island texel within radius, as
// render_path_paint_dilate does. src: a copy of dst.
static void export_tiled_dilate(gpu_texture_t *dst, gpu_texture_t *src, gpu_texture_t *coverage, i32 radius) {
	_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(export_tiled_pipe_dilate(dst->format));
	gpu_set_texture(0, src);
	gpu_set_texture(1, coverage);
	gpu_set_float(0, (f32)radius);
	gpu_set_float2(8, (f32)dst->width, (f32)dst->height);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

static void export_tiled_copy(gpu_texture_t *dst, gpu_texture_t *src) {
	draw_begin(dst, false, 0);
	draw_set_pipeline(dst->format == GPU_TEXTURE_FORMAT_R8 ? NULL : pipes_copy);
	draw_image(src, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
}

// Reads rectangle (x, y, w, h) of textures into out[i] (malloc'd, raw texels)
static void export_tiled_read(gpu_texture_t **tex, i32 n, i32 x, i32 y, i32 w, i32 h, u8 **out) {
#ifdef IRON_METAL
	size_t off[8];
	size_t total = 0;
	for (i32 i = 0; i < n; ++i) {
		off[i] = total;
		total += (size_t)w * h * gpu_texture_format_size(tex[i]->format);
		total = (total + 255) & ~(size_t)255;
	}
	if (et_xfer == NULL) {
		et_xfer = gpu_transfer_create(total);
	}
	gpu_transfer_map(et_xfer, total);
	gpu_copy_regions_begin();
	for (i32 i = 0; i < n; ++i) {
		gpu_copy_region_to_transfer(et_xfer, tex[i], x, y, w, h, off[i]);
	}
	gpu_copy_regions_end();
	gpu_execute_and_wait();
	u8 *mem = gpu_transfer_map(et_xfer, total);
	for (i32 i = 0; i < n; ++i) {
		size_t len = (size_t)w * h * gpu_texture_format_size(tex[i]->format);
		out[i]     = malloc(len);
		memcpy(out[i], mem + off[i], len);
	}
#else
	for (i32 i = 0; i < n; ++i) {
		i32       bpp = gpu_texture_format_size(tex[i]->format);
		buffer_t *b   = gpu_get_texture_pixels(tex[i]);
		out[i]        = malloc((size_t)w * h * bpp);
		for (i32 row = 0; row < h; ++row) {
			memcpy(out[i] + (size_t)row * w * bpp, b->buffer + ((size_t)(y + row) * tex[i]->width + x) * bpp, (size_t)w * bpp);
		}
		gpu_release_texture_pixels(tex[i]);
	}
#endif
}

// Sources

// The source rectangle of output rectangle (x, y, w, h) in src (sw x sh texels), through map m. False when the source
// covers none of it, else *dst the part it covers (output texels).
static bool export_tiled_map_rect(export_tiled_map_t m, i32 sw, i32 sh, i32 x, i32 y, i32 w, i32 h, i32 *dst, f32 *src) {
	i32 dx0 = 0, dy0 = 0, dx1 = et.w, dy1 = et.h; // Where the source lands in the output
	f32 ax = sw / (f32)et.w, ay = sh / (f32)et.h, bx = 0.0, by = 0.0;
	if (m.mode != 0) {
		i32 stride = util_mesh_atlas_stride();
		f32 cx     = (m.slot % stride) / (f32)stride;
		f32 cy     = (m.slot / stride) / (f32)stride;
		if (m.mode == 1) { // The slot of the atlas, scaled up to the output
			ax = ax / stride;
			ay = ay / stride;
			bx = cx * sw;
			by = cy * sh;
		}
		else { // The whole source into the slot of the output, as draw_scaled_image (whole texels)
			dx0 = (i32)(cx * et.w);
			dy0 = (i32)(cy * et.h);
			i32 slot_w = (i32)(et.w / (f32)stride);
			i32 slot_h = (i32)(et.h / (f32)stride);
			dx1        = dx0 + slot_w;
			dy1        = dy0 + slot_h;
			ax         = sw / (f32)slot_w;
			ay         = sh / (f32)slot_h;
			bx         = -dx0 * ax;
			by         = -dy0 * ay;
		}
	}
	i32 ix0 = x > dx0 ? x : dx0;
	i32 iy0 = y > dy0 ? y : dy0;
	i32 ix1 = x + w < dx1 ? x + w : dx1;
	i32 iy1 = y + h < dy1 ? y + h : dy1;
	if (ix1 <= ix0 || iy1 <= iy0) {
		return false;
	}
	dst[0] = ix0;
	dst[1] = iy0;
	dst[2] = ix1 - ix0;
	dst[3] = iy1 - iy0;
	src[0] = ax * ix0 + bx;
	src[1] = ay * iy0 + by;
	src[2] = ax * (ix1 - ix0);
	src[3] = ay * (iy1 - iy0);
	return true;
}

// dst (the tile's halo rectangle hx, hy) becomes src's pixels for it through map m, transparent where src does not reach
static void export_tiled_blit(gpu_texture_t *dst, gpu_texture_t *src, export_tiled_map_t m, i32 hx, i32 hy) {
	i32  d[4];
	f32  s[4];
	bool any  = export_tiled_map_rect(m, src->width, src->height, hx, hy, dst->width, dst->height, d, s);
	bool full = any && d[0] == hx && d[1] == hy && d[2] == dst->width && d[3] == dst->height;
	draw_begin(dst, !full, 0x00000000);
	if (any) {
		draw_set_pipeline(dst->format == GPU_TEXTURE_FORMAT_R8 ? NULL : pipes_copy);
		draw_scaled_sub_image(src, s[0], s[1], s[2], s[3], d[0] - hx, d[1] - hy, d[2], d[3]);
		draw_set_pipeline(NULL);
	}
	draw_end();
}

static gpu_texture_t *export_tiled_source_target(gpu_texture_t **t, gpu_texture_t *like) {
	i32 n = et.size + 2 * et.halo;
	util_texture_ensure_target(t, n, n, like->format);
	return *t;
}

// The plug-in point: layer l's pixels for the tile, into t->out. Paint layers from their timeline (stroke_rec.h) at the
// state they show; otherwise, or when it can not (today: restoring kept states only, at their own size), the live
// layer's pixels (scaled when the output's resolution differs). Next: fill layers rendered for t->uv_rect
// (layers_update_fill_layer through the paint track's uv tile transform).
static bool export_tiled_source(slot_layer_t *l, export_tiled_map_t m, i32 hx, i32 hy, paint_target_t *t) {
	timeline_t *tl = m.mode == 0 ? timeline_of(l) : NULL; // Udim maps: the live layer for now
	if (tl != NULL) {
		if (timeline_materialize(tl, tl->cursor, t)) {
			et_stats[9]++;
			return true;
		}
		et_stats[10]++;
	}
	gpu_texture_t *live[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	for (i32 k = 0; k < t->channels; ++k) {
		export_tiled_blit(t->out[k], live[k], m, hx, hy);
	}
	return true;
}

// Phase A

static void export_tiled_coverage(export_tiled_map_t m, i32 hx, i32 hy) {
	bool raster = et_test_raster || util_uv_dilatemap == NULL || util_uv_dilatemap->width != et.w || util_uv_dilatemap->height != et.h || m.mode != 0;
	if (!raster) {
		export_tiled_blit(et_cov, util_uv_dilatemap, m, hx, hy);
		return;
	}
	if (et_pipe_uv == NULL) {
		et_pipe_uv                 = gpu_create_pipeline();
		et_pipe_uv->vertex_shader   = sys_get_shader("export_uv_map.vert");
		et_pipe_uv->fragment_shader = sys_get_shader("export_uv_map.frag");
		gpu_vertex_structure_t *vs = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
		gpu_vertex_structure_add(vs, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
		gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
		et_pipe_uv->input_layout        = vs;
		et_pipe_uv->depth_write         = false;
		et_pipe_uv->depth_mode          = GPU_COMPARE_MODE_ALWAYS;
		et_pipe_uv->color_attachment[0] = GPU_TEXTURE_FORMAT_R8;
		gpu_pipeline_compile(et_pipe_uv);
	}
	// The uv rectangle the tile shows: through a udim map, the atlas slot's part
	f32 u0 = hx / (f32)et.w, v0 = hy / (f32)et.h, du = et_cov->width / (f32)et.w, dv = et_cov->height / (f32)et.h;
	if (m.mode == 1) {
		i32 stride = util_mesh_atlas_stride();
		u0         = ((m.slot % stride) + u0) / stride;
		v0         = ((m.slot / stride) + v0) / stride;
		du         = du / stride;
		dv         = dv / stride;
	}
	mesh_data_t *geom = g_context->merged_object != NULL ? g_context->merged_object->data : g_context->paint_object->data;
	_gpu_begin(et_cov, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_set_pipeline(et_pipe_uv);
	gpu_set_float4(0, u0, v0, du, dv);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	gpu_end();
}

static void export_tiled_spool_write(export_tiled_tile_t *t, i32 k, u8 *data, i32 len) {
	buffer_t  raw = {.buffer = data, .length = len, .capacity = len};
	buffer_t *c   = lz4_encode(&raw);
	fseek(et.spool, 0, SEEK_END);
	t->off[k]  = ftell(et.spool);
	t->len[k]  = len;
	t->clen[k] = c->length;
	fwrite(c->buffer, 1, c->length, et.spool);
	iron_delete_blob(c);
}

static u8 *export_tiled_spool_read(export_tiled_tile_t *t, i32 k) {
	buffer_t *c = buffer_create(t->clen[k]);
	fseek(et.spool, t->off[k], SEEK_SET);
	if (fread(c->buffer, 1, t->clen[k], et.spool) != (size_t)t->clen[k]) {
		console_error("Export: spool read failed");
	}
	buffer_t *raw = lz4_decode(c, t->len[k]);
	array_free(c);
	free(c);
	u8 *out = malloc(t->len[k]);
	memcpy(out, raw->buffer, t->len[k]);
	iron_delete_blob(raw);
	return out;
}

static f32 export_tiled_get(u8 *p, i32 i) {
	if (et.bits == 8) {
		return p[i] / 255.0;
	}
	if (et.bits == 16) {
		return half_to_float(((u16 *)p)[i]);
	}
	return ((f32 *)p)[i];
}

static void export_tiled_set(u8 *p, i32 i, f32 v) {
	if (et.bits == 8) {
		v    = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
		p[i] = (u8)math_floor(v * 255.0 + 0.5);
	}
	else if (et.bits == 16) {
		((u16 *)p)[i] = float_to_half_fast(v);
	}
	else {
		((f32 *)p)[i] = v;
	}
}

// Sums the core's covered and reached texels into the far padding picture
static void export_tiled_pyramid_add(export_tiled_tile_t *t, u8 **comp, u8 *reach) {
	for (i32 y = 0; y < t->h; ++y) {
		for (i32 x = 0; x < t->w; ++x) {
			i32 i = y * t->w + x;
			if (reach[i] == 0) {
				continue;
			}
			i32 p = ((t->y + y) / et.ps) * et.pw + (t->x + x) / et.ps;
			for (i32 k = 0; k < 3; ++k) {
				for (i32 c = 0; c < 4; ++c) {
					et.pyr[p * 12 + k * 4 + c] += export_tiled_get(comp[k], i * 4 + c);
				}
			}
			et.pyr_w[p] += 1.0;
		}
	}
}

static void export_tiled_render_tile(export_tiled_job_t *job, export_tiled_tile_t *t) {
	i32  n  = et.size + 2 * et.halo;
	i32  hx = t->x - et.halo;
	i32  hy = t->y - et.halo;
	bool bake = et.bake;

	export_tiled_map_t identity = {0, 0};
	export_tiled_map_t cov_map  = et.udim_slot >= 0 ? (export_tiled_map_t){1, et.udim_slot} : identity;
	if (et.padding || et.height_coverage) {
		export_tiled_coverage(cov_map, hx, hy);
	}
	if (et.padding) { // Skipped without uv coverage within the halo
		u8  *cov;
		bool any = false;
		export_tiled_read(&et_cov, 1, 0, 0, n, n, &cov);
		for (i32 i = 0; i < n * n && !any; ++i) {
			any = cov[i] != 0;
		}
		free(cov);
		if (!any) {
			t->skip = true;
			et_stats[3]++;
			return;
		}
	}

	export_texture_clear(et_acc);
	gpu_texture_t *empty = render_path_empty_white();
	for (i32 i = 0; i < job->layers->length; ++i) {
		slot_layer_t *l1 = job->layers->buffer[i];
		if (!export_texture_layer_used(l1, job->object_name)) {
			continue;
		}
		export_tiled_map_t m = identity;
		if (!bake && et.udim_slot >= 0 && util_mesh_udim_layer(l1)) {
			m = (export_tiled_map_t){1, et.udim_slot};
		}
		else if (!bake && et.udim_slot < 0 && layers_udim_tile_slot(l1) >= 0) {
			m = (export_tiled_map_t){2, layers_udim_tile_slot(l1)};
		}

		paint_target_t pt = {.w = n, .h = n, .res_w = et.w, .res_h = et.h, .channels = 3};
		pt.uv_rect[0]     = hx / (f32)et.w;
		pt.uv_rect[1]     = hy / (f32)et.h;
		pt.uv_rect[2]     = (hx + n) / (f32)et.w;
		pt.uv_rect[3]     = (hy + n) / (f32)et.h;
		pt.out[0]         = export_tiled_source_target(&et_src[0], l1->texpaint);
		pt.out[1]         = export_tiled_source_target(&et_src[1], l1->texpaint_nor);
		pt.out[2]         = export_tiled_source_target(&et_src[2], l1->texpaint_pack);
		export_tiled_source(l1, m, hx, hy, &pt);
		slot_layer_t tl  = *l1;
		tl.texpaint      = pt.out[0];
		tl.texpaint_nor  = pt.out[1];
		tl.texpaint_pack = pt.out[2];

		gpu_texture_t        *mask    = empty;
		slot_layer_t_array_t *l1masks = slot_layer_get_masks(l1, true);
		if (l1masks != NULL && !bake) {
			draw_begin(et_mask, true, 0xffffffff);
			draw_end();
			for (i32 j = 0; j < l1masks->length; ++j) {
				slot_layer_t  *mk  = l1masks->buffer[j];
				gpu_texture_t *tex = slot_layer_mask_texture(mk); // Evaluated whole (blur at its own size)
				export_tiled_blit(export_tiled_source_target(&et_mask_src, tex), tex, m, hx, hy);
				layers_merge_mask_ex(et_mask, et_mask_temp, mk, et_mask_src, &tl, et_acc[2], et.height_coverage ? et_cov : NULL);
			}
			mask = et_mask;
		}
		export_texture_merge_layer(et_acc, et_temp, &tl, mask, i, job->layers->length);
	}

	u8 *reach = NULL;
	if (et.padding) {
		for (i32 k = 0; k < 3; ++k) {
			export_tiled_copy(et_temp, et_acc[k]);
			export_tiled_dilate(et_acc[k], et_temp, et_cov, EXPORT_TILED_NEAR);
		}
		export_tiled_copy(et_reach, et_cov);
		export_tiled_dilate(et_reach, et_cov, et_cov, EXPORT_TILED_NEAR); // Covered or reached
	}

	u8            *comp[4];
	gpu_texture_t *read[4] = {et_acc[0], et_acc[1], et_acc[2], et_reach};
	export_tiled_read(read, et.padding ? 4 : 3, et.halo, et.halo, t->w, t->h, comp);
	for (i32 k = 0; k < 3; ++k) {
		export_tiled_spool_write(t, k, comp[k], t->w * t->h * et.bpp);
	}
	if (et.padding) {
		reach = comp[3];
		export_tiled_spool_write(t, 3, reach, t->w * t->h);
		export_tiled_pyramid_add(t, comp, reach);
		free(reach);
	}
	for (i32 k = 0; k < 3; ++k) {
		free(comp[k]);
	}
	et_stats[2]++;
}

// Far padding: push-pull over the picture's levels, every texel gets the average of the nearest covered ones

static void export_tiled_pull() {
	i32  w = et.pw, h = et.ph;
	f32 *sum[24];
	f32 *wt[24];
	i32  ws[24], hs[24];
	i32  n = 0;
	sum[0] = et.pyr;
	wt[0]  = et.pyr_w;
	ws[0]  = w;
	hs[0]  = h;
	n      = 1;
	while ((w > 1 || h > 1) && n < 24) { // Push: weighted 2x2 sums
		i32  nw = (w + 1) / 2, nh = (h + 1) / 2;
		f32 *s  = calloc((size_t)nw * nh * 12, sizeof(f32));
		f32 *q  = calloc((size_t)nw * nh, sizeof(f32));
		for (i32 y = 0; y < h; ++y) {
			for (i32 x = 0; x < w; ++x) {
				i32 p = (y / 2) * nw + x / 2;
				for (i32 c = 0; c < 12; ++c) {
					s[p * 12 + c] += sum[n - 1][(y * w + x) * 12 + c];
				}
				q[p] += wt[n - 1][y * w + x];
			}
		}
		sum[n] = s;
		wt[n]  = q;
		ws[n]  = nw;
		hs[n]  = nh;
		n++;
		w = nw;
		h = nh;
	}
	// Normalized colors, from the top
	f32 **lv = calloc(n, sizeof(f32 *));
	for (i32 l = n - 1; l >= 0; --l) {
		lv[l] = calloc((size_t)ws[l] * hs[l] * 12, sizeof(f32));
		for (i32 y = 0; y < hs[l]; ++y) {
			for (i32 x = 0; x < ws[l]; ++x) {
				i32 p = y * ws[l] + x;
				if (wt[l][p] > 0.0) {
					for (i32 c = 0; c < 12; ++c) {
						lv[l][p * 12 + c] = sum[l][p * 12 + c] / wt[l][p];
					}
				}
				else if (l < n - 1) { // Pull: bilinear from the level above
					f32 fx = (x + 0.5) / 2.0 - 0.5, fy = (y + 0.5) / 2.0 - 0.5;
					i32 uw = ws[l + 1], uh = hs[l + 1];
					fx     = fx < 0.0 ? 0.0 : fx > uw - 1 ? uw - 1 : fx;
					fy     = fy < 0.0 ? 0.0 : fy > uh - 1 ? uh - 1 : fy;
					i32 x0 = (i32)fx, y0 = (i32)fy;
					i32 x1 = x0 + 1 < uw ? x0 + 1 : x0, y1 = y0 + 1 < uh ? y0 + 1 : y0;
					f32 tx = fx - x0, ty = fy - y0;
					for (i32 c = 0; c < 12; ++c) {
						f32 a = lv[l + 1][(y0 * uw + x0) * 12 + c] * (1 - tx) + lv[l + 1][(y0 * uw + x1) * 12 + c] * tx;
						f32 b = lv[l + 1][(y1 * uw + x0) * 12 + c] * (1 - tx) + lv[l + 1][(y1 * uw + x1) * 12 + c] * tx;
						lv[l][p * 12 + c] = a * (1 - ty) + b * ty;
					}
				}
			}
		}
	}
	for (i32 l = 1; l < n; ++l) {
		free(sum[l]);
		free(wt[l]);
		free(lv[l]);
	}
	et.levels    = lv;
	et.nlevels   = 1; // Only the finest is kept
}

// Texels of composite k the near padding did not reach, from the far padding picture
static void export_tiled_far_fill(export_tiled_tile_t *t, i32 k, u8 *comp, u8 *reach) {
	f32 *lv = et.levels[0];
	for (i32 y = 0; y < t->h; ++y) {
		for (i32 x = 0; x < t->w; ++x) {
			i32 i = y * t->w + x;
			if (reach != NULL && reach[i] != 0) {
				continue;
			}
			f32 fx = (t->x + x + 0.5) / et.ps - 0.5, fy = (t->y + y + 0.5) / et.ps - 0.5;
			fx     = fx < 0.0 ? 0.0 : fx > et.pw - 1 ? et.pw - 1 : fx;
			fy     = fy < 0.0 ? 0.0 : fy > et.ph - 1 ? et.ph - 1 : fy;
			i32 x0 = (i32)fx, y0 = (i32)fy;
			i32 x1 = x0 + 1 < et.pw ? x0 + 1 : x0, y1 = y0 + 1 < et.ph ? y0 + 1 : y0;
			f32 tx = fx - x0, ty = fy - y0;
			for (i32 c = 0; c < 4; ++c) {
				i32 ch = k * 4 + c;
				f32 a  = lv[(y0 * et.pw + x0) * 12 + ch] * (1 - tx) + lv[(y0 * et.pw + x1) * 12 + ch] * tx;
				f32 b  = lv[(y1 * et.pw + x0) * 12 + ch] * (1 - tx) + lv[(y1 * et.pw + x1) * 12 + ch] * tx;
				export_tiled_set(comp, i * 4 + c, a * (1 - ty) + b * ty);
			}
		}
	}
}

// Composite k of tile t: from the spool, or all far padding for a skipped tile
static u8 *export_tiled_tile_pixels(export_tiled_tile_t *t, i32 k, u8 *reach) {
	if (t->skip) {
		u8 *p = calloc((size_t)t->w * t->h, et.bpp);
		export_tiled_far_fill(t, k, p, NULL);
		return p;
	}
	u8 *p = export_tiled_spool_read(t, k);
	if (et.padding) {
		export_tiled_far_fill(t, k, p, reach);
	}
	return p;
}

// Phase B: output file tex of the preset
static void export_tiled_write_file(i32 tex) {
	export_preset_t         *preset = box_export_preset;
	export_preset_texture_t *t      = preset->textures->buffer[tex];
	export_texture_layout_t  layout = export_texture_layout(t);
	char                    *path   = export_texture_out_path(et.path, et.file, t, et.ext);
	size_t                   bpp    = et.bpp;
	buffer_t                *out    = buffer_create((u32)((size_t)et.w * et.h * bpp));
	bool                     need[3];
	need[0] = need[1] = need[2] = false;
	if (layout.src >= 0) {
		need[layout.src] = true;
	}
	else {
		export_preset_t one   = {0};
		void           *buf[] = {t};
		export_preset_texture_t_array_t arr = {.buffer = (export_preset_texture_t **)buf, .length = 1, .capacity = 1};
		one.textures                        = &arr;
		export_texture_needs(&one, need);
	}
	for (i32 i = 0; i < et.ntiles; ++i) {
		export_tiled_tile_t *tile  = &et.tiles[i];
		u8                  *reach = et.padding && !tile->skip ? export_tiled_spool_read(tile, 3) : NULL;
		u8                  *comp[3] = {NULL, NULL, NULL};
		buffer_t             cb[3];
		for (i32 k = 0; k < 3; ++k) {
			if (need[k]) {
				comp[k] = export_tiled_tile_pixels(tile, k, reach);
			}
			u32 len = (u32)((size_t)tile->w * tile->h * bpp);
			cb[k]   = (buffer_t){.buffer = comp[k], .length = comp[k] != NULL ? len : 0, .capacity = len};
		}
		u8 *src = NULL;
		if (layout.src >= 0) {
			src = comp[layout.src];
		}
		else {
			buffer_t *pix = buffer_create((u32)((size_t)tile->w * tile->h * bpp));
			export_texture_pack(t, need[0] ? &cb[0] : NULL, need[1] ? &cb[1] : NULL, need[2] ? &cb[2] : NULL, pix, et.bits);
			src = malloc(pix->length);
			memcpy(src, pix->buffer, pix->length);
			array_free(pix);
			free(pix);
		}
		for (i32 row = 0; row < tile->h; ++row) {
			memcpy(out->buffer + ((size_t)(tile->y + row) * et.w + tile->x) * bpp, src + (size_t)row * tile->w * bpp, (size_t)tile->w * bpp);
		}
		if (layout.src < 0) {
			free(src);
		}
		for (i32 k = 0; k < 3; ++k) {
			free(comp[k]);
		}
		free(reach);
	}
	export_texture_write_texture(path, out, et.w, et.h, layout.src >= 0 ? layout.type : 3, layout.src >= 0 ? layout.off : 0);
	array_free(out);
	free(out);
	any_array_push(et.written, string_copy(path));
	et_stats[4]++;
}

// Jobs

static void export_tiled_add_job(slot_layer_t_array_t *layers, char *object_name, void *_) {
	if (et.njobs == et.njobs_cap) {
		et.njobs_cap = et.njobs_cap == 0 ? 8 : et.njobs_cap * 2;
		et.jobs      = realloc(et.jobs, sizeof(export_tiled_job_t) * et.njobs_cap);
	}
	et.jobs[et.njobs++] = (export_tiled_job_t){.layers = layers, .object_name = string_copy(object_name)};
}

static void export_tiled_job_end() {
	if (et.spool != NULL) {
		fclose(et.spool);
		et.spool = NULL;
		iron_delete_file(et.spool_path);
	}
	free(et.tiles);
	et.tiles = NULL;
	free(et.pyr);
	free(et.pyr_w);
	et.pyr   = NULL;
	et.pyr_w = NULL;
	if (et.levels != NULL) {
		free(et.levels[0]);
		free(et.levels);
		et.levels = NULL;
	}
}

static bool export_tiled_job_begin(export_tiled_job_t *job) {
	et.w         = et_res_w > 0 ? et_res_w : config_get_texture_res_x();
	et.h         = et_res_h > 0 ? et_res_h : config_get_texture_res_y();
	et.bits      = export_texture_bits();
	et.format    = layers_format(base_bits);
	et.bpp       = gpu_texture_format_size(et.format);
	et.padding   = g_context->export_padding;
	et.file      = export_texture_file_base(job->layers, job->object_name, &et.ext);
	et.udim_slot = export_texture_udim_slot(job->layers, job->object_name);
	et.halo      = et.padding ? (i32)math_ceil(1.25 * EXPORT_TILED_NEAR) + 2 : 0;
	if (et.padding) {
		util_uv_cache_dilate_map();
	}

	// Tile size: the platform's largest, halved while the targets exceed the budget
	i32 s = et.w > et.h ? et.w : et.h;
	s     = s > EXPORT_TILED_MAX_TILE ? EXPORT_TILED_MAX_TILE : s;
	if (et_test_tile > 0 && et_test_tile < s) {
		s = et_test_tile;
	}
	while (s > 256 && export_tiled_target_bytes(s, et.halo, et.format) > export_tiled_budget()) {
		s /= 2;
	}
	et.size = s;
	export_tiled_targets(s, et.halo);

	i32 nx    = (et.w + s - 1) / s;
	i32 ny    = (et.h + s - 1) / s;
	et.ntiles = nx * ny;
	et.tiles  = calloc(et.ntiles, sizeof(export_tiled_tile_t));
	for (i32 y = 0; y < ny; ++y) {
		for (i32 x = 0; x < nx; ++x) {
			export_tiled_tile_t *t = &et.tiles[y * nx + x];
			t->x                   = x * s;
			t->y                   = y * s;
			t->w                   = x * s + s <= et.w ? s : et.w - x * s;
			t->h                   = y * s + s <= et.h ? s : et.h - y * s;
		}
	}

	if (et.padding) {
		i32 m    = et.w > et.h ? et.w : et.h;
		et.ps    = (m + EXPORT_TILED_PYR - 1) / EXPORT_TILED_PYR;
		et.pw    = (et.w + et.ps - 1) / et.ps;
		et.ph    = (et.h + et.ps - 1) / et.ps;
		et.pyr   = calloc((size_t)et.pw * et.ph * 12, sizeof(f32));
		et.pyr_w = calloc((size_t)et.pw * et.ph, sizeof(f32));
	}

	static i32 serial = 0;
	char      *tmp    = getenv("TMPDIR");
	et.spool_path     = string("%s%sms_export_%d_%d.spool", tmp != NULL ? tmp : "/tmp", tmp != NULL && ends_with(tmp, "/") ? "" : "/", (i32)getpid(), serial++);
	et.spool          = fopen(et.spool_path, "w+b");
	if (et.spool == NULL) {
		console_error(string("Export: can not write %s", et.spool_path));
		return false;
	}
	return true;
}

// Material Maker buffers: the passes the fill layers' materials sample, rendered full at a raised cap (mm_buffers.c)
// before the tiles, pinned until the export ends. Only when the export is larger than the working resolution (or a test
// asks): at the working resolution layers come from the live ones, rendered with the normal cap.

static bool export_tiled_mm_wanted() {
	i32 w = et_res_w > 0 ? et_res_w : config_get_texture_res_x();
	i32 h = et_res_h > 0 ? et_res_h : config_get_texture_res_y();
	return et_test_mm || w > config_get_texture_res_x() || h > config_get_texture_res_y();
}

static void export_tiled_mm_begin() {
	u64              keys[256];
	i32              n         = 0;
	slot_material_t *_material = g_context->material;
	tool_type_t      _tool     = g_context->tool;
	g_context->tool            = TOOL_TYPE_FILL;
	any_array_t     *seen      = any_array_create_from_raw((void *[]){}, 0);
	for (i32 j = 0; j < et.njobs; ++j) {
		for (i32 i = 0; i < et.jobs[j].layers->length; ++i) {
			slot_material_t *m = et.jobs[j].layers->buffer[i]->fill_material;
			if (m == NULL || array_index_of(seen, m) >= 0 || n >= 256) {
				continue;
			}
			any_array_push(seen, m);
			g_context->material = m;
			make_material_parse_paint_material(false);
			n += mm_buffers_paint_keys(make_material_paint_scon, keys + n, 256 - n);
		}
	}
	g_context->material = _material;
	g_context->tool     = _tool;
	if (seen->length > 0) {
		make_material_parse_paint_material(false);
	}
	i32 w        = et_res_w > 0 ? et_res_w : config_get_texture_res_x();
	i32 h        = et_res_h > 0 ? et_res_h : config_get_texture_res_y();
	i32 cap      = mm_buffers_export_begin(keys, n, w > h ? w : h, mm_buffers_export_budget(export_tiled_budget()));
	et_mm_capped = true;
	et_stats[5]  = cap;
	et.units_total += n;
}

// API

static void export_tiled_finish(bool cancelled);

// Starts exporting textures as export_texture_run would, into path. False when there is nothing to export.
bool export_tiled_start(char *path, bool bake_material) {
	if (et.active) {
		export_tiled_cancel();
	}
	export_texture_init_preset();
	timeline_finalize(); // Strokes still being recorded land in their timelines
	memset(&et, 0, sizeof(et));
	memset(et_stats, 0, sizeof(et_stats));
	et.path    = string_copy(path);
	et.bake    = bake_material;
	et.written = any_array_create_from_raw((void *[]){}, 0);
	if (bake_material) {
		slot_layer_t_array_t *layers = export_texture_bake_begin();
		if (layers == NULL) {
			return false;
		}
		et.bake_source = g_context->material;
		export_tiled_add_job(layers, "", NULL);
	}
	else {
		export_texture_for_each_job(&export_tiled_add_job, NULL);
	}
	if (et.njobs == 0) {
		return false;
	}
	et.active          = true;
	et.height_coverage = util_uv_dilatemap_cached; // Height blended masks read it when there is one, as the legacy export
	et.bytes_base      = util_texture_tracked_bytes();
	util_texture_tracked_peak_reset();
	et_stats[1] = export_tiled_budget() / 1024;

	// Units of progress: per job its tiles and files, estimated with the default tile size
	i32 s = et_test_tile > 0 ? et_test_tile : EXPORT_TILED_MAX_TILE;
	i32 w = config_get_texture_res_x(), h = config_get_texture_res_y();
	i32 tiles       = ((w + s - 1) / s) * ((h + s - 1) / s);
	et.units_total = et.njobs * (tiles + box_export_preset->textures->length);
	et.phase       = -1;
	if (export_tiled_mm_wanted()) {
		export_tiled_mm_begin();
	}
	return true;
}

// Does export work for about budget_ms. True once the export is over (done, failed or cancelled).
bool export_tiled_step(f64 budget_ms) {
	if (!et.active) {
		return true;
	}
	f64 t0 = iron_time();
	while ((iron_time() - t0) * 1000.0 < budget_ms) {
		export_tiled_job_t *job = &et.jobs[et.job];
		if (et.phase == -1) { // Material Maker buffers full
			if (et_mm_capped && mm_buffers_export_step() > 0) {
				et.units_done++;
			}
			else {
				et.phase = 0;
			}
		}
		else if (et.phase == 0) {
			if (!export_tiled_job_begin(job)) {
				export_tiled_finish(true);
				return true;
			}
			et.tile  = 0;
			et.tex   = 0;
			et.phase = 1;
		}
		else if (et.phase == 1) {
			if (et.tile < et.ntiles) {
				export_tiled_render_tile(job, &et.tiles[et.tile++]);
				et.units_done++;
				i64 peak    = util_texture_tracked_peak() - et.bytes_base;
				et_stats[0] = peak / 1024 > et_stats[0] ? peak / 1024 : et_stats[0];
			}
			else {
				if (et.padding) {
					export_tiled_pull();
				}
				et.phase = 2;
			}
		}
		else if (et.phase == 2) {
			if (et.tex < box_export_preset->textures->length) {
				export_tiled_write_file(et.tex++);
				et.units_done++;
			}
			else {
				et.phase = 3;
			}
		}
		else {
			export_tiled_job_end();
			et.job++;
			et.phase = 0;
			if (et.job >= et.njobs) {
				export_tiled_finish(false);
				return true;
			}
		}
	}
	return false;
}

static void export_tiled_finish(bool cancelled) {
	export_tiled_job_end();
	export_tiled_release_targets();
	if (et_mm_capped) {
		mm_buffers_export_end();
		et_mm_capped = false;
	}
	if (cancelled) { // Files of this export go
		for (i32 i = 0; i < et.written->length; ++i) {
			if (g_context->layers_destination != EXPORT_DESTINATION_PACK_INTO_PROJECT) {
				iron_delete_file(et.written->buffer[i]);
			}
		}
	}
	else {
		if (et.bake) {
			export_texture_bake_end(et.bake_source);
		}
		export_texture_done();
	}
	free(et.jobs);
	et.jobs   = NULL;
	et.njobs  = 0;
	et.active = false;
}

void export_tiled_cancel() {
	if (et.active) {
		export_tiled_finish(true);
	}
}

bool export_tiled_running() {
	return et.active;
}

f32 export_tiled_progress() {
	if (!et.active) {
		return 1.0;
	}
	f32 p = et.units_total > 0 ? et.units_done / (f32)et.units_total : 0.0;
	return p > 0.99 ? 0.99 : p;
}

// Exports at once (scripts, command line)
void export_tiled_run(char *path, bool bake_material) {
	if (!export_tiled_start(path, bake_material)) {
		return;
	}
	while (!export_tiled_step(1.0e9)) {
	}
}

// The size of the textures exported, 0 the working resolution. Above it, layers are scaled from their live pixels until
// they replay their strokes (stroke_rec.h).
void export_tiled_set_resolution(i32 w, i32 h) {
	et_res_w = w;
	et_res_h = h;
}

// Tests

void export_tiled_test_tile(i32 size) {
	et_test_tile = size;
}

void export_tiled_test_raster(bool on) {
	et_test_raster = on;
}

void export_tiled_test_mm(bool on) {
	et_test_mm = on;
}

// 0 peak tile target KB of the last export, 1 its budget KB, 2 tiles rendered, 3 tiles skipped, 4 files, 5 mm cap,
// 6 tile target KB held now, 7 1 while a spool file is open, 8 files written by the export in progress, 9 layer tiles
// from timelines, 10 layer tiles with a timeline it could not materialize (the live layer instead)
i32 export_tiled_stat(i32 what) {
	if (what == 6) {
		i64 kb = 0;
		gpu_texture_t *t[] = {et_acc[0], et_acc[1], et_acc[2], et_temp, et_src[0], et_src[1], et_src[2], et_mask, et_mask_temp, et_mask_src, et_cov, et_reach};
		for (i32 i = 0; i < 12; ++i) {
			kb += t[i] != NULL ? (i64)t[i]->width * t[i]->height * gpu_texture_format_size(t[i]->format) / 1024 : 0;
		}
		return (i32)kb;
	}
	if (what == 7) {
		return et.spool != NULL ? 1 : 0;
	}
	if (what == 8) {
		return et.written != NULL ? et.written->length : 0;
	}
	return what >= 0 && what < 11 ? (i32)et_stats[what] : 0;
}

// The uv coverage the legacy padding dilates with
gpu_texture_t *export_tiled_test_coverage() {
	util_uv_cache_dilate_map();
	return util_uv_dilatemap;
}
