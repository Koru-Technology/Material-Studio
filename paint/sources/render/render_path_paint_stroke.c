#include "../global.h"

// The stroke model: stamps along the stroke with spacing, jitter, fall off and taper, pen samples, smoothing

// Brush stamps: spacing, jitter, fall off and taper
static u32 render_path_paint_stamp_seed = 1;

// Tests: strokes painted again jitter alike, 0 seeds from the time
void render_path_paint_set_stamp_seed(i32 seed) {
	render_path_paint_stamp_seed_fixed = (u32)seed;
}

static f32 render_path_paint_stamp_random() {
	render_path_paint_stamp_seed = render_path_paint_stamp_seed * 1664525u + 1013904223u;
	return (render_path_paint_stamp_seed >> 8) / 16777216.0f;
}

// Streamline and stabilization: two smoothing passes over the brush positions, timed so that pen samples and frames smooth alike
bool render_path_paint_filter_on() {
	return g_context->brush_smoothing > 0.0 || g_context->brush_stabilization > 0.0;
}

static f32 render_path_paint_filter_pass(f32 *v, f32 target, f32 amount, f32 dt) {
	f32 tau = amount * amount * 0.25; // Seconds the brush takes to catch up, about
	if (tau <= 0.0) {
		*v = target;
	}
	else {
		*v += (target - *v) * (1.0 - expf(-dt / tau));
	}
	return *v;
}

void render_path_paint_filter_apply(render_path_paint_filter_t *f, f32 *x, f32 *y, f32 dt) {
	if (!f->started) {
		f->started = true;
		f->x = f->x2 = *x;
		f->y = f->y2 = *y;
		return;
	}
	dt = dt < 0.0005 ? 0.0005 : dt > 0.1 ? 0.1 : dt;
	render_path_paint_filter_pass(&f->x2, *x, g_context->brush_stabilization * 0.8, dt);
	render_path_paint_filter_pass(&f->y2, *y, g_context->brush_stabilization * 0.8, dt);
	*x = render_path_paint_filter_pass(&f->x, f->x2, g_context->brush_smoothing, dt);
	*y = render_path_paint_filter_pass(&f->y, f->y2, g_context->brush_smoothing, dt);
}

// Taper and fall off lengths are in paint view heights
#define RENDER_PATH_PAINT_TAPER_LENGTH 0.6

static bool render_path_paint_stroke_pen = false; // Pencil stroke, it tapers by the pencil taper, else by the touch taper

static f32 render_path_paint_taper_start() {
	return (render_path_paint_stroke_pen ? g_context->brush_taper_start : g_context->brush_touch_taper_start) * RENDER_PATH_PAINT_TAPER_LENGTH;
}

static f32 render_path_paint_taper_end() {
	return (render_path_paint_stroke_pen ? g_context->brush_taper_end : g_context->brush_touch_taper_end) * RENDER_PATH_PAINT_TAPER_LENGTH;
}

// Spacing painted with: a continuous brush stamps closely when it tapers, fades or jitters, its stamps carry those
f32 render_path_paint_spacing() {
	f32 s = g_context->brush_spacing;
	if (s <= 0.0 && (g_context->brush_taper_start > 0.0 || g_context->brush_taper_end > 0.0 || g_context->brush_touch_taper_start > 0.0 ||
	                 g_context->brush_touch_taper_end > 0.0 || g_context->brush_falloff > 0.0 || g_context->brush_jitter_lateral > 0.0 ||
	                 g_context->brush_jitter_linear > 0.0 || g_context->brush_jitter_size > 0.0 || g_context->brush_jitter_opacity > 0.0 ||
	                 g_context->brush_jitter_angle > 0.0)) {
		s = 0.03;
	}
	return s;
}

bool render_path_paint_stamps() {
	bool tool = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE;
	return tool && render_path_paint_spacing() > 0.0 && !slot_layer_is_path(g_context->layer) && g_context->layer->fill_material == NULL &&
	       !context_is_decal() && !context_is_pixel_snap();
}

// A stamp to paint, kept back while the end taper may still shrink it
typedef struct {
	f32  x;
	f32  y;
	f32  len; // Stroke length at the stamp
	f32  radius;
	f32  opacity;
	f32  angle;
	f32  stroke_angle;
	f32  pressure; // Pen state of the stamp
	f32  altitude;
	f32  azimuth;
	f32  roll;
	bool preview_pen;
} render_path_paint_dab_t;

#define RENDER_PATH_PAINT_TAIL_MAX 1024

