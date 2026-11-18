#include "../global.h"

// Hold a stroke to make it a curve. A brush stroke held still for a second at its end is taken back and becomes a curve
// through a few of its points (Ramer-Douglas-Peucker), a centripetal Catmull-Rom spline through them: two points for a
// straight stroke. The points sit on the surface, a world position picked under each with the camera it was placed
// from, so the view can turn while they are dragged; each segment paints from the camera of its end point (as path
// layers, util_path.c). Until confirmed the curve paints into the live layer, shown in place of the layer. Confirming
// paints it as a stroke of the layer through the brush (spacing, taper, jitter, the pen's pressure along the stroke):
// one undo step, recorded and replayed as any stroke. Undo drops it. Runtime only, never saved.

#define CURVE_MAX_POINTS  8
#define CURVE_CAPTURE_MAX 4096
#define CURVE_PROFILE     64
#define CURVE_HOLD_TIME   1.0  // Seconds still
#define CURVE_HOLD_MOVE   6.0  // Points (UI_SCALE) the touch may wander while still
#define CURVE_MIN_TRAVEL  24.0 // Points a stroke goes before holding it makes a curve
#define CURVE_FINGER_WAIT 0.35 // A finger touching while the curve shows confirms it unless a second one follows (as util_brush.c)

typedef struct curve_point {
	vec4_t world;
	f32    cam[9]; // util_camera_get
} curve_point_t;

typedef struct curve_sample {
	f32 x; // 0-1 of the paint view, through the camera of point cam
	f32 y;
	f32 t; // Along the curve, 0-1
	i32 cam;
} curve_sample_t;

static struct {
	bool          active;
	slot_layer_t *layer;
	curve_point_t pts[CURVE_MAX_POINTS];
	i32           n;
	bool          pen;
	f32           profile[CURVE_PROFILE]; // The pen's pressure along the stroke
	f32           altitude;
	f32           azimuth;
	u32           seed; // Jitter of its stamps, alike in the preview and the stroke
	bool          dirty;
	u64           brush_sig;
	bool          move_pending; // A drag moved a point: picked at the next update
	i32           move_index;
	f32           move_x;
	f32           move_y;
	bool          finger_pending; // A finger touched the canvas
	f64           finger_time;
	u32           gen; // A new curve: picks still in flight for the one before do not move its points
} curve = {0};

// The stroke painted now
static struct {
	f32             x[CURVE_CAPTURE_MAX]; // 0-1 of the paint view
	f32             y[CURVE_CAPTURE_MAX];
	f32             p[CURVE_CAPTURE_MAX]; // Pen pressure, -1 without a pen
	i32             n;
	f32             travel; // Pixels
	f64             still_time;
	f32             still_x;
	f32             still_y;
	bool            pen;
	f32             altitude;
	f32             azimuth;
	u32             serial; // Of the next history step when the stroke started: its own step is at least this (an address
	                        // could be a step freed since)
	bool            done;  // Held: no curve twice from one touch
	bool            touch; // Begun for the touch down now: a stroke start seen again (a first frame without time) keeps it
} cap = {0};

static curve_sample_t *curve_samples     = NULL;
static i32             curve_samples_n   = 0;
static i32             curve_samples_cap = 0;

bool util_curve_stroke_active() {
	return curve.active;
}

i32 util_curve_stroke_count() {
	return curve.active ? curve.n : 0;
}

// The live layer shows the curve in place of the layer
bool util_curve_stroke_previewing() {
	return curve.active && curve.layer == g_context->layer && render_path_paint_live_layer != NULL;
}

// ---- Capture and hold ----

static bool curve_stroke_tool() {
	slot_layer_t *l = g_context->layer;
	return (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER) && l != NULL && !slot_layer_is_group(l) &&
	       !slot_layer_is_path(l) && l->fill == NULL && !g_context->paint2d && !base_paint_in_2d() && !context_is_ruler_paint() &&
	       !context_is_decal();
}

// Where the pen or the mouse is, pixels
static f32 curve_input_x() {
	return pen_down("tip") ? pen_x : mouse_x;
}

static f32 curve_input_y() {
	return pen_down("tip") ? pen_y : mouse_y;
}

