
#include "global.h"

// Drawing pad of the Brush Creator: scribbles painted with the brush being edited, on the plane facing the camera that the
// brush preview paints on (util_render_brush_scene_begin). The stroke being drawn paints in the live layer, a finished stroke is
// laid over the pad image. Strokes are kept as pen samples: a changed setting paints them all again, as they would look with it.

typedef struct {
	f32 x; // In the pad, 0-1
	f32 y;
	f32 pressure;
	f32 altitude;
	f32 azimuth;
} brush_pad_point_t;

typedef struct {
	brush_pad_point_t *points;
	i32                count;
	i32                capacity;
	bool               pen; // Drawn with the pencil, else a finger or the mouse
} brush_pad_stroke_t;

#define BRUSH_PAD_STROKES_MAX 32

static brush_pad_stroke_t brush_pad_strokes[BRUSH_PAD_STROKES_MAX];
static i32                brush_pad_count     = 0;
static i32                brush_pad_painted   = 0;     // Points of the last stroke painted so far
static bool               brush_pad_drawing   = false; // The last stroke is being drawn
static bool               brush_pad_ending    = false; // The last stroke ended, it is laid over the image on the next paint
static bool               brush_pad_live      = false; // The live layer holds the stroke being drawn
static bool               brush_pad_replay    = false; // All strokes paint again
static f64                brush_pad_replay_at = 0.0;   // Not before, settings change many times a second while a slider moves
static bool               brush_pad_scheduled = false;
static gpu_texture_t     *brush_pad_image     = NULL; // Finished strokes
static f32                brush_pad_view[4];          // Part of the paint view the pad shows: x, y, w, h, 0-1
static f32                brush_pad_size  = 0.5;      // Preview size of the brush
static f32                brush_pad_dab_x = -1.0;     // Last stamp of the stroke being drawn, kept between frames
static f32                brush_pad_dab_y = -1.0;

static void brush_pad_stroke_free(brush_pad_stroke_t *s) {
	free(s->points);
	s->points   = NULL;
	s->count    = 0;
	s->capacity = 0;
}

static void brush_pad_add_point(brush_pad_stroke_t *s, brush_pad_point_t p) {
	if (s->count > 0) { // Closer than a thousandth of the pad adds nothing
		brush_pad_point_t *l  = &s->points[s->count - 1];
		f32                dx = p.x - l->x;
		f32                dy = p.y - l->y;
		if (dx * dx + dy * dy < 0.000001) {
			l->pressure = p.pressure;
			return;
		}
	}
	if (s->count == s->capacity) {
		s->capacity = s->capacity == 0 ? 256 : s->capacity * 2;
		s->points   = realloc(s->points, s->capacity * sizeof(brush_pad_point_t));
	}
	s->points[s->count++] = p;
}

static void brush_pad_schedule();

// Paints all strokes again, soon
static void brush_pad_replay_soon(f64 delay) {
	brush_pad_replay    = true;
	brush_pad_replay_at = iron_time() + delay;
	brush_pad_schedule();
}

// A sample scribble, as the pad shows when the Brush Creator opens
static void brush_pad_add_scribble() {
	brush_pad_stroke_t *s = &brush_pad_strokes[brush_pad_count++];
	s->pen                = true;
	i32 n                 = 160;
	for (i32 i = 0; i <= n; ++i) {
		f32 t = i / (f32)n;
		f32 x = 0.2 + 0.6 * t + 0.12 * sinf(t * math_pi() * 6.0);
		f32 y = 0.8 - 0.6 * t + 0.08 * cosf(t * math_pi() * 6.0);
		f32 p = sinf(math_pi() * powf(t, 0.8));
		brush_pad_add_point(s, (brush_pad_point_t){x, y, p < 0.05 ? 0.05 : p, math_pi() / 2.0, 0.0});
	}
}

void brush_pad_clear() {
	for (i32 i = 0; i < brush_pad_count; ++i) {
		brush_pad_stroke_free(&brush_pad_strokes[i]);
	}
	brush_pad_count   = 0;
	brush_pad_drawing = false;
	brush_pad_ending  = false;
	brush_pad_replay_soon(0.0);
}

// The pad as it opens: the sample scribble alone
void brush_pad_reset() {
	brush_pad_clear();
	brush_pad_add_scribble();
}

// A setting of the brush changed, the strokes show it
void brush_pad_changed() {
	if (!brush_pad_drawing) {
		brush_pad_replay_soon(0.1);
	}
}

// Size of the brush on the pad, the brush's preview size 0-1
void brush_pad_set_size(f32 preview_size) {
	if (brush_pad_size != preview_size) {
		brush_pad_size = preview_size;
		brush_pad_changed();
	}
}

static f32 brush_pad_radius() {
	return 0.25 + 0.75 * brush_pad_size;
}