static render_path_paint_dab_t render_path_paint_tail[RENDER_PATH_PAINT_TAIL_MAX];
static i32                     render_path_paint_tail_first     = 0;
static i32                     render_path_paint_tail_count     = 0;
bool                           render_path_paint_tail_pending   = false; // The stroke ended, its tail paints with the next draw
static f32                     render_path_paint_stamp_prev_x   = 0.0;
static f32                     render_path_paint_stamp_prev_y   = 0.0;
static f32                     render_path_paint_stamp_dir      = 0.0; // Screen direction of the stroke at the last stamp, radians, y down
static bool                    render_path_paint_stamp_has_prev = false;
static f32                     render_path_paint_step_factor    = 1.0; // Spacing jitter of the next step

// First stamp of a stroke: seeds the jitter, the stroke length starts over
static void render_path_paint_stroke_begin(f32 x, f32 y) {
	render_path_paint_stamp_seed     = render_path_paint_stamp_seed_fixed != 0
	                                       ? render_path_paint_stamp_seed_fixed
	                                       : (u32)(x * 100003.0) ^ (u32)(y * 7919.0) ^ (u32)(sys_time() * 1000.0);
	g_context->brush_stroke_length   = 0.0;
	render_path_paint_stamp_has_prev = false;
	render_path_paint_stamp_dir      = -g_context->brush_stroke_angle; // Until the stroke shows its own direction
	render_path_paint_step_factor    = 1.0;
	render_path_paint_tail_first     = 0;
	render_path_paint_tail_count     = 0;
	render_path_paint_tail_pending   = false;
	render_path_paint_stroke_pen     = util_brush_pen_active();
}

static void render_path_paint_dab_paint(render_path_paint_dab_t *d, f32 size, f32 opacity, bool dilation) {
	f32  pressure                 = util_brush_sample_pressure;
	f32  altitude                 = util_brush_sample_altitude;
	f32  azimuth                  = util_brush_sample_azimuth;
	f32  roll                     = util_brush_sample_roll;
	bool preview_pen              = util_brush_preview_pen;
	util_brush_sample_pressure    = d->pressure;
	util_brush_sample_altitude    = d->altitude;
	util_brush_sample_azimuth     = d->azimuth;
	util_brush_sample_roll        = d->roll;
	util_brush_preview_pen        = d->preview_pen;
	g_context->brush_dab_radius   = d->radius * size;
	g_context->brush_dab_opacity  = d->opacity * opacity;
	g_context->brush_dab_angle    = d->angle;
	g_context->brush_stroke_angle = d->stroke_angle;
	g_context->paint_vec.x        = d->x;
	g_context->paint_vec.y        = d->y;
	g_context->last_paint_vec_x   = d->x;
	g_context->last_paint_vec_y   = d->y;
	if (g_context->brush_dab_opacity > 0.001 && g_context->brush_dab_radius > 0.001) {
		if (render_path_paint_batched()) { // Painted with the other stamps of the frame by render_path_paint_batch_flush
			render_path_paint_batch_add();
		}
		else {
			render_path_paint_commands_symmetry();
			render_path_paint_commands_paint(dilation);
		}
	}
	util_brush_sample_pressure = pressure;
	util_brush_sample_altitude = altitude;
	util_brush_sample_azimuth  = azimuth;
	util_brush_sample_roll     = roll;
	util_brush_preview_pen     = preview_pen;
}

// Smaller and fainter towards a tapered end, t 0 at the tip and 1 past the taper
static void render_path_paint_taper_factors(f32 t, f32 *size, f32 *opacity) {
	t        = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	t        = t * t * (3.0 - 2.0 * t);
	*size    = 1.0 - g_context->brush_taper_size * (1.0 - t);
	*opacity = 1.0 - g_context->brush_taper_opacity * (1.0 - t);
}

static render_path_paint_dab_t *render_path_paint_tail_at(i32 i) {
	return &render_path_paint_tail[(render_path_paint_tail_first + i) % RENDER_PATH_PAINT_TAIL_MAX];
}

// Paints the kept back stamps the end taper no longer reaches, and the oldest when there is no room for another
static void render_path_paint_tail_release(f32 len, f32 taper_end) {
	while (render_path_paint_tail_count > 0) {
		render_path_paint_dab_t *d = render_path_paint_tail_at(0);
		if (render_path_paint_tail_count < RENDER_PATH_PAINT_TAIL_MAX && d->len > len - taper_end) {
			break;
		}
		render_path_paint_dab_paint(d, 1.0, 1.0, false);
		render_path_paint_tail_first = (render_path_paint_tail_first + 1) % RENDER_PATH_PAINT_TAIL_MAX;
		render_path_paint_tail_count--;
	}
}

