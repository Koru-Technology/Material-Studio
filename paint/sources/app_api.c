// The native UI's interface to the app, see app_api.h. #included by main.c after minic_impl.c (it reuses the script_* helpers).

#include "app_api.h"
#include "app_api_test.h"

#define APP_SAME(a, b) ((i32)(a) == (i32)(b))
_Static_assert(APP_SAME(APP_TOOL_BRUSH, TOOL_TYPE_BRUSH) && APP_SAME(APP_TOOL_ERASER, TOOL_TYPE_ERASER) && APP_SAME(APP_TOOL_FILL, TOOL_TYPE_FILL) &&
                   APP_SAME(APP_TOOL_DECAL, TOOL_TYPE_DECAL) && APP_SAME(APP_TOOL_TEXT, TOOL_TYPE_TEXT) && APP_SAME(APP_TOOL_CLONE, TOOL_TYPE_CLONE) &&
                   APP_SAME(APP_TOOL_BLUR, TOOL_TYPE_BLUR) && APP_SAME(APP_TOOL_PARTICLE, TOOL_TYPE_PARTICLE) && APP_SAME(APP_TOOL_PICKER, TOOL_TYPE_PICKER),
               "app_api.h mirrors tool_type_t");
_Static_assert(APP_SAME(APP_VIEWPORT_LIT, VIEWPORT_MODE_LIT) && APP_SAME(APP_VIEWPORT_NORMAL_MAP, VIEWPORT_MODE_NORMAL_MAP) &&
                   APP_SAME(APP_VIEWPORT_TEXCOORD, VIEWPORT_MODE_TEXCOORD) && APP_SAME(APP_VIEWPORT_MATERIAL_ID, VIEWPORT_MODE_MATERIAL_ID) &&
                   APP_SAME(APP_VIEWPORT_MASK, VIEWPORT_MODE_MASK) && APP_SAME(APP_VIEWPORT_PATH_TRACE + 1, VIEWPORT_MODE_PLUGIN),
               "app_api.h mirrors viewport_mode_t");

static bool     app_engine_ready     = false;
static uint32_t app_previews_counter = 0;

// util_layer_update_preview redrew layer thumbnails
void app_notify_previews(void) {
	app_previews_counter++;
}

void app_set_ready(void) {
	app_engine_ready = true;
}

// The engine started and has a project. Every call needs it: the native UI can call before the first frame.
static bool app_ready(void) {
	return app_engine_ready && g_context != NULL && g_project != NULL && g_project->_ != NULL;
}

// Strings handed to Swift are never NULL: String(cString:) crashes on one
static const char *app_str(const char *s) {
	return s != NULL ? s : "";
}

// Signatures and keys: FNV-1a over the values a list or an item shows

#define APP_HASH_SEED 2166136261u

static uint32_t app_hash(uint32_t h, uint32_t v) {
	return (h ^ v) * 16777619u; // FNV-1a step
}

static uint32_t app_hash_str(uint32_t h, const char *s) {
	for (; s != NULL && *s != '\0'; ++s) {
		h = app_hash(h, (uint8_t)*s);
	}
	return h;
}

static uint32_t app_hash_ptr(uint32_t h, const void *p) {
	uint64_t v = (uint64_t)(uintptr_t)p;
	return app_hash(app_hash(h, (uint32_t)v), (uint32_t)(v >> 32));
}

// Bit for bit
static uint32_t app_hash_f32(uint32_t h, float v) {
	uint32_t u;
	memcpy(&u, &v, sizeof(u));
	return app_hash(h, u);
}

// To 1 / 10000: slider values that only drift below what shows do not change it
static uint32_t app_hash_fq(uint32_t h, float v) {
	return app_hash(h, (uint32_t)(int32_t)(v * 10000.0f));
}

static slot_layer_t *app_layer_by_id(int32_t id) {
	return app_ready() ? mask_stack_layer_by_id(id) : NULL;
}

static uint32_t app_layers_signature(void) {
	uint32_t h = APP_HASH_SEED;
	if (g_project == NULL || g_project->_ == NULL) {
		return h;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		h               = app_hash(h, (uint32_t)l->id);
		h               = app_hash(h, l->parent != NULL ? (uint32_t)l->parent->id : 0xffffffffu);
		h               = app_hash(h, l->visible);
		h               = app_hash(h, (uint32_t)(l->mask_opacity * 1000.0f));
		h               = app_hash(h, l->fill != NULL);
		h               = app_hash_str(h, l->name);
	}
	h = app_hash(h, g_context->layer != NULL ? (uint32_t)g_context->layer->id : 0xffffffffu);
	return h;
}