// A stroke starts (util_brush_update)
void util_curve_stroke_capture_begin() {
	if (cap.touch) {
		return;
	}
	cap.touch      = true;
	cap.n          = 0;
	cap.travel     = 0.0;
	cap.still_time = sys_time();
	cap.still_x    = curve_input_x();
	cap.still_y    = curve_input_y();
	cap.pen        = pen_down("tip");
	cap.altitude   = 0.0;
	cap.azimuth    = 0.0;
	cap.serial     = history_serial_peek();
	cap.done       = false;
}

static f32 curve_dist(f32 x0, f32 y0, f32 x1, f32 y1) { // Pixels between two points of the paint view
	f32 dx = (x1 - x0) * base_paint_view_w();
	f32 dy = (y1 - y0) * base_paint_view_h();
	return sqrtf(dx * dx + dy * dy);
}

// Distance of point i from the line a b, pixels
static f32 curve_line_dist(i32 i, i32 a, i32 b) {
	f32 w  = base_paint_view_w();
	f32 h  = base_paint_view_h();
	f32 ax = cap.x[a] * w, ay = cap.y[a] * h;
	f32 bx = cap.x[b] * w, by = cap.y[b] * h;
	f32 px = cap.x[i] * w, py = cap.y[i] * h;
	f32 dx = bx - ax, dy = by - ay;
	f32 l2 = dx * dx + dy * dy;
	if (l2 < 0.0001) {
		return sqrtf((px - ax) * (px - ax) + (py - ay) * (py - ay));
	}
	f32 t  = ((px - ax) * dx + (py - ay) * dy) / l2;
	t      = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	f32 qx = ax + t * dx - px, qy = ay + t * dy - py;
	return sqrtf(qx * qx + qy * qy);
}

// Ramer-Douglas-Peucker: keep[i] for the points of the capture within tol pixels of the polyline through them
static void curve_simplify(i32 a, i32 b, f32 tol, bool *keep) {
	f32 dmax = 0.0;
	i32 imax = -1;
	for (i32 i = a + 1; i < b; ++i) {
		f32 d = curve_line_dist(i, a, b);
		if (d > dmax) {
			dmax = d;
			imax = i;
		}
	}
	if (imax >= 0 && dmax > tol) {
		keep[imax] = true;
		curve_simplify(a, imax, tol, keep);
		curve_simplify(imax, b, tol, keep);
	}
}

// The points of the curve through the capture, CURVE_MAX_POINTS at most, into idx: their count
static i32 curve_fit(i32 *idx) {
	bool *keep = calloc(cap.n, sizeof(bool));
	f32   diag = sqrtf((f32)base_paint_view_w() * base_paint_view_w() + (f32)base_paint_view_h() * base_paint_view_h());
	f32   tol  = diag * 0.015;
	i32   n    = 0;
	for (i32 tries = 0; tries < 16; ++tries, tol *= 1.5) {
		memset(keep, 0, cap.n * sizeof(bool));
		keep[0]          = true;
		keep[cap.n - 1]  = true;
		curve_simplify(0, cap.n - 1, tol, keep);
		n = 0;
		for (i32 i = 0; i < cap.n; ++i) {
			n += keep[i] ? 1 : 0;
		}
		if (n <= CURVE_MAX_POINTS) {
			break;
		}
	}
	n = 0;
	for (i32 i = 0; i < cap.n && n < CURVE_MAX_POINTS; ++i) {
		if (keep[i]) {
			idx[n++] = i;
		}
	}
	free(keep);
	return n;
}

// The surface under 0-1 of the paint view, false off the mesh
static bool curve_pick(f32 x, f32 y, vec4_t *world) {
	vec4_t saved           = g_context->paint_vec;
	g_context->paint_vec.x = x;
	g_context->paint_vec.y = y;
	util_render_pick_pos_nor_tex();
	g_context->paint_vec = saved;
	if (!util_render_picked_on_mesh()) {
		return false;
	}
	*world = (vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 1.0};
	return true;
}