// The stroke ended: its kept back stamps paint, tapered towards the end
void render_path_paint_tail_flush() {
	paint_exec_begin(); // The dabs resolve together (paint_exec.c)
	f32 taper_end = render_path_paint_taper_end();
	f32 len       = g_context->brush_stroke_length;
	for (i32 i = 0; i < render_path_paint_tail_count; ++i) {
		render_path_paint_dab_t *d = render_path_paint_tail_at(i);
		f32                      size, opacity;
		render_path_paint_taper_factors(taper_end > 0.0 ? (len - d->len) / taper_end : 1.0, &size, &opacity);
		render_path_paint_dab_paint(d, size, opacity, false);
	}
	render_path_paint_tail_first   = 0;
	render_path_paint_tail_count   = 0;
	render_path_paint_tail_pending = false;
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(true);
	}
	paint_exec_end();
	g_context->brush_dab_radius  = 1.0;
	g_context->brush_dab_opacity = 1.0;
	g_context->brush_dab_angle   = 0.0;
}

// The brush was released: the stamps kept back for the end taper paint with the next draw
void render_path_paint_stroke_end() {
	if (render_path_paint_tail_count > 0) {
		render_path_paint_tail_pending = true;
		g_context->pdirty              = 1;
	}
}

static void render_path_paint_stamp(f32 x, f32 y, f32 aspect, f32 diameter, bool last) {
	// Direction and length of the stroke so far
	if (render_path_paint_stamp_has_prev) {
		f32 dx = (x - render_path_paint_stamp_prev_x) * aspect;
		f32 dy = y - render_path_paint_stamp_prev_y;
		f32 d  = math_sqrt(dx * dx + dy * dy);
		g_context->brush_stroke_length += d;
		if (d > 0.000001) {
			render_path_paint_stamp_dir = atan2f(dy, dx);
		}
	}
	render_path_paint_stamp_prev_x   = x;
	render_path_paint_stamp_prev_y   = y;
	render_path_paint_stamp_has_prev = true;
	f32 len                          = g_context->brush_stroke_length;

	// Jitter along and across the stroke
	f32 along  = (render_path_paint_stamp_random() * 2.0 - 1.0) * g_context->brush_jitter_linear * diameter;
	f32 across = (render_path_paint_stamp_random() * 2.0 - 1.0) * g_context->brush_jitter_lateral * diameter;
	f32 c      = math_cos(render_path_paint_stamp_dir);
	f32 s      = math_sin(render_path_paint_stamp_dir);

	render_path_paint_dab_t d;
	d.x            = x + (c * along - s * across) / aspect;
	d.y            = y + s * along + c * across;
	d.len          = len;
	d.radius       = 1.0 - g_context->brush_jitter_size * render_path_paint_stamp_random();
	d.opacity      = 1.0 - g_context->brush_jitter_opacity * render_path_paint_stamp_random();
	d.angle        = (render_path_paint_stamp_random() * 2.0 - 1.0) * math_pi() * g_context->brush_jitter_angle;
	d.stroke_angle = g_context->brush_rotation == BRUSH_ROTATION_FOLLOW ? -render_path_paint_stamp_dir : g_context->brush_stroke_angle;
	d.pressure     = util_brush_sample_pressure;
	d.altitude     = util_brush_sample_altitude;
	d.azimuth      = util_brush_sample_azimuth;
	d.roll         = util_brush_sample_roll;
	d.preview_pen  = util_brush_preview_pen;

	// The start tapers, the stroke fades out along its length
	f32 taper_start = render_path_paint_taper_start();
	if (taper_start > 0.0) {
		f32 size, opacity;
		render_path_paint_taper_factors(len / taper_start, &size, &opacity);
		d.radius *= size;
		d.opacity *= opacity;
	}
	if (g_context->brush_falloff > 0.0) {
		f32 k    = 1.0 - g_context->brush_falloff;
		f32 fade = 0.05 + k * k * 3.0;
		d.opacity *= math_max(0.0, 1.0 - len / fade);
	}

	f32 sj                        = g_context->brush_spacing_jitter;
	render_path_paint_step_factor = sj > 0.0 ? math_max(0.1, 1.0 + sj * (render_path_paint_stamp_random() * 2.0 - 1.0)) : 1.0;

	// The end taper reaches back from the pen: stamps within it wait until the pen has moved past them or the stroke ends
	f32 taper_end = render_path_paint_taper_end();
	if (taper_end > 0.0) {
		render_path_paint_tail_release(len, taper_end);
		*render_path_paint_tail_at(render_path_paint_tail_count) = d;
		render_path_paint_tail_count++;
		return;
	}
	render_path_paint_dab_paint(&d, 1.0, 1.0, last);
}