void app_get_state(app_state_t *out) {
	*out = (app_state_t){0};
	if (!app_ready()) {
		return;
	}
	out->ready             = true;
	out->tool              = g_context->tool;
	out->brush_radius      = g_context->brush_radius;
	out->brush_opacity     = g_context->brush_opacity;
	out->brush_hardness    = g_context->brush_hardness;
	out->undos             = history_undos + (util_curve_stroke_active() ? 1 : 0); // Undo drops a curve being edited
	out->redos             = history_redos;
	out->selected_layer_id = g_context->layer != NULL ? g_context->layer->id : -1;
	out->layers_signature  = app_layers_signature();
	out->previews_version  = app_previews_counter;
	out->layer_props_signature = app_layers_props_signature();
	out->view_mode         = ui_base_view_mode();
	out->viewport_mode     = g_context->viewport_mode;
	out->brush_angle       = g_context->brush_angle;
	out->sym_x             = g_context->sym_x;
	out->sym_y             = g_context->sym_y;
	out->sym_z             = g_context->sym_z;
	out->engine_modal      = ui_box_show || ui_menu_show || ui_brush_creator_shown() || material_studio_show;
	if (scene_camera != NULL) {
		out->camera_fov = scene_camera->data->fov;
	}
	out->camera_pivot    = g_context->camera_pivot;
	out->camera_controls = g_context->camera_controls;
	out->camera_type     = g_context->camera_type;
	out->projects_shown  = box_projects_native_shown;
	out->projects_closable = box_projects_native_closable;
	out->finger_paint    = g_config->finger_paint;
	out->curve_editing     = util_curve_stroke_active();
	out->curve_point_count = util_curve_stroke_count();
	out->curve_width       = util_curve_stroke_width();
	uint32_t h           = 2166136261u;
	for (i32 i = 0; i < g_config->recent_projects->length; ++i) {
		h = app_hash_str(h, g_config->recent_projects->buffer[i]);
	}
	out->projects_signature = h;
	if (scene_camera != NULL) {
		quat_t rot         = scene_camera->base->transform->rot;
		out->camera_rot[0] = rot.x;
		out->camera_rot[1] = rot.y;
		out->camera_rot[2] = rot.z;
		out->camera_rot[3] = rot.w;
	}
}

// Thumbnails, read back from the GPU