// The stroke becomes the curve: false when it can not (fewer than two of its points on the mesh)
static bool curve_from_capture() {
	if (cap.n < 2) {
		return false;
	}
	i32 idx[CURVE_MAX_POINTS];
	i32 n = curve_fit(idx);
	f32 cam[9];
	util_camera_get(cam);
	curve.n = 0;
	for (i32 i = 0; i < n; ++i) {
		curve_point_t *p = &curve.pts[curve.n];
		if (curve_pick(cap.x[idx[i]], cap.y[idx[i]], &p->world)) {
			memcpy(p->cam, cam, sizeof(cam));
			curve.n++;
		}
	}
	if (curve.n < 2) {
		return false;
	}

	// Pressure along the stroke, by its length
	f32 *len = malloc(sizeof(f32) * cap.n);
	len[0]   = 0.0;
	for (i32 i = 1; i < cap.n; ++i) {
		len[i] = len[i - 1] + curve_dist(cap.x[i - 1], cap.y[i - 1], cap.x[i], cap.y[i]);
	}
	f32 total = len[cap.n - 1] > 0.0 ? len[cap.n - 1] : 1.0;
	i32 j     = 0;
	for (i32 k = 0; k < CURVE_PROFILE; ++k) {
		f32 at = total * k / (CURVE_PROFILE - 1);
		while (j < cap.n - 2 && len[j + 1] < at) {
			j++;
		}
		f32 span = len[j + 1] - len[j];
		f32 f    = span > 0.0 ? (at - len[j]) / span : 0.0;
		f        = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
		curve.profile[k] = cap.p[j] + (cap.p[j + 1] - cap.p[j]) * f;
	}
	free(len);
	curve.pen      = cap.pen;
	curve.altitude = cap.n > 0 ? cap.altitude / cap.n : 0.0;
	curve.azimuth  = cap.n > 0 ? cap.azimuth / cap.n : 0.0;
	return true;
}

// Takes the stroke back and shows it as a curve, false when it can not be one
static bool curve_hold() {
	if (!curve_from_capture()) {
		return false;
	}
	// The stroke stops: its kept back taper stamps never paint, the release does nothing
	render_path_paint_tail_drop();
	g_context->brush_time        = 0;
	g_context->pdirty            = 0;
	g_context->prev_paint_vec_x  = -1;
	g_context->prev_paint_vec_y  = -1;
	g_context->brush_dab_x       = -1;
	g_context->brush_dab_y       = -1;
	g_context->brush_blend_dirty = true;
	history_step_t *last         = history_steps->length > 0 ? history_steps->buffer[history_steps->length - 1] : NULL;
	if (last != NULL && last->serial >= cap.serial && last->action == HISTORY_ACTION_PAINT && history_undos > 0) {
		history_undo();
	}
	curve.active         = true;
	curve.gen++;
	curve.layer          = g_context->layer;
	curve.seed           = ((u32)(sys_time() * 1000.0) * 2654435761u) | 1u;
	curve.dirty          = true;
	curve.move_pending   = false;
	curve.finger_pending = false;
	iron_haptic_tick(mouse_x, mouse_y);
	return true;
}

// No touch paints (util_brush_update): the next stroke start is a new stroke
void util_curve_stroke_capture_end() {
	cap.touch = false;
}

// Each frame of a stroke, after it painted (util_brush_update): true once it was held into a curve, the touch then
// paints no more
bool util_curve_stroke_capture() {
	if (cap.done) {
		return false;
	}
	f32 x = g_context->paint_vec.x;
	f32 y = g_context->paint_vec.y;
	if (cap.n == 0 || (curve_dist(cap.x[cap.n - 1], cap.y[cap.n - 1], x, y) > 0.5 && cap.n < CURVE_CAPTURE_MAX)) {
		if (cap.n > 0) {
			cap.travel += curve_dist(cap.x[cap.n - 1], cap.y[cap.n - 1], x, y);
		}
		cap.x[cap.n] = x;
		cap.y[cap.n] = y;
		cap.p[cap.n] = cap.pen ? pen_pressure : -1.0;
		cap.altitude += pen_altitude;
		cap.azimuth += pen_azimuth;
		cap.n++;
	}
	if (math_abs(curve_input_x() - cap.still_x) + math_abs(curve_input_y() - cap.still_y) > CURVE_HOLD_MOVE * UI_SCALE()) {
		cap.still_x    = curve_input_x();
		cap.still_y    = curve_input_y();
		cap.still_time = sys_time();
		return false;
	}
	if (sys_time() - cap.still_time < CURVE_HOLD_TIME || cap.travel < CURVE_MIN_TRAVEL * UI_SCALE() || !curve_stroke_tool()) {
		return false;
	}
	cap.done = true;
	return curve_hold();
}