// Stamps from the last one to the brush position, one every spacing; the remainder carries over to the next frame
void render_path_paint_commands_stamps() {
	if (g_context->pdirty <= 0) {
		return;
	}
	paint_exec_begin();
	f32 cx       = g_context->paint_vec.x;
	f32 cy       = g_context->paint_vec.y;
	f32 aspect   = base_paint_view_aspect();
	f32 diameter = util_layer_brush_screen_radius() * 2.0;
	f32 step     = math_max(render_path_paint_spacing() * diameter, 0.0005);
	i32 stamps   = 0;

	if (g_context->brush_dab_x < 0.0) { // First stamp of the stroke
		render_path_paint_stroke_begin(cx, cy);
		render_path_paint_stamp(cx, cy, aspect, diameter, true);
		g_context->brush_dab_x = cx;
		g_context->brush_dab_y = cy;
		stamps++;
	}
	else {
		f32 dx    = (cx - g_context->brush_dab_x) * aspect;
		f32 dy    = cy - g_context->brush_dab_y;
		f32 dist  = math_sqrt(dx * dx + dy * dy);
		f32 sx    = g_context->brush_dab_x;
		f32 sy    = g_context->brush_dab_y;
		f32 along = step * render_path_paint_step_factor;
		while (along <= dist && stamps < 64) { // Keep the frame responsive on a fast stroke, the rest follows on the next frames
			f32 t = along / dist;
			f32 x = sx + (cx - sx) * t;
			f32 y = sy + (cy - sy) * t;
			render_path_paint_stamp(x, y, aspect, diameter, false);
			g_context->brush_dab_x = x;
			g_context->brush_dab_y = y;
			stamps++;
			along += step * render_path_paint_step_factor;
		}
	}
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(true);
	}
	else if (stamps > 0) {
		render_path_paint_dilate_after_paint();
	}
	paint_exec_end();

	g_context->paint_vec.x       = cx;
	g_context->paint_vec.y       = cy;
	g_context->brush_dab_radius  = 1.0;
	g_context->brush_dab_opacity = 1.0;
	g_context->brush_dab_angle   = 0.0;
}

// Pen samples of the frame, drawn one by one so fast curves stay round and the pressure varies along the stroke.
// Only for plain strokes, smoothing, rulers and snapping work on the frame position.
typedef struct {
	f32 x;
	f32 y;
	f32 p; // Pressure
	f32 a; // Altitude, azimuth and roll of the pen
	f32 z;
	f32 r;
} render_path_paint_point_t;

// The brush uniforms read the pressure and angles of this point instead of the current pen
static void render_path_paint_point_use(render_path_paint_point_t *pt) {
	util_brush_sample_pressure = pt->p;
	util_brush_sample_altitude = pt->a;
	util_brush_sample_azimuth  = pt->z;
	util_brush_sample_roll     = pt->r;
}

static void render_path_paint_point_clear() {
	util_brush_sample_pressure = -1.0;
	util_brush_sample_altitude = -1.0;
}

static f32 render_path_paint_angle_lerp(f32 a, f32 b, f32 t) {
	f32 d = b - a;
	while (d > math_pi()) {
		d -= 2.0 * math_pi();
	}
	while (d < -math_pi()) {
		d += 2.0 * math_pi();
	}
	return a + d * t;
}

static render_path_paint_point_t render_path_paint_point_lerp(render_path_paint_point_t *a, render_path_paint_point_t *b, f32 t) {
	return (render_path_paint_point_t){a->x + (b->x - a->x) * t,
	                                   a->y + (b->y - a->y) * t,
	                                   a->p + (b->p - a->p) * t,
	                                   a->a + (b->a - a->a) * t,
	                                   render_path_paint_angle_lerp(a->z, b->z, t),
	                                   render_path_paint_angle_lerp(a->r, b->r, t)};
}

#define RENDER_PATH_PAINT_POINTS_MAX 128