static void brush_pad_paint_point(brush_pad_stroke_t *s, i32 i, bool stamps) {
	brush_pad_point_t *p     = &s->points[i];
	brush_pad_point_t *q     = &s->points[i > 0 ? i - 1 : 0];
	f32                x     = brush_pad_view[0] + p->x * brush_pad_view[2];
	f32                y     = brush_pad_view[1] + p->y * brush_pad_view[3];
	f32                lx    = brush_pad_view[0] + q->x * brush_pad_view[2];
	f32                ly    = brush_pad_view[1] + q->y * brush_pad_view[3];
	f32                ratio = sys_w() / (f32)sys_h();

	g_context->paint_vec.x      = x;
	g_context->paint_vec.y      = y;
	g_context->last_paint_vec_x = lx;
	g_context->last_paint_vec_y = ly;
	g_context->prev_paint_vec_x = i > 0 ? lx : x - 0.001; // Directional brushes discard the dab without a previous position
	g_context->prev_paint_vec_y = ly;
	g_context->brush_time       = i == 0 ? 0.0 : 1.0; // The stroke starts at its first point and builds up after
	g_context->brush_speed      = 0.5;
	if (x != lx || y != ly) {
		g_context->brush_stroke_angle = atan2f(-(y - ly), (x - lx) * ratio);
	}
	util_brush_preview_pen     = s->pen;
	util_brush_sample_pressure = s->pen ? p->pressure : -1.0;
	util_brush_sample_altitude = s->pen ? p->altitude : -1.0;
	util_brush_sample_azimuth  = p->azimuth;
	util_brush_sample_roll     = 0.0;
	g_context->pdirty          = 2;
	if (stamps) {
		if (i == 0) {
			g_context->brush_dab_x = -1.0;
			g_context->brush_dab_y = -1.0;
		}
		render_path_paint_commands_stamps();
	}
	else {
		render_path_paint_commands_paint(false);
	}
}