// ---- The curve ----

static void curve_camera_restore(const f32 *c);

static void curve_samples_push(f32 x, f32 y, f32 t, i32 cam) {
	if (curve_samples_n == curve_samples_cap) {
		curve_samples_cap = curve_samples_cap == 0 ? 1024 : curve_samples_cap * 2;
		curve_samples     = realloc(curve_samples, sizeof(curve_sample_t) * curve_samples_cap);
	}
	curve_samples[curve_samples_n++] = (curve_sample_t){x, y, t, cam};
}

static f32 curve_knot(f32 t, vec2_t a, vec2_t b) {
	f32 dx = b.x - a.x;
	f32 dy = b.y - a.y;
	f32 d  = powf(dx * dx + dy * dy, 0.25); // Centripetal: the square root of the distance
	return t + (d > 0.0001 ? d : 0.0001);
}

static vec2_t curve_lerp(vec2_t a, vec2_t b, f32 t0, f32 t1, f32 t) {
	f32 f = (t - t0) / (t1 - t0);
	return (vec2_t){a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
}

// Centripetal Catmull-Rom between p1 and p2 at u 0-1 (Barry and Goldman)
static vec2_t curve_eval(vec2_t p0, vec2_t p1, vec2_t p2, vec2_t p3, f32 u) {
	f32    t0 = 0.0;
	f32    t1 = curve_knot(t0, p0, p1);
	f32    t2 = curve_knot(t1, p1, p2);
	f32    t3 = curve_knot(t2, p2, p3);
	f32    t  = t1 + (t2 - t1) * u;
	vec2_t a1 = curve_lerp(p0, p1, t0, t1, t);
	vec2_t a2 = curve_lerp(p1, p2, t1, t2, t);
	vec2_t a3 = curve_lerp(p2, p3, t2, t3, t);
	vec2_t b1 = curve_lerp(a1, a2, t0, t2, t);
	vec2_t b2 = curve_lerp(a2, a3, t1, t3, t);
	return curve_lerp(b1, b2, t1, t2, t);
}

// The point i in pixels of the paint view through the current camera
static bool curve_project(i32 i, vec2_t *out) {
	f32 sx, sy;
	if (!util_project_to_screen(curve.pts[i].world, &sx, &sy)) {
		return false;
	}
	*out = (vec2_t){sx * base_paint_view_w(), sy * base_paint_view_h()};
	return true;
}

// Both points of segment s are in the view of the camera of point c
static bool curve_segment_seen(i32 s, i32 c) {
	util_camera_set(curve.pts[c].cam);
	vec2_t a, b;
	f32    w = base_paint_view_w();
	f32    h = base_paint_view_h();
	return curve_project(s, &a) && curve_project(s + 1, &b) && a.x >= 0.0 && a.x <= w && a.y >= 0.0 && a.y <= h && b.x >= 0.0 && b.x <= w &&
	       b.y >= 0.0 && b.y <= h;
}

// The samples being made: where the last one is, the length so far
static struct {
	vec2_t last;
	f32    length;
	i32    cam;
	f32    max_len;
} curve_flat;

static void curve_emit(vec2_t q, bool restart) {
	if (!restart) {
		curve_flat.length += sqrtf((q.x - curve_flat.last.x) * (q.x - curve_flat.last.x) + (q.y - curve_flat.last.y) * (q.y - curve_flat.last.y));
	}
	curve_flat.last = q;
	curve_samples_push(q.x / base_paint_view_w(), q.y / base_paint_view_h(), curve_flat.length, curve_flat.cam);
}

// The curve from a (at u0) to b (at u1) as straight pieces: split where it strays more than half a pixel from the
// chord, or where the chord is longer than curve_flat.max_len (the pen's pressure changes along it). The brush's
// spacing places the dabs along each piece, as along the pieces of a pen stroke.
static void curve_flatten(vec2_t p0, vec2_t p1, vec2_t p2, vec2_t p3, f32 u0, vec2_t a, f32 u1, vec2_t b, i32 depth) {
	f32    um  = (u0 + u1) * 0.5;
	vec2_t m   = curve_eval(p0, p1, p2, p3, um);
	f32    dx  = b.x - a.x;
	f32    dy  = b.y - a.y;
	f32    len = sqrtf(dx * dx + dy * dy);
	f32    dev = len > 0.0001 ? math_abs((m.x - a.x) * dy - (m.y - a.y) * dx) / len : sqrtf((m.x - a.x) * (m.x - a.x) + (m.y - a.y) * (m.y - a.y));
	if (depth < 2 || (depth < 14 && (dev > 0.5 || len > curve_flat.max_len))) {
		curve_flatten(p0, p1, p2, p3, u0, a, um, m, depth + 1);
		curve_flatten(p0, p1, p2, p3, um, m, u1, b, depth + 1);
		return;
	}
	curve_emit(b, false);
}

// Points along the curve (pixels of the paint view apart as the curve bends, curve_flatten), each segment through the
// camera of its end point, or of its start point when only that one sees both (an end dragged from a view turned away)
static void curve_sample() {
	curve_samples_n = 0;
	f32 live[9];
	util_camera_get(live);
	curve_flat.length  = 0.0;
	curve_flat.max_len = curve.pen ? 24.0 : 96.0;
	i32 prev_cam       = -1;
	for (i32 s = 0; s < curve.n - 1; ++s) {
		i32 cam = curve_segment_seen(s, s + 1) || !curve_segment_seen(s, s) ? s + 1 : s;
		util_camera_set(curve.pts[cam].cam);
		vec2_t p1, p2, p0, p3;
		if (!curve_project(s, &p1) || !curve_project(s + 1, &p2)) {
			continue;
		}
		if (s == 0 || !curve_project(s - 1, &p0)) { // The ends continue straight
			p0 = (vec2_t){2.0 * p1.x - p2.x, 2.0 * p1.y - p2.y};
		}
		if (s + 2 >= curve.n || !curve_project(s + 2, &p3)) {
			p3 = (vec2_t){2.0 * p2.x - p1.x, 2.0 * p2.y - p1.y};
		}
		bool restart = prev_cam < 0 || memcmp(curve.pts[cam].cam, curve.pts[prev_cam].cam, sizeof(f32) * 9) != 0; // The start, or another view
		prev_cam     = cam;
		curve_flat.cam = cam;
		if (restart) {
			curve_emit(p1, true);
		}
		curve_flatten(p0, p1, p2, p3, 0.0, p1, 1.0, p2, 0);
	}
	curve_camera_restore(live);
	f32 length = curve_flat.length;
	for (i32 i = 0; i < curve_samples_n; ++i) {
		curve_samples[i].t = length > 0.0 ? curve_samples[i].t / length : 0.0;
	}
}

// The view's camera back (util_camera_get gave it), its projection as the view builds it
static void curve_camera_restore(const f32 *c) {
	scene_camera->base->transform->loc = (vec4_t){c[0], c[1], c[2], c[3]};
	scene_camera->base->transform->rot = (quat_t){c[4], c[5], c[6], c[7]};
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);
}