bool render_path_paint_use_samples() {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR) {
		return false;
	}
	if (pen_sample_count == 0 || !(pen_down("tip") || pen_released("tip")) || g_context->pdirty <= 0) {
		return false;
	}
	bool in_2d = context_in_2d_view(VIEW_2D_TYPE_LAYER);
	bool snap  = g_context->grid_snap_toggle || keymap_shortcut(any_map_get(g_keymap, "grid_snap"), SHORTCUT_TYPE_DOWN) ||
	            (g_config->view2d_grid_snap && in_2d) || context_is_pixel_snap();
	return g_context->brush_lazy_radius <= 0.0 && !context_is_ruler_paint() && !snap && !slot_layer_is_path(g_context->layer) &&
	       g_context->layer->fill_material == NULL && !context_is_decal();
}

static render_path_paint_filter_t render_path_paint_samples_filter = {0};
static f64                        render_path_paint_samples_time   = 0.0; // Of the last sample smoothed

// Frame samples in paint space, smoothed by the streamline and stabilization of the brush; the last one is the brush position
static i32 render_path_paint_points(render_path_paint_point_t *out) {
	f32  w      = (f32)base_paint_view_w();
	f32  h      = (f32)base_paint_view_h();
	i32  count  = 0;
	bool filter = render_path_paint_filter_on();
	for (i32 i = 0; i < pen_sample_count; ++i) {
		pen_sample_t *s = &pen_samples[i];
		f32           x = base_paint_x(s->x);
		f32           y = base_paint_y(s->y);
		if (filter) {
			f32 dt = render_path_paint_samples_filter.started ? (f32)(s->time - render_path_paint_samples_time) : 0.0;
			render_path_paint_filter_apply(&render_path_paint_samples_filter, &x, &y, dt > 0.0 ? dt : 1.0 / 240.0);
			render_path_paint_samples_time = s->time;
		}
		if (count > 0) {
			f32 dx = (x - out[count - 1].x) * w;
			f32 dy = (y - out[count - 1].y) * h;
			if (dx * dx + dy * dy < 0.25) { // Closer than half a pixel
				out[count - 1].p = s->pressure;
				out[count - 1].a = s->altitude;
				out[count - 1].z = s->azimuth;
				out[count - 1].r = s->roll;
				continue;
			}
		}
		if (count == RENDER_PATH_PAINT_POINTS_MAX) { // Too many for a frame, keep every other one
			for (i32 k = 0; k < count / 2; ++k) {
				out[k] = out[k * 2 + 1];
			}
			count /= 2;
		}
		out[count++] = (render_path_paint_point_t){x, y, s->pressure, s->altitude, s->azimuth, s->roll};
		// Drop the middle of three points when it adds nothing: within a third of a pixel of the line and the pressure about linear,
		// each point is a draw over the whole layer
		if (count >= 3) {
			render_path_paint_point_t *a  = &out[count - 3];
			render_path_paint_point_t *b  = &out[count - 2];
			render_path_paint_point_t *c  = &out[count - 1];
			f32                        ax = a->x * w, ay = a->y * h, bx = b->x * w, by = b->y * h, cx = c->x * w, cy = c->y * h;
			f32                        lx = cx - ax, ly = cy - ay;
			f32                        ll = lx * lx + ly * ly;
			f32                        t  = ll > 0.0 ? ((bx - ax) * lx + (by - ay) * ly) / ll : 0.0;
			t                             = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
			f32 dx = ax + lx * t - bx, dy = ay + ly * t - by;
			f32 dp = a->p + (c->p - a->p) * t - b->p;
			f32 da = a->a + (c->a - a->a) * t - b->a;
			if (dx * dx + dy * dy < 0.11 && dp < 0.02 && dp > -0.02 && da < 0.02 && da > -0.02) {
				*b = *c;
				count--;
			}
		}
	}
	if (count > 0 && !filter) {
		out[count - 1].x = g_context->paint_vec.x;
		out[count - 1].y = g_context->paint_vec.y;
	}
	return count;
}

static bool                      render_path_paint_samples_started = false;
static render_path_paint_point_t render_path_paint_samples_last;
static f32                       render_path_paint_samples_carry = 0.0; // Distance since the last stamp

void render_path_paint_samples_reset() {
	render_path_paint_samples_started        = false;
	render_path_paint_samples_filter.started = false;
}