static float app_clamp01(float v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// RGBA of texel i as 0..1, straight alpha
static void app_texel(gpu_texture_t *t, uint8_t *px, i32 i, float *c) {
	switch (t->format) {
	case GPU_TEXTURE_FORMAT_RGBA64: { // Reads back in RGBA order
		uint16_t *s = (uint16_t *)px + i * 4;
		for (i32 k = 0; k < 4; ++k) {
			f32 v = half_to_float(s[k]);
			c[k]  = app_clamp01(v == v ? v : 0.0f); // NaN reads as 0
		}
		break;
	}
	case GPU_TEXTURE_FORMAT_RGBA128: {
		float *s = (float *)px + i * 4;
		for (i32 k = 0; k < 4; ++k) {
			c[k] = app_clamp01(s[k]);
		}
		break;
	}
	case GPU_TEXTURE_FORMAT_R8:
		c[0] = c[1] = c[2] = px[i] / 255.0f;
		c[3]               = 1.0f;
		break;
	default: // RGBA32 (render targets swapped by gpu_get_texture_pixels_rgba8) and BC7 ones (decompressed) in RGBA order
		for (i32 k = 0; k < 4; ++k) {
			c[k] = px[i * 4 + k] / 255.0f;
		}
		break;
	}
}

// Texels of a w x h texture of format as RGBA8 (raw: as read back, RGBA order), box filtered down by f into out
static void app_rgba8_convert(gpu_texture_t *fmt, i32 tw, uint8_t *raw, i32 f, bool premultiply, uint8_t *out, i32 ow, i32 oh) {
	if (f == 1 && !premultiply && fmt->format == GPU_TEXTURE_FORMAT_RGBA32) { // As it is
		memcpy(out, raw, (size_t)ow * oh * 4);
		return;
	}
	float n = (float)(f * f);
	for (i32 y = 0; y < oh; ++y) {
		for (i32 x = 0; x < ow; ++x) {
			float s[4] = {0, 0, 0, 0}; // Sums, premultiplied or not
			for (i32 j = 0; j < f; ++j) {
				for (i32 i = 0; i < f; ++i) {
					float c[4];
					app_texel(fmt, raw, (y * f + j) * tw + x * f + i, c);
					float a = premultiply ? c[3] : 1.0f;
					s[0] += c[0] * a;
					s[1] += c[1] * a;
					s[2] += c[2] * a;
					s[3] += c[3];
				}
			}
			uint8_t *o = &out[(y * ow + x) * 4];
			for (i32 k = 0; k < 4; ++k) {
				o[k] = (uint8_t)(app_clamp01(s[k] / n) * 255.0f + 0.5f);
			}
		}
	}
}

// Textures the native UI shows (material thumbnails, icons, brush images, studio tiles) are read back without waiting
// for the GPU: a read gives the texels of the texture as last written (gpu_texture_t.written) once they arrived, 0 until
// then, and asks for them; they come with a later frame and the previews version tells the UI to read again. A read at
// once waited behind the GPU's queue: material thumbnails held Book 1's open for 0.5 s on the iPad.
// The copy is recorded when asked for, while the texture surely exists; a texture is never used after that (a slot only
// compares its address), so one deleted meanwhile is no matter.
#define APP_READS_MAX   256
#define APP_READS_BATCH 32
#define APP_READS_BYTES (8 * 1024 * 1024) // Of a batch: a read that does not fit waits for the next one

typedef struct {
	gpu_texture_t *t; // Compared, never used: NULL for a free slot
	i32            max_size;
	bool           premultiply;
	u64            have;     // Stamp of rgba
	bool           inflight; // Copied in the batch being read back
	u32            gen;      // Changes when the slot is given to another read
	u32            used;     // Tick of the last read, the least recent slot is given away
	uint8_t       *rgba;
	i32            w;
	i32            h;
} app_read_t;

typedef struct {
	i32           slot;
	u32           gen;
	u64           stamp;
	gpu_texture_t fmt; // Format, size and state when recorded
	size_t        offset;
} app_read_item_t;

typedef struct {
	app_read_item_t items[APP_READS_BATCH];
	i32             count;
	size_t          used;
} app_read_batch_t;

static app_read_t        app_reads[APP_READS_MAX];
static u32               app_reads_tick     = 0;
static bool              app_reads_inflight = false;
static bool              app_reads_hooked   = false;
static app_read_batch_t *app_reads_batch    = NULL; // Recorded this frame, committed by app_reads_flush
static gpu_transfer_t   *app_reads_transfer = NULL;

static bool app_reads_async(gpu_texture_t *t) {
	return t->format == GPU_TEXTURE_FORMAT_RGBA32 || t->format == GPU_TEXTURE_FORMAT_RGBA64 || t->format == GPU_TEXTURE_FORMAT_RGBA128 ||
	       t->format == GPU_TEXTURE_FORMAT_R8;
}

static void app_reads_done(gpu_transfer_t *tr, void *ud) {
	app_read_batch_t *b   = ud;
	uint8_t          *mem = gpu_transfer_map(tr, 0);
	for (i32 i = 0; i < b->count; ++i) {
		app_read_item_t *it = &b->items[i];
		app_read_t      *r  = &app_reads[it->slot];
		if (r->gen != it->gen) {
			continue; // The slot went to another read meanwhile
		}
		r->inflight      = false;
		gpu_texture_t *f = &it->fmt;
		uint8_t       *raw = mem + it->offset;
#ifdef IRON_BGRA
		uint8_t *swapped = NULL;
		if (f->format == GPU_TEXTURE_FORMAT_RGBA32 && f->state == GPU_TEXTURE_STATE_RENDER_TARGET) { // As gpu_get_texture_pixels_rgba8
			size_t size = (size_t)f->width * f->height * 4;
			swapped     = malloc(size);
			for (size_t k = 0; k < size; k += 4) {
				swapped[k]     = raw[k + 2];
				swapped[k + 1] = raw[k + 1];
				swapped[k + 2] = raw[k];
				swapped[k + 3] = raw[k + 3];
			}
			raw = swapped;
		}
#endif
		i32 fac = 1;
		if (r->max_size > 0) {
			i32 big = f->width > f->height ? f->width : f->height;
			fac     = (big + r->max_size - 1) / r->max_size;
		}
		r->w    = f->width / fac;
		r->h    = f->height / fac;
		r->rgba = realloc(r->rgba, (size_t)r->w * r->h * 4);
		app_rgba8_convert(f, f->width, raw, fac, r->premultiply, r->rgba, r->w, r->h);
		r->have = it->stamp;
#ifdef IRON_BGRA
		free(swapped);
#endif
	}
	free(b);
	app_reads_inflight = false;
	app_notify_previews(); // The UI reads them again
}

// Each frame: the copies recorded since the last one are read back together
static void app_reads_flush(void *_) {
	if (app_reads_batch == NULL || app_reads_inflight) {
		return;
	}
	app_read_batch_t *b = app_reads_batch;
	app_reads_batch     = NULL;
	app_reads_inflight  = true;
	gpu_transfer_commit(app_reads_transfer, app_reads_done, b);
	iron_delay_idle_sleep(); // Its done runs in a later frame
}

// The read of t (at max_size, premultiplied or not): its slot, given the least recently read one the first time
static app_read_t *app_reads_slot(gpu_texture_t *t, i32 max_size, bool premultiply) {
	if (!app_reads_hooked) {
		app_reads_hooked = true;
		sys_notify_on_update(app_reads_flush, NULL);
	}
	if (t->written == 0) {
		gpu_texture_written(t); // A stamp of its own: another texture made at the same address is not this one
	}
	app_read_t *oldest = &app_reads[0];
	for (i32 i = 0; i < APP_READS_MAX; ++i) {
		app_read_t *r = &app_reads[i];
		if (r->t == t && r->max_size == max_size && r->premultiply == premultiply) {
			r->used = ++app_reads_tick;
			return r;
		}
		if (r->t == NULL || (oldest->t != NULL && r->used < oldest->used)) {
			oldest = r;
		}
	}
	oldest->t           = t;
	oldest->max_size    = max_size;
	oldest->premultiply = premultiply;
	oldest->have        = 0;
	oldest->inflight    = false;
	oldest->gen++;
	oldest->used = ++app_reads_tick;
	return oldest;
}

// t's texels copied into this frame's batch, false when it waits (a batch in flight, or this one full)
static bool app_reads_record(app_read_t *r, gpu_texture_t *t) {
	if (app_reads_inflight) {
		return false; // The transfer is not mapped while its batch is pending: asked again once it landed
	}
	size_t bytes = ((size_t)t->width * t->height * gpu_texture_format_size(t->format) + 255) & ~(size_t)255;
	if (app_reads_batch == NULL) {
		if (app_reads_transfer == NULL) {
			app_reads_transfer = gpu_transfer_create(0);
		}
		gpu_transfer_map(app_reads_transfer, APP_READS_BYTES);
		app_reads_batch = calloc(1, sizeof(app_read_batch_t));
	}
	app_read_batch_t *b = app_reads_batch;
	if (b->count == APP_READS_BATCH || b->used + bytes > APP_READS_BYTES) {
		return false;
	}
	app_read_item_t *it = &b->items[b->count++];
	it->slot            = (i32)(r - app_reads);
	it->gen             = r->gen;
	it->stamp           = t->written;
	it->fmt             = *t;
	it->offset          = b->used;
	b->used += bytes;
	draw_saved_t saved = draw_suspend();
	gpu_copy_regions_begin();
	gpu_copy_region_to_transfer(app_reads_transfer, t, 0, 0, t->width, t->height, it->offset);
	gpu_copy_regions_end();
	draw_resume(saved);
	r->inflight = true;
	return true;
}

// Reads in progress (the idle sleep, tests)
bool app_reads_busy(void) {
	return app_reads_inflight || app_reads_batch != NULL;
}

// Pixels of t as RGBA8 rows top to bottom, box filtered down by an integer factor to max_size or less (0: full size).
// premultiply: alpha multiplied in, so the transparent background does not darken the edges; straight alpha otherwise.
// With out NULL: the byte count needed, nothing read back. Returns the byte count written, 0 when none (yet: uncompressed
// textures are read back without waiting, see app_reads_slot).
static int32_t app_read_rgba8(gpu_texture_t *t, int32_t max_size, bool premultiply, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	if (t == NULL || t->width <= 0 || t->height <= 0 || t->format == GPU_TEXTURE_FORMAT_D32 || t->format == GPU_TEXTURE_FORMAT_R16 ||
	    t->format == GPU_TEXTURE_FORMAT_R32) {
		return 0;
	}
	i32 f = 1;
	if (max_size > 0) {
		i32 big = t->width > t->height ? t->width : t->height;
		f       = (big + max_size - 1) / max_size;
	}
	i32 ow    = t->width / f;
	i32 oh    = t->height / f;
	i32 bytes = ow * oh * 4;
	if (ow <= 0 || oh <= 0) {
		return 0;
	}
	if (out == NULL) {
		return bytes; // Known from the size, nothing read back
	}
	if (capacity < bytes) {
		return 0;
	}
	if (app_reads_async(t)) {
		app_read_t *r = app_reads_slot(t, max_size, premultiply);
		if (r->have != t->written || r->rgba == NULL || r->w != ow || r->h != oh) {
			if (!r->inflight) {
				app_reads_record(r, t);
			}
			return 0; // Asked for: it comes with a later frame
		}
		memcpy(out, r->rgba, bytes);
		*w = ow;
		*h = oh;
		return bytes;
	}
	draw_saved_t saved  = draw_suspend();
	buffer_t    *pixels = gpu_get_texture_pixels_rgba8(t); // RGBA order, render targets included (BC7 decompressed)
	draw_resume(saved);
	i32 need = t->width * t->height * (t->format == GPU_TEXTURE_FORMAT_R8 ? 1 : t->format == GPU_TEXTURE_FORMAT_RGBA64 ? 8 : t->format == GPU_TEXTURE_FORMAT_RGBA128 ? 16 : 4);
	if (pixels == NULL || pixels->buffer == NULL || pixels->length < need) {
		gpu_release_texture_pixels(t);
		return 0;
	}
	app_rgba8_convert(t, t->width, pixels->buffer, f, premultiply, out, ow, oh);
	gpu_release_texture_pixels(t);
	*w = ow;
	*h = oh;
	return bytes;
}

i32 app_overlay_top_inset = 0;

void app_set_overlay_top_inset(int32_t pixels) {
	app_overlay_top_inset = pixels > 0 ? pixels : 0;
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

// Tool and brush

void app_set_tool(int32_t tool) {
	if (!app_ready() || tool < 0 || tool > TOOL_TYPE_BAKE || tool == g_context->tool) {
		return;
	}
	context_select_tool(tool);
}

static void app_brush_changed(void) {
	config_brush_save_update();
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

void app_set_brush_radius(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_radius = math_max(0.01, math_min(2.0, value));
	app_brush_changed();
}

void app_set_brush_opacity(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_opacity = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

void app_set_brush_hardness(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_hardness = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

void app_set_brush_angle(float degrees) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_angle = math_max(0.0, math_min(360.0, degrees));
	make_material_parse_paint_material(true);
	app_brush_changed();
}

void app_set_symmetry(bool x, bool y, bool z) {
	if (!app_ready()) {
		return;
	}
	g_context->sym_x = x;
	g_context->sym_y = y;
	g_context->sym_z = z;
	app_brush_changed();
}

// Views and camera

void app_cycle_view_mode(void) {
	if (!app_ready()) {
		return;
	}
	ui_base_cycle_view_mode();
	iron_delay_idle_sleep();
}

void app_set_viewport_mode(int32_t mode) {
	if (!app_ready() || mode < APP_VIEWPORT_LIT || mode > APP_VIEWPORT_PATH_TRACE) {
		return;
	}
	context_set_viewport_mode(mode);
	iron_delay_idle_sleep();
}

// The engine stops updating after a while without input (IDLE_SLEEP), and touches on the native controls are not its
// input: an action waiting for the next frame would wait for the next touch on the canvas
void app_wake(void) {
	iron_delay_idle_sleep();
}

void app_view_face(int32_t face) {
	if (!app_ready()) {
		return;
	}
	viewport_set_face(face);
	iron_delay_idle_sleep();
}

void app_view_orbit(float x, float y) {
	if (!app_ready()) {
		return;
	}
	viewport_orbit(x, y);
	iron_delay_idle_sleep();
}

void app_view_roll(float radians, bool held) {
	if (!app_ready()) {
		return;
	}
	camera_roll(radians, held);
	iron_delay_idle_sleep();
}

// As the old Camera menu's FoV slider: a perspective camera moves along its view to keep the pivot's framing
void app_set_camera_fov(float fov) {
	if (!app_ready() || scene_camera == NULL) {
		return;
	}
	fov         = math_max(0.3, math_min(1.4, fov));
	f32 old_fov = scene_camera->data->fov;
	scene_camera->data->fov = fov;
	if (g_context->camera_type == CAMERA_TYPE_PERSPECTIVE && fov != old_fov) {
		transform_t *t    = scene_camera->base->transform;
		vec4_t       look = camera_object_look(scene_camera);
		f32          d    = vec4_dot(vec4_sub(camera_origins[camera_index()], t->loc), look);
		f32          d2   = d * math_tan(old_fov / 2.0) / math_tan(fov / 2.0);
		if (d > 0.0) {
			transform_move(t, look, d - d2);
		}
	}
	viewport_update_camera_type(g_context->camera_type);
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

void app_set_camera_pivot(int32_t pivot) {
	if (!app_ready()) {
		return;
	}
	g_context->camera_pivot = pivot == 1 ? 1 : 0;
}

void app_set_camera_controls(int32_t controls) {
	if (app_ready() && controls >= CAMERA_CONTROLS_ORBIT && controls <= CAMERA_CONTROLS_FLY) {
		g_context->camera_controls = controls;
	}
}

void app_set_brush_preview(bool on) {
	if (!app_ready()) {
		return;
	}
	render_path_paint_cursor_preview = on;
	render_path_base_redraw();
	iron_delay_idle_sleep();
}

void app_set_finger_paint(int32_t mode) {
	if (!app_ready() || mode < 0 || mode > 2) {
		return;
	}
	g_config->finger_paint = mode;
	config_save();
}

void app_set_camera_type(int32_t type) {
	if (!app_ready() || (type != CAMERA_TYPE_PERSPECTIVE && type != CAMERA_TYPE_ORTHOGRAPHIC)) {
		return;
	}
	g_context->camera_type = type;
	viewport_update_camera_type(type);
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

// The frame after the request is drawn without the grid, gizmo and decal box, the one after captures it
static void app_capture_screenshot(void *_) {
	viewport_capture_screenshot();
}

static void app_capture_screenshot_drawn(void *_) {
	sys_notify_on_next_frame(app_capture_screenshot, NULL);
}

// Projects screen

int32_t app_project_count(void) {
	return app_ready() ? g_config->recent_projects->length : 0;
}

const char *app_project_path(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return "";
	}
	return app_str(box_projects_recent_path(index));
}

const char *app_project_icon_path(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return "";
	}
	return app_str(project_icon_path(box_projects_recent_path(index)));
}

void app_project_open(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return;
	}
	char *path = box_projects_recent_path(index);
	if (!iron_file_exists(path)) {
		return;
	}
#ifdef IRON_IOS
	console_toast(tr("Opening project"));
#endif
	box_projects_load(path);
}

void app_project_delete(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return;
	}
	box_projects_delete(index);
}

void app_project_new(void) {
	if (!app_ready()) {
		return;
	}
	box_projects_new();
}

const char *app_project_entry(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return "";
	}
	return app_str(g_config->recent_projects->buffer[index]);
}

const char *app_project_rename(int32_t index, const char *name) {
#ifdef IRON_IOS
	if (index < 0 || index >= app_project_count() || name == NULL) {
		return "";
	}
	char *path = project_rename(box_projects_recent_path(index), string_copy((char *)name));
	if (path == NULL) {
		return "";
	}
	char *entry = string_copy(project_recent_entry(path));
	g_config->recent_projects->buffer[index] = entry;
	config_save();
	iron_delay_idle_sleep();
	return app_str(entry);
#else
	return "";
#endif
}

void app_projects_close(void) {
	box_projects_native_shown   = false;
	box_projects_import_pending = false;
	iron_delay_idle_sleep();
}

int32_t app_template_count(void) {
	if (!app_ready()) {
		return 0;
	}
	project_fetch_default_meshes();
	return project_default_mesh_list->length;
}

const char *app_template_id(int32_t index) {
	if (index < 0 || index >= app_template_count()) {
		return "";
	}
	return app_str(project_default_mesh_list->buffer[index]);
}

int32_t app_template_selected(void) {
	return app_template_count() > 0 ? g_context->project_type : -1;
}

void app_project_new_from_template(int32_t index) {
	if (index < 0 || index >= app_template_count()) {
		return;
	}
	g_context->project_type = index;
	box_projects_new();
}

bool app_take_new_project_request(void) {
	bool r                    = app_new_project_requested;
	app_new_project_requested = false;
	return r;
}

void app_project_import(void) {
	if (!app_ready()) {
		return;
	}
	box_projects_import();
}

void app_command(int32_t command) {
	if (!app_ready()) {
		return;
	}
	switch (command) {
	case APP_COMMAND_OPEN:
		project_open();
		break;
	case APP_COMMAND_OPEN_RECENT:
		box_projects_show();
		break;
	case APP_COMMAND_SAVE:
		project_save(false);
		break;
	case APP_COMMAND_SAVE_AS:
		project_save_as(false);
		break;
	case APP_COMMAND_IMPORT_TEXTURE:
		project_import_asset(string_array_join(path_texture_formats(), ","), false);
		break;
	case APP_COMMAND_IMPORT_FONT:
		project_import_asset("ttf,ttc,otf", true);
		break;
	case APP_COMMAND_IMPORT_MATERIAL:
		project_import_material();
		break;
	case APP_COMMAND_IMPORT_BRUSH:
		project_import_brush();
		break;
	case APP_COMMAND_IMPORT_SWATCHES:
		project_import_swatches(false);
		break;
	case APP_COMMAND_IMPORT_MESH:
		project_import_mesh(true, NULL);
		break;
	case APP_COMMAND_REIMPORT_MESH:
		project_reimport_mesh();
		break;
	case APP_COMMAND_REIMPORT_TEXTURES:
		project_reimport_textures();
		break;
	case APP_COMMAND_EXPORT_MESH:
		g_context->export_mesh_index = 0; // All
		box_export_show_mesh();
		break;
	case APP_COMMAND_EXPORT_SWATCHES:
		project_export_swatches();
		break;
	case APP_COMMAND_BAKE_MATERIAL:
		box_export_show_bake_material();
		break;
	case APP_COMMAND_VIEW_RESET:
		viewport_reset();
		viewport_scale_to_bounds(2.0);
		break;
	case APP_COMMAND_VIEW_FRAME:
		viewport_frame();
		break;
	case APP_COMMAND_ZOOM_IN:
		viewport_zoom(0.2);
		break;
	case APP_COMMAND_ZOOM_OUT:
		viewport_zoom(-0.2);
		break;
	case APP_COMMAND_ORBIT_OPPOSITE:
		viewport_orbit_opposite();
		break;
	case APP_COMMAND_SCREENSHOT:
		g_context->capturing_screenshot = true;
		g_context->ddirty               = 2;
		sys_notify_on_next_frame(app_capture_screenshot_drawn, NULL);
		break;
	case APP_COMMAND_DELETE_UNUSED:
		slot_material_delete_unused();
		util_texture_delete_unused();
		break;
	}
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

// History

const char *app_undo_name(void) {
	if (!app_ready() || history_undos <= 0) {
		return "";
	}
	history_step_t *step = history_steps->buffer[history_steps->length - 1 - history_redos];
	return app_str(step->name);
}

const char *app_redo_name(void) {
	if (!app_ready() || history_redos <= 0) {
		return "";
	}
	history_step_t *step = history_steps->buffer[history_steps->length - history_redos];
	return app_str(step->name);
}

app_history_notice_t app_history_notice(void) {
	return (app_history_notice_t){.serial = history_notice_serial, .redo = history_notice_redo, .name = app_str(history_notice_name)};
}

void app_undo(void) {
	if (!app_ready()) {
		return;
	}
	history_user_undo();
	iron_delay_idle_sleep();
}

void app_redo(void) {
	if (!app_ready()) {
		return;
	}
	history_user_redo();
	iron_delay_idle_sleep();
}

// Curve strokes

bool app_curve_point(int32_t index, float *x, float *y) {
	f32 px, py;
	if (!app_ready() || !util_curve_stroke_point(index, &px, &py) || px < 0.0 || px > 1.0 || py < 0.0 || py > 1.0) {
		return false;
	}
	*x = base_paint_screen_x(px);
	*y = base_paint_screen_y(py);
	return true;
}

void app_curve_move(int32_t index, float x, float y) {
	if (!app_ready()) {
		return;
	}
	util_curve_stroke_move(index, base_paint_x(x), base_paint_y(y));
	iron_delay_idle_sleep();
}

bool app_curve_remove(int32_t index) {
	return app_ready() && util_curve_stroke_remove(index);
}

float app_curve_point_size(int32_t index) {
	return app_ready() ? util_curve_stroke_size(index) : 1.0;
}

void app_curve_set_point_size(int32_t index, float size) {
	if (app_ready()) {
		util_curve_stroke_set_size(index, size);
		iron_delay_idle_sleep();
	}
}

void app_curve_set_width(float width) {
	if (app_ready()) {
		util_curve_stroke_set_width(width);
		iron_delay_idle_sleep();
	}
}

void app_curve_cancel(void) {
	if (!app_ready()) {
		return;
	}
	util_curve_stroke_discard();
	iron_delay_idle_sleep();
}

void app_curve_confirm(void) {
	if (!app_ready()) {
		return;
	}
	util_curve_stroke_confirm();
	iron_delay_idle_sleep();
}

bool app_history_busy(void) {
	return app_ready() && history_busy();
}

// Layers

int32_t app_layer_count(void) {
	return app_ready() ? layers_op_count() : 0;
}

static i32 app_layer_depth(slot_layer_t *l) {
	i32 depth = 0;
	for (slot_layer_t *p = l->parent; p != NULL; p = p->parent) {
		depth++;
	}
	return depth;
}

bool app_layer_info(int32_t index, app_layer_info_t *out) {
	i32 n = app_layer_count();
	if (index < 0 || index >= n) {
		return false;
	}
	slot_layer_t *l = g_project->_->layers->buffer[n - 1 - index]; // Stored bottom to top
	out->id         = l->id;
	out->parent_id  = l->parent != NULL ? l->parent->id : -1;
	out->depth      = app_layer_depth(l);
	out->kind       = slot_layer_is_group(l) ? APP_LAYER_KIND_GROUP
	                  : slot_layer_is_mask(l) ? APP_LAYER_KIND_MASK
	                  : slot_layer_is_filter(l) ? APP_LAYER_KIND_FILTER
	                                            : APP_LAYER_KIND_LAYER;
	out->visible    = l->visible;
	out->selected   = l == g_context->layer;
	out->is_fill    = l->fill != NULL;
	out->opacity    = l->mask_opacity;
	out->name       = app_str(l->name);
	return true;
}

bool app_layer_set_visible(int32_t id, bool visible) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->visible == visible) {
		return false;
	}
	layers_toggle_visible(l);
	iron_delay_idle_sleep();
	return true;
}