static f32 curve_pressure(f32 t) {
	f32 at = t * (CURVE_PROFILE - 1);
	i32 i  = (i32)at;
	i      = i < 0 ? 0 : i > CURVE_PROFILE - 2 ? CURVE_PROFILE - 2 : i;
	f32 f  = at - i;
	return curve.profile[i] + (curve.profile[i + 1] - curve.profile[i]) * f;
}

// Paints the samples through the brush, as a stroke of the pen or the finger that drew it, in one paint executor scope:
// its dabs resolve and draw together (paint_exec.c), not a pass each. The gbuffer is drawn again for each camera other
// than the view's (the scope closes first: dabs resolve against the depth they were placed on), the view's is back
// after.
static void curve_paint() {
	curve_sample();
	if (curve_samples_n == 0) {
		return;
	}
	f32 live[9];
	util_camera_get(live);
	bool stamps                        = render_path_paint_stamps();
	f32  ratio                         = base_window_aspect();
	render_path_paint_stamp_seed_fixed = curve.seed;
	util_brush_preview_pen             = curve.pen;
	util_brush_sample_altitude         = curve.pen ? curve.altitude : -1.0;
	util_brush_sample_azimuth          = curve.azimuth;
	util_brush_sample_roll             = 0.0;
	i32  applied                       = -1; // Point whose camera the gbuffer shows, -1 the view's
	bool moved                         = false;
	paint_exec_begin();
	for (i32 i = 0; i < curve_samples_n; ++i) {
		curve_sample_t *s     = &curve_samples[i];
		bool            first = i == 0;
		if (applied < 0 || memcmp(curve.pts[s->cam].cam, curve.pts[applied].cam, sizeof(f32) * 9) != 0) {
			bool same = memcmp(curve.pts[s->cam].cam, live, sizeof(live)) == 0;
			if (!same || moved) {
				paint_exec_end();
				util_camera_set(curve.pts[s->cam].cam);
				render_path_base_draw_gbuffer();
				paint_exec_begin();
				moved = !same;
			}
			first   = true; // From another view: the stroke goes on from here
			applied = s->cam;
		}
		curve_sample_t *q           = first ? s : &curve_samples[i - 1];
		g_context->paint_vec.x      = s->x;
		g_context->paint_vec.y      = s->y;
		g_context->decal_x          = s->x;
		g_context->decal_y          = s->y;
		g_context->last_paint_vec_x = q->x;
		g_context->last_paint_vec_y = q->y;
		g_context->prev_paint_vec_x = first ? s->x - 0.001 : q->x; // Directional brushes discard a dab without a previous position
		g_context->prev_paint_vec_y = q->y;
		g_context->brush_time       = i == 0 ? 0.0 : 1.0;
		g_context->brush_speed      = 0.5;
		if (!first && (s->x != q->x || s->y != q->y)) {
			g_context->brush_stroke_angle = atan2f(-(s->y - q->y), (s->x - q->x) * ratio);
		}
		util_brush_sample_pressure = curve.pen ? curve_pressure(s->t) : -1.0;
		g_context->pdirty          = 2;
		if (stamps) {
			if (i == 0) {
				g_context->brush_dab_x = -1.0;
				g_context->brush_dab_y = -1.0;
			}
			render_path_paint_commands_stamps();
		}
		else {
			render_path_paint_commands_symmetry(); // Mirror and radial copies as with the pen
			render_path_paint_commands_paint_stroke(false);
		}
	}
	if (stamps) { // The end taper
		g_context->pdirty = 2;
		render_path_paint_tail_flush();
	}
	paint_exec_end();
	render_path_paint_stamp_seed_fixed = 0;
	util_brush_preview_pen             = false;
	util_brush_sample_pressure         = -1.0;
	util_brush_sample_altitude         = -1.0;
	util_brush_sample_azimuth          = 0.0;
	if (moved) {
		curve_camera_restore(live);
		render_path_base_draw_gbuffer();
	}
}