static void brush_pad_clear_live() {
	slot_layer_clear(render_path_paint_live_layer, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	g_context->brush_blend_dirty = true; // The coverage of the stroke starts over
	render_path_paint_clear_blend();
	brush_pad_live = false;
}

// Lays the stroke in the live layer over the pad image
static void brush_pad_lay_over() {
	gpu_texture_t *live = render_path_paint_live_layer->texpaint;
	draw_begin(brush_pad_image, false, 0);
	draw_set_color(0xffffffff);
	draw_scaled_sub_image(live, brush_pad_view[0] * live->width, brush_pad_view[1] * live->height, brush_pad_view[2] * live->width,
	                      brush_pad_view[3] * live->height, 0, 0, brush_pad_image->width, brush_pad_image->height);
	draw_end();
}

// Paints the points of stroke s from first on, the stroke ends when finish
static void brush_pad_paint_stroke(i32 index, i32 first, bool finish, bool stamps) {
	brush_pad_stroke_t *s              = &brush_pad_strokes[index];
	render_path_paint_stamp_seed_fixed = (u32)(index * 7919 + 1); // Painted again, it jitters alike
	for (i32 i = first; i < s->count; ++i) {
		brush_pad_paint_point(s, i, stamps);
	}
	brush_pad_live = true;
	if (finish) {
		if (stamps) { // The end taper
			g_context->pdirty = 2;
			render_path_paint_tail_flush();
		}
		brush_pad_lay_over();
		brush_pad_clear_live();
	}
	render_path_paint_stamp_seed_fixed = 0;
}

static void brush_pad_paint(void *_) {
	brush_pad_scheduled = false;
	if (brush_pad_image == NULL || !ui_brush_creator_shown()) {
		return;
	}
	if (brush_pad_replay && iron_time() < brush_pad_replay_at) {
		brush_pad_schedule();
		return;
	}

	util_render_brush_scene_t sc;
	util_render_brush_scene_begin(&sc);
	g_context->brush_radius   = brush_pad_radius();
	g_context->brush_opacity  = 1.0;
	g_context->brush_hardness = sc.hardness;
	bool stamps               = render_path_paint_stamps();
	g_context->brush_dab_x    = brush_pad_dab_x;
	g_context->brush_dab_y    = brush_pad_dab_y;

	if (brush_pad_replay) {
		brush_pad_replay = false;
		draw_begin(brush_pad_image, true, 0x00000000);
		draw_end();
		brush_pad_clear_live();
		i32 last = brush_pad_count - (brush_pad_drawing ? 1 : 0); // The stroke being drawn goes on painting below
		for (i32 i = 0; i < last; ++i) {
			brush_pad_paint_stroke(i, 0, true, stamps);
		}
		brush_pad_painted = 0;
	}
	if ((brush_pad_drawing || brush_pad_ending) && brush_pad_count > 0) {
		i32 last = brush_pad_count - 1;
		if (brush_pad_painted == 0) {
			brush_pad_clear_live();
		}
		brush_pad_paint_stroke(last, brush_pad_painted, brush_pad_ending, stamps);
		brush_pad_painted = brush_pad_strokes[last].count;
		if (brush_pad_ending) {
			brush_pad_ending  = false;
			brush_pad_painted = 0;
		}
	}

	brush_pad_dab_x = g_context->brush_dab_x;
	brush_pad_dab_y = g_context->brush_dab_y;
	util_render_brush_scene_end(&sc);
	g_context->brush_blend_dirty = false; // The coverage of a stroke being drawn stays for its next points
	ui_brush_creator_redraw();
}

static void brush_pad_schedule() {
	if (!brush_pad_scheduled) {
		brush_pad_scheduled = true;
		sys_notify_on_next_frame(&brush_pad_paint, NULL);
	}
}

static bool brush_pad_in(f32 x, f32 y, f32 rx, f32 ry, f32 rw, f32 rh) {
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static f32 brush_pad_clamp(f32 v) {
	return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
}

// Pen and touches on the pad, at x, y, w, h on screen
void brush_pad_input(f32 x, f32 y, f32 w, f32 h) {
	bool pen_start   = pen_started("tip") && brush_pad_in(pen_x, pen_y, x, y, w, h);
	bool mouse_start = !pen_start && !pen_down("tip") && mouse_started("left") && brush_pad_in(mouse_x, mouse_y, x, y, w, h);
	if (!brush_pad_drawing && !brush_pad_ending && (pen_start || mouse_start)) {
		if (brush_pad_count == BRUSH_PAD_STROKES_MAX) { // The oldest stroke makes room
			brush_pad_stroke_free(&brush_pad_strokes[0]);
			for (i32 i = 1; i < brush_pad_count; ++i) {
				brush_pad_strokes[i - 1] = brush_pad_strokes[i];
			}
			brush_pad_count--;
			brush_pad_strokes[brush_pad_count] = (brush_pad_stroke_t){0};
			brush_pad_replay_soon(0.0);
		}
		brush_pad_stroke_t *s = &brush_pad_strokes[brush_pad_count++];
		*s                    = (brush_pad_stroke_t){0};
		s->pen                = pen_start;
		brush_pad_drawing     = true;
		brush_pad_painted     = 0;
	}
	if (!brush_pad_drawing) {
		return;
	}

	brush_pad_stroke_t *s = &brush_pad_strokes[brush_pad_count - 1];
	if (s->pen) {
		for (i32 i = 0; i < pen_sample_count; ++i) {
			pen_sample_t *p = &pen_samples[i];
			brush_pad_add_point(s, (brush_pad_point_t){brush_pad_clamp((p->x - x) / w), brush_pad_clamp((p->y - y) / h), p->pressure, p->altitude,
			                                           p->azimuth});
		}
		if (pen_sample_count == 0 && s->count == 0) {
			brush_pad_add_point(s, (brush_pad_point_t){brush_pad_clamp((pen_x - x) / w), brush_pad_clamp((pen_y - y) / h), pen_pressure, pen_altitude,
			                                           pen_azimuth});
		}
	}
	else {
		brush_pad_add_point(s, (brush_pad_point_t){brush_pad_clamp((mouse_x - x) / w), brush_pad_clamp((mouse_y - y) / h), 1.0, math_pi() / 2.0, 0.0});
	}

	bool down = s->pen ? pen_down("tip") : mouse_down("left");
	if (!down) {
		brush_pad_drawing = false;
		brush_pad_ending  = true;
	}
	brush_pad_schedule();
}

// Draws the pad at x, y, w, h in the current window, the paint view part shown keeps the pad's aspect
void brush_pad_draw(f32 x, f32 y, f32 w, f32 h) {
	if (w < 1 || h < 1) {
		return;
	}
	i32 iw = (i32)w;
	i32 ih = (i32)h;
	if (brush_pad_image == NULL || brush_pad_image->width != iw || brush_pad_image->height != ih) {
		if (brush_pad_image != NULL) {
			gpu_delete_texture(brush_pad_image);
		}
		brush_pad_image = gpu_create_render_target(iw, ih, GPU_TEXTURE_FORMAT_RGBA32);
		f32 pad_aspect  = w / h;
		f32 view_aspect = sys_w() / (f32)sys_h();
		if (pad_aspect < view_aspect) {
			brush_pad_view[2] = pad_aspect / view_aspect;
			brush_pad_view[3] = 1.0;
		}
		else {
			brush_pad_view[2] = 1.0;
			brush_pad_view[3] = view_aspect / pad_aspect;
		}
		brush_pad_view[0] = (1.0 - brush_pad_view[2]) / 2.0;
		brush_pad_view[1] = (1.0 - brush_pad_view[3]) / 2.0;
		brush_pad_replay_soon(0.0);
	}
	draw_set_color(0xffffffff);
	draw_scaled_image(brush_pad_image, x, y, w, h);
	if (brush_pad_live && render_path_paint_live_layer != NULL) { // The stroke being drawn
		gpu_texture_t *live = render_path_paint_live_layer->texpaint;
		draw_scaled_sub_image(live, brush_pad_view[0] * live->width, brush_pad_view[1] * live->height, brush_pad_view[2] * live->width,
		                      brush_pad_view[3] * live->height, x, y, w, h);
	}
}

// Painting on the pad, the Brush Creator waits with what would use the live layer
bool brush_pad_busy() {
	return brush_pad_drawing || brush_pad_ending || brush_pad_scheduled;
}