bool app_layer_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !layers_can_delete(l)) {
		return false;
	}
	layers_op_delete(l);
	iron_delay_idle_sleep();
	return true;
}

// The slider drag in progress, one undo step for the whole gesture: the opacity of a layer, a layer value
// (app_layer_value_begin) or a mask effect value (app_mask_effect_value_begin). Named by ids, not pointers: a layer
// deleted during the drag is not taken for a new one at the same address.
typedef enum {
	APP_DRAG_NONE,
	APP_DRAG_OPACITY,
	APP_DRAG_RAISE,
	APP_DRAG_LAYER_VALUE,
	APP_DRAG_MASK_VALUE,
} app_drag_kind_t;

typedef struct {
	app_drag_kind_t kind;
	i32             id;
	i32             index; // The value, or the mask effect (stack index)
	i32             field; // Mask effects: the opacity (-1) or a param
} app_drag_t;

static app_drag_t app_drag = {.kind = APP_DRAG_NONE};

static void app_drag_begin(app_drag_kind_t kind, i32 id, i32 index, i32 field) {
	app_drag = (app_drag_t){.kind = kind, .id = id, .index = index, .field = field};
}

// A set that is part of the drag in progress, otherwise it is its own undo step
static bool app_dragging(app_drag_kind_t kind, i32 id, i32 index, i32 field) {
	return app_drag.kind == kind && app_drag.id == id && app_drag.index == index && app_drag.field == field;
}