// Brush settings, tool and paint shader the preview was painted with
static u64 curve_brush_signature() {
	u8 *a = (u8 *)&g_context->brush_nodes_radius;
	u8 *b = (u8 *)&g_context->brush_tilt_shading + sizeof(bool);
	u64 h = hash_fnv1a(HASH_FNV1A_BASIS, a, b - a);
	u64 v[4] = {(u64)g_context->tool, (u64)(uintptr_t)g_context->material, (u64)(uintptr_t)make_material_paint_scon,
	            (u64)(uintptr_t)g_context->layer};
	return hash_fnv1a(h, v, sizeof(v));
}

// The curve painted again into the live layer (render_path_paint_begin, before the gbuffer of the frame)
void util_curve_stroke_render() {
	if (!curve.active) {
		return;
	}
	if (curve.layer != g_context->layer || !slot_layer_alive(curve.layer)) {
		curve.active = false; // The layer went (undo of its creation, project closed)
		return;
	}
	u64 sig = curve_brush_signature();
	if (sig != curve.brush_sig) {
		curve.brush_sig = sig;
		curve.dirty     = true;
	}
	if (render_path_paint_live_layer != NULL) {
		render_path_paint_live_layer_used();
	}
	if (!curve.dirty || make_material_paint_pending()) {
		return;
	}
	curve.dirty = false;

	vec4_t paint_vec = g_context->paint_vec;
	f32    last_x    = g_context->last_paint_vec_x;
	f32    last_y    = g_context->last_paint_vec_y;
	render_path_paint_live_layer_copy();
	render_path_paint_use_live_layer(true);
	render_path_paint_clear_blend_stroke(); // The coverage of the stroke starts over
	render_path_paint_live_brush = true;    // A preview: Material Maker buffers as they are
	curve_paint();
	render_path_paint_live_brush = false;
	render_path_paint_use_live_layer(false);
	g_context->paint_vec         = paint_vec;
	g_context->last_paint_vec_x  = last_x;
	g_context->last_paint_vec_y  = last_y;
	g_context->brush_time        = 0.0;
	g_context->pdirty            = 0;
	g_context->brush_dab_x       = -1.0;
	g_context->brush_dab_y       = -1.0;
	g_context->brush_blend_dirty = true;
	g_context->ddirty            = 2;
	g_context->rtdirty           = 1;
}