// One capsule per sample, from the previous one
void render_path_paint_commands_samples() {
	render_path_paint_point_t pts[RENDER_PATH_PAINT_POINTS_MAX];
	i32                       count = render_path_paint_points(pts);
	paint_exec_begin();
	if (!render_path_paint_samples_started) {
		render_path_paint_samples_last    = pts[0];
		render_path_paint_samples_started = true;
	}
	f32 cx = g_context->paint_vec.x;
	f32 cy = g_context->paint_vec.y;
	for (i32 i = 0; i < count; ++i) {
		g_context->last_paint_vec_x = render_path_paint_samples_last.x;
		g_context->last_paint_vec_y = render_path_paint_samples_last.y;
		g_context->paint_vec.x      = pts[i].x;
		g_context->paint_vec.y      = pts[i].y;
		render_path_paint_point_use(&pts[i]);
		if (render_path_paint_batched()) {
			render_path_paint_batch_add();
		}
		else {
			render_path_paint_commands_symmetry();
			render_path_paint_commands_paint(i == count - 1);
		}
		render_path_paint_samples_last = pts[i];
	}
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(true);
	}
	paint_exec_end();
	render_path_paint_point_clear();
	g_context->paint_vec.x     = cx;
	g_context->paint_vec.y     = cy;
}

// Stamps every spacing along the samples, the pressure interpolated between them
void render_path_paint_commands_samples_stamps() {
	render_path_paint_point_t pts[RENDER_PATH_PAINT_POINTS_MAX];
	i32                       count    = render_path_paint_points(pts);
	f32                       cx       = g_context->paint_vec.x;
	f32                       cy       = g_context->paint_vec.y;
	f32                       aspect   = base_paint_view_aspect();
	f32                       diameter = util_layer_brush_screen_radius() * 2.0;
	f32                       step     = math_max(render_path_paint_spacing() * diameter, 0.0005);
	i32                       stamps   = 0;
	i32                       first    = 0;
	paint_exec_begin();

	if (g_context->brush_dab_x < 0.0 || !render_path_paint_samples_started) { // First stamp of the stroke
		render_path_paint_point_use(&pts[0]);
		render_path_paint_stroke_begin(pts[0].x, pts[0].y);
		render_path_paint_stamp(pts[0].x, pts[0].y, aspect, diameter, count == 1);
		render_path_paint_samples_last    = pts[0];
		render_path_paint_samples_carry   = 0.0;
		render_path_paint_samples_started = true;
		g_context->brush_dab_x            = pts[0].x;
		g_context->brush_dab_y            = pts[0].y;
		first                             = 1;
	}

	render_path_paint_point_t a = render_path_paint_samples_last;
	for (i32 i = first; i < count && stamps < 256; ++i) {
		render_path_paint_point_t b   = pts[i];
		f32                       dx  = (b.x - a.x) * aspect;
		f32                       dy  = b.y - a.y;
		f32                       len = math_sqrt(dx * dx + dy * dy);
		f32                       next = step * render_path_paint_step_factor;
		f32                       d    = next - render_path_paint_samples_carry; // Distance along the segment to the next stamp
		while (d <= len && stamps < 256) {
			f32                       t  = len > 0.0 ? d / len : 1.0;
			render_path_paint_point_t at = render_path_paint_point_lerp(&a, &b, t);
			render_path_paint_point_use(&at);
			render_path_paint_stamp(at.x, at.y, aspect, diameter, false);
			stamps++;
			next = step * render_path_paint_step_factor; // Jittered by the stamp
			d += next;
		}
		render_path_paint_samples_carry = len - (d - next);
		a                               = b;
	}
	render_path_paint_samples_last = a;
	g_context->brush_dab_x         = a.x;
	g_context->brush_dab_y         = a.y;
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(false);
	}
	if (stamps > 0) {
		render_path_paint_dilate_after_paint();
	}
	paint_exec_end();

	render_path_paint_point_clear();
	g_context->paint_vec.x       = cx;
	g_context->paint_vec.y       = cy;
	g_context->brush_dab_radius  = 1.0;
	g_context->brush_dab_opacity = 1.0;
	g_context->brush_dab_angle   = 0.0;
}

// Tests: taper of strokes, the pencil's and touch's alike (start, end in 0-1 of the taper length, then how much the size
// and the opacity thin out)
void render_path_paint_test_set_taper(f32 start, f32 end, f32 size, f32 opacity) {
	g_context->brush_taper_start       = start;
	g_context->brush_taper_end         = end;
	g_context->brush_touch_taper_start = start;
	g_context->brush_touch_taper_end   = end;
	g_context->brush_taper_size        = size;
	g_context->brush_taper_opacity     = opacity;
}