void app_drag_end(void) {
	app_drag = (app_drag_t){.kind = APP_DRAG_NONE};
}

void app_layer_opacity_begin(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return;
	}
	if (l != g_context->layer) {
		context_set_layer(l);
	}
	history_layer_opacity();
	app_drag_begin(APP_DRAG_OPACITY, id, 0, 0);
}

bool app_layer_set_opacity(int32_t id, float value) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	if (!app_dragging(APP_DRAG_OPACITY, id, 0, 0)) {
		app_layer_opacity_begin(id);
	}
	l->mask_opacity = math_max(0.0, math_min(1.0, value));
	if (l == g_project->_->layers->buffer[0] && l->mask_opacity < 1.0 && !g_project->_->translucency) {
		layers_enable_translucency();
		make_material_parse_mesh_material();
	}
	else { // A shader constant, see make_mesh_opacity
		g_context->ddirty = 2;
		render_path_base_redraw();
	}
	iron_delay_idle_sleep();
	return true;
}

void app_layer_raise_begin(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !slot_layer_is_layer(l)) {
		return;
	}
	if (l != g_context->layer) {
		context_set_layer(l);
	}
	history_layer_raise();
	app_drag_begin(APP_DRAG_RAISE, id, 0, 0);
}

bool app_layer_set_raise(int32_t id, float value) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !slot_layer_is_layer(l)) {
		return false;
	}
	if (!app_dragging(APP_DRAG_RAISE, id, 0, 0)) {
		app_layer_raise_begin(id);
	}
	f32  v       = math_max(-1.0, math_min(1.0, value));
	bool reshape = (l->height_raise != 0.0) != (v != 0.0); // The shader reads the raise while it is not 0
	l->height_raise = v;
	if (reshape) {
		make_material_parse_mesh_material();
	}
	else { // A shader constant, see make_mesh_write_raise
		g_context->ddirty = 2;
		render_path_base_redraw();
	}
	g_context->rtdirty = 1; // The path tracer's flattened layers
	iron_delay_idle_sleep();
	return true;
}

int32_t app_layer_thumbnail(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || (l->thumb == NULL && l->texpaint_preview == NULL)) {
		return 0;
	}
	i32 size  = l->thumb != NULL ? l->thumb_size : l->texpaint_preview->width;
	i32 bytes = size * size * 4;
	*w        = size;
	*h        = size;
	if (out == NULL) {
		return bytes;
	}
	if (l->thumb == NULL || l->thumb_size != size || capacity < bytes) {
		return 0; // Not read back yet: the list shows it once it is (app_notify_previews)
	}
	memcpy(out, l->thumb, bytes);
	return bytes;
}