// The pick of a dragged point, read back without waiting (util_render_pick_pos_nor_tex_async): one in flight, the
// latest drag position waits for it
static struct {
	gpu_transfer_t *t;
	bool            busy;
	bool            ready;
	bool            hit;
	i32             index;
	u32             gen;
	vec4_t          world;
	f32             cam[9]; // The view it was picked from
	vec4_t          loc;
	vec4_t          look;
	f32             far;
} curve_drag = {0};

static void curve_drag_done(gpu_transfer_t *t, void *ud) {
	f32   *v = gpu_transfer_map(t, 0);
	vec4_t p = {v[0], v[1], v[2], 1.0};
	f32    d = (p.x - curve_drag.loc.x) * curve_drag.look.x + (p.y - curve_drag.loc.y) * curve_drag.look.y + (p.z - curve_drag.loc.z) * curve_drag.look.z;
	curve_drag.hit   = d < curve_drag.far * 0.99; // Background reconstructs to the far plane (util_render_picked_on_mesh)
	curve_drag.world = p;
	curve_drag.busy  = false;
	curve_drag.ready = true;
}

// A finished pick moves its point, the latest drag position starts the next one
static void curve_drag_update() {
	if (curve_drag.ready) {
		curve_drag.ready = false;
		if (curve_drag.hit && curve_drag.gen == curve.gen && curve.active && curve_drag.index < curve.n) { // Off the mesh it stays
			curve.pts[curve_drag.index].world = curve_drag.world;
			memcpy(curve.pts[curve_drag.index].cam, curve_drag.cam, sizeof(curve_drag.cam));
			curve.dirty = true;
		}
	}
	if (!curve.move_pending || curve_drag.busy) {
		return;
	}
	curve.move_pending = false;
	if (curve_drag.t == NULL) {
		curve_drag.t = gpu_transfer_create(32);
	}
	camera_object_t *cam = scene_camera;
	curve_drag.index     = curve.move_index;
	curve_drag.gen       = curve.gen;
	util_camera_get(curve_drag.cam);
	curve_drag.loc  = (vec4_t){transform_world_x(cam->base->transform), transform_world_y(cam->base->transform), transform_world_z(cam->base->transform), 0.0};
	curve_drag.look = camera_object_look_world(cam);
	curve_drag.far  = cam->data->far_plane;
	curve_drag.busy = true;
	vec4_t       saved  = g_context->paint_vec;
	draw_saved_t gpu    = draw_suspend();
	g_context->paint_vec.x = curve.move_x;
	g_context->paint_vec.y = curve.move_y;
	util_render_pick_pos_nor_tex_async(curve_drag.t, curve_drag_done, NULL);
	g_context->paint_vec = saved;
	draw_resume(gpu);
}

// The curve as a stroke of its layer, one undo step
void util_curve_stroke_confirm() {
	if (!curve.active) {
		return;
	}
	for (i32 k = 0; k < 2 && (curve_drag.busy || curve.move_pending || curve_drag.ready); ++k) { // The last drag lands before it paints
		if (curve_drag.busy) {
			gpu_transfer_wait();
		}
		curve_drag_update();
	}
	if (curve_drag.busy) {
		gpu_transfer_wait();
		curve_drag_update();
	}
	curve.active         = false;
	curve.finger_pending = false;
	if (curve.layer != g_context->layer || !slot_layer_alive(curve.layer)) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	if (make_material_paint_pending()) {
		make_material_paint_ready(-1.0);
	}
	render_path_paint_clear_blend_stroke();
	render_path_base_draw_gbuffer();
	history_push_undo = true;
	history_paint();
	if (render_path_paint_fill_picks()) { // At its first draw, as util_brush.c
		render_path_paint_fill_pick = true;
	}
	curve_paint();
	render_path_paint_dilate_stroke(true, true);
	layers_update_linked_layers();
	g_context->pdirty              = 0;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
	g_context->brush_time          = 0.0;
	g_context->brush_blend_dirty   = true;
	g_context->layer_preview_dirty = true;
	g_context->prev_paint_vec_x    = -1.0;
	g_context->prev_paint_vec_y    = -1.0;
	g_context->brush_dab_x         = -1.0;
	g_context->brush_dab_y         = -1.0;
	draw_resume(gpu);
}

// Drops the curve: the stroke it came from stays undone
void util_curve_stroke_discard() {
	if (!curve.active) {
		return;
	}
	curve.active         = false;
	curve.finger_pending = false;
	curve.move_pending   = false;
	g_context->ddirty    = 2;
}

// A touch on the canvas while the curve shows (util_brush_update, before it paints). The pen and the mouse confirm it
// and paint. A finger confirms it once it is not the first of a two finger gesture (turning the view, the curve stays):
// true while the touch paints nothing.
bool util_curve_stroke_touch(bool started) {
	if (!curve.active) {
		return false;
	}
	bool finger = g_config->touch_ui && !pen_down("tip") && !pen_in_use;
	if (!finger) {
		if (started) {
			util_curve_stroke_confirm();
		}
		return false;
	}
	if (started && !curve.finger_pending) {
		curve.finger_pending = true;
		curve.finger_time    = sys_time();
	}
	return true;
}

// ---- Points ----

// Point i at 0-1 of the paint view through the current camera, false behind it
bool util_curve_stroke_point(i32 i, f32 *x, f32 *y) {
	if (!curve.active || i < 0 || i >= curve.n) {
		return false;
	}
	vec2_t p;
	if (!curve_project(i, &p)) {
		return false;
	}
	*x = p.x / base_paint_view_w();
	*y = p.y / base_paint_view_h();
	return true;
}

// Point i goes to the surface under 0-1 of the paint view, picked at the next update
void util_curve_stroke_move(i32 i, f32 x, f32 y) {
	if (!curve.active || i < 0 || i >= curve.n) {
		return;
	}
	curve.move_pending = true;
	curve.move_index   = i;
	curve.move_x       = x;
	curve.move_y       = y;
}

// Each frame (ui_base_update): a dragged point lands on the surface, a finger touch decides
void util_curve_stroke_update() {
	if (!curve.active) {
		return;
	}
	if (curve.finger_pending) {
		if (mouse_down("right")) { // Two fingers: the view turns
			curve.finger_pending = false;
		}
		else if (!mouse_down("left") || sys_time() - curve.finger_time > CURVE_FINGER_WAIT) {
			util_curve_stroke_confirm();
			return;
		}
	}
	curve_drag_update();
}

// ---- Scripts ----

// A stroke through the points xyp (x, y, pressure each, 0-1 of the paint view; pressure -1 without a pen) held into a
// curve, as the hold does: the last step is taken back when it is a stroke
bool util_curve_stroke_test_hold(f32_array_t *xyp) {
	cap.n    = 0;
	cap.pen  = xyp->length >= 3 && xyp->buffer[2] >= 0.0;
	cap.serial = 0;
	cap.altitude = 0.0;
	cap.azimuth  = 0.0;
	for (i32 i = 0; i + 2 < xyp->length && cap.n < CURVE_CAPTURE_MAX; i += 3) {
		cap.x[cap.n] = xyp->buffer[i];
		cap.y[cap.n] = xyp->buffer[i + 1];
		cap.p[cap.n] = xyp->buffer[i + 2];
		cap.n++;
	}
	draw_saved_t gpu = draw_suspend();
	bool         ok  = curve_hold();
	draw_resume(gpu);
	return ok;
}

// Point i in canvas pixels, axis 0 x, 1 y; -1 when it does not show
f32 util_curve_stroke_test_point(i32 i, i32 axis) {
	f32 x, y;
	if (!util_curve_stroke_point(i, &x, &y)) {
		return -1.0;
	}
	return axis == 0 ? base_paint_screen_x(x) : base_paint_screen_y(y);
}
