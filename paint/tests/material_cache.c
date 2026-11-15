#include "global.h"

// Material caches (util_material_cache.c): paint draws read the material's outputs from textures rendered once instead
// of evaluating its graph per texel.
// - a node material at its uv coordinates (checker): its cache builds, the paint shader reads it, a stroke painted with
//   the cache matches the same stroke painted live
// - a graph edit makes the paint shader live at once, the cache builds again, strokes match again
// - a material reading the position (unlinked checker) paints live and still paints
// - triplanar painting and a texture scale other than 1 paint live, as with the cache off
// - a heavy Material Maker library material (Old Brick Wall, 784, about 1.4 s of GPU for the whole texture): cache once its
//   buffers are full, no long frame while
//   it builds, strokes match
// - eviction under a small budget, never the current material's cache; a material over the budget waits
// - a new project frees every cache
// Strokes compared start from the same stamp seed (render_path_paint_set_stamp_seed); the live brush preview keeps the
// camera from jittering (as stroke_refine.c).

int fails  = 0;
int checks = 0;
int phase  = 0;
int frame  = 0;
double t0  = 0.0;

slot_material_t *m_uv   = NULL; // Checker at the uv coordinates
slot_material_t *m_pos  = NULL; // Checker at the position
slot_material_t *m_lib  = NULL; // Old Brick Wall
slot_material_t *m_uv2  = NULL; // Another checker at the uv coordinates
ui_node_t       *checker = NULL;
int              evicted = 0;
float            max_build = 0.0; // Longest frame while a cache builds
int              entry_kb  = 0;

int STRIDE = 3; // Texels between samples

buffer_t *base_b = NULL; // The layer before the strokes
buffer_t *base_n = NULL;
buffer_t *base_p = NULL;
buffer_t *ref_b  = NULL; // A stroke painted one way
buffer_t *ref_n  = NULL;
buffer_t *ref_p  = NULL;
buffer_t *first_b = NULL; // The checker stroke before the graph edit

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[matcache] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *sample(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = p->length / (4 * STRIDE);
	buffer_t *c = buffer_create(n * 4);
	for (int i = 0; i < n; ++i) {
		int s = i * 4 * STRIDE;
		buffer_set_u8(c, i * 4, buffer_get_u8(p, s));
		buffer_set_u8(c, i * 4 + 1, buffer_get_u8(p, s + 1));
		buffer_set_u8(c, i * 4 + 2, buffer_get_u8(p, s + 2));
		buffer_set_u8(c, i * 4 + 3, buffer_get_u8(p, s + 3));
	}
	return c;
}

int absi(int v) {
	return v < 0 ? -v : v;
}

// Samples that differ by more than tol in a channel
int differ(buffer_t *a, buffer_t *b, int tol) {
	int n = 0;
	for (int i = 0; i < a->length; i += 4) {
		int d = 0;
		for (int c = 0; c < 4; ++c) {
			int e = absi(buffer_get_u8(a, i + c) - buffer_get_u8(b, i + c));
			d     = e > d ? e : d;
		}
		if (d > tol) {
			n = n + 1;
		}
	}
	return n;
}

int compare_max = 0;

// a against r where either was painted (differs from base): samples off by more than tol, per mille of those painted
int compare(char *name, buffer_t *a, buffer_t *r, buffer_t *base, int tol) {
	int painted = 0;
	int bad     = 0;
	int sum     = 0;
	compare_max = 0;
	for (int i = 0; i < a->length; i += 4) {
		int p = 0;
		int d = 0;
		for (int c = 0; c < 4; ++c) {
			int e = absi(buffer_get_u8(r, i + c) - buffer_get_u8(base, i + c));
			int g = absi(buffer_get_u8(a, i + c) - buffer_get_u8(base, i + c));
			p     = e > p ? e : p;
			p     = g > p ? g : p;
			int f = absi(buffer_get_u8(a, i + c) - buffer_get_u8(r, i + c));
			d     = f > d ? f : d;
		}
		if (p > 0) {
			painted = painted + 1;
			sum     = sum + d;
			compare_max = d > compare_max ? d : compare_max;
			if (d > tol) {
				bad = bad + 1;
			}
		}
	}
	int permille = painted > 0 ? bad * 1000 / painted : 0;
	printf("[matcache] %s: %d painted samples, %d off by more than %d (%d per mille), mean %.3f, max %d", name, painted, bad, tol, permille,
	       painted > 0 ? sum / (float)painted : 0.0, compare_max);
	if (painted < 500) {
		return 1000; // Nothing painted is no match
	}
	return permille;
}

void snap_base() {
	slot_layer_t *l = script_get_context()->layer;
	base_b          = sample(l->texpaint);
	base_n          = sample(l->texpaint_nor);
	base_p          = sample(l->texpaint_pack);
}

void snap_ref() {
	slot_layer_t *l = script_get_context()->layer;
	ref_b           = sample(l->texpaint);
	ref_n           = sample(l->texpaint_nor);
	ref_p           = sample(l->texpaint_pack);
}

// The current layer against the reference stroke: at most 2 per mille of the painted samples off by more than 2
void compare_ref(char *what, int tol) {
	slot_layer_t *l = script_get_context()->layer;
	int           b = compare(string("%s base", what), sample(l->texpaint), ref_b, base_b, tol);
	int           n = compare(string("%s normal", what), sample(l->texpaint_nor), ref_n, base_n, tol);
	int           p = compare(string("%s pack", what), sample(l->texpaint_pack), ref_p, base_p, tol);
	check(b <= 2 && n <= 2 && p <= 2, string("%s (%d, %d, %d per mille off by more than %d)", what, b, n, p, tol));
}

int changed() {
	slot_layer_t *l = script_get_context()->layer;
	return differ(sample(l->texpaint), base_b, 2);
}

void stroke() {
	render_path_paint_set_stamp_seed(4242);
	for (int i = 0; i < 24; ++i) {
		float t = i / 23.0;
		script_paint(0.3 + 0.4 * t, 0.42 + 0.12 * t);
	}
	script_paint_end();
}

int ready() {
	return material_cache_stat(2) == 3 && material_cache_stat(7) == 1;
}

void finish() {
	material_cache_test_budget(0);
	material_cache_test_min_ms(-1.0);
	material_cache_set_enabled(1);
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[matcache] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "MATCACHE PASS" : "MATCACHE FAIL");
	script_quit();
}

// Waits for the current material's cache, false (and a failed check) after the timeout
int wait_ready(char *what, float timeout) {
	if (ready()) {
		return 1;
	}
	if (sys_time() - t0 > timeout) {
		check(false, string("%s: the cache is ready within %.0f s (state %d, reads %d)", what, timeout, material_cache_stat(2), material_cache_stat(7)));
		finish();
		return -1;
	}
	return 0;
}

// The checker material: base color from a checker at the uv coordinates (linked) or the position (unlinked)
slot_material_t *make_checker(char *name, int uv) {
	slot_material_t *m   = script_material_create(name);
	ui_node_t       *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
	if (uv) {
		ui_node_t *tc = script_material_create_node("TEX_COORD");
		script_material_connect(tc, 2, chk, 0); // UV
	}
	script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
	script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
	script_material_set_float(chk, 1, 3, 23.0);
	script_material_connect(chk, 0, out, 0); // Base color
	script_material_connect(chk, 1, out, 3); // Roughness from the factor
	script_material_update();
	checker = chk;
	return m;
}

void next() {
	frame    = frame + 1;
	float ft = script_frame_time();
	if (phase == 0) { // The checker at the uv coordinates
		if (frame == 3) {
			script_layer_new();
			m_uv  = make_checker("CacheUV", 1);
			t0    = sys_time();
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) {
		int r = wait_ready("checker", 30.0);
		if (r < 0) {
			return;
		}
		if (r == 1 && frame > 2) {
			printf("[matcache] checker cached %.2f s after it was made (%d KB, parse %.1f ms)", sys_time() - t0, material_cache_stat(1), material_cache_stat(8) / 1000.0);
			check(material_cache_stat(0) >= 1, "the checker at the uv coordinates gets a cache, the paint shader reads it");
			snap_base();
			stroke();
			check(material_cache_stat(7) == 1, "the stroke painted with the cache");
			snap_ref();
			first_b = ref_b;
			history_undo();
			phase = 2;
			frame = 0;
		}
	}
	else if (phase == 2) { // The same stroke live
		if (frame == 3) {
			material_cache_set_enabled(0);
			stroke();
			check(material_cache_stat(7) == 0, "with the cache off the stroke paints live");
			compare_ref("checker: the cached stroke matches the live one", 2);
			history_undo();
			material_cache_set_enabled(1);
			t0    = sys_time();
			phase = 3;
			frame = 0;
		}
	}
	else if (phase == 3) { // A graph edit
		int r = wait_ready("checker again", 10.0);
		if (r < 0) {
			return;
		}
		if (r == 1) {
			script_material_set_color(checker, 1, 1, 0.2, 0.9, 0.3, 1.0);
			script_material_update();
			check(material_cache_stat(7) == 0, "a graph edit makes the paint shader live at once");
			phase = 4;
			frame = 0;
		}
	}
	else if (phase == 4) {
		if (frame == 2) {
			check(material_cache_stat(2) != 3, string("the edited material's cache builds again (state %d)", material_cache_stat(2)));
			t0 = sys_time();
		}
		if (frame > 2) {
			int r = wait_ready("edited checker", 30.0);
			if (r < 0) {
				return;
			}
			if (r == 1) {
				stroke();
				snap_ref();
				int d = differ(ref_b, first_b, 8);
				check(d > 500, string("the edit shows in the cached stroke (%d samples changed)", d));
				history_undo();
				phase = 5;
				frame = 0;
			}
		}
	}
	else if (phase == 5) {
		if (frame == 3) {
			material_cache_set_enabled(0);
			stroke();
			compare_ref("edited checker: the cached stroke matches the live one", 2);
			history_undo();
			material_cache_set_enabled(1);
			m_pos = make_checker("CachePos", 0);
			t0    = sys_time();
			phase = 6;
			frame = 0;
		}
	}
	else if (phase == 6) { // The checker at the position: unfit, paints live
		if (material_cache_stat(2) == 4 || sys_time() - t0 > 10.0) {
			check(material_cache_stat(2) == 4, string("a material reading the position gets no cache (state %d)", material_cache_stat(2)));
			stroke();
			int c = changed();
			check(material_cache_stat(7) == 0 && c > 1000, string("it paints live (%d samples changed)", c));
			history_undo();
			script_material_set(m_uv);
			t0    = sys_time();
			phase = 7;
			frame = 0;
		}
	}
	else if (phase == 7) { // Triplanar
		int r = wait_ready("checker picked again", 10.0);
		if (r < 0) {
			return;
		}
		if (r == 1) {
			script_get_context()->brush_paint = 1; // UV_TYPE_TRIPLANAR
			script_material_update();
			phase = 71;
			frame = 0;
		}
	}
	else if (phase == 71) {
		if (frame == 3) { // The view drawn again first (as for the strokes compared to it)
			stroke();
			check(material_cache_stat(7) == 0, "triplanar paints live");
			snap_ref();
			history_undo();
			phase = 8;
			frame = 0;
		}
	}
	else if (phase == 8) {
		if (frame == 3) {
			material_cache_set_enabled(0);
			stroke();
			compare_ref("triplanar: the stroke matches the one with the cache off", 0);
			history_undo();
			material_cache_set_enabled(1);
			script_get_context()->brush_paint = 0; // UV_TYPE_UVMAP
			script_get_context()->material->uv_scale = 2.0;
			script_material_update();
			phase = 9;
			frame = 0;
		}
	}
	else if (phase == 9) { // Texture scale 2
		if (frame == 3) {
			stroke();
			check(material_cache_stat(7) == 0, "a texture scale of 2 paints live");
			snap_ref();
			history_undo();
		}
		if (frame == 6) {
			material_cache_set_enabled(0);
			stroke();
			compare_ref("scale 2: the stroke matches the one with the cache off", 0);
			history_undo();
			material_cache_set_enabled(1);
			script_get_context()->material->uv_scale = 1.0;
			script_material_update();
			mm_library_count();
			int i = mm_library_find(784);
			printf("[matcache] picking %s", mm_library_name(i));
			mm_library_use(i);
			t0        = sys_time();
			max_build = 0.0;
			phase     = 10;
			frame     = 0;
		}
	}
	else if (phase == 10) { // Old Brick Wall
		if (frame > 2 && material_cache_stat(2) == 2 && ft > max_build) {
			max_build = ft;
		}
		int r = wait_ready("Old Brick Wall", 120.0);
		if (r < 0) {
			return;
		}
		if (r == 1 && frame > 2) {
			m_lib = script_get_context()->material;
			printf("[matcache] Old Brick Wall cached %.1f s after the pick: parse %.1f ms, %d bands, longest band %.1f ms of GPU", sys_time() - t0,
			       material_cache_stat(8) / 1000.0, material_cache_stat(11), material_cache_stat(5) / 1000.0);
			check(max_build < 0.1, string("no frame takes 100 ms while the cache builds (at most %.0f ms)", max_build * 1000.0));
			snap_base();
			stroke();
			snap_ref();
			history_undo();
			phase = 11;
			frame = 0;
		}
	}
	else if (phase == 11) {
		if (frame == 3) {
			material_cache_set_enabled(0);
			stroke();
			compare_ref("Old Brick Wall: the cached stroke matches the live one", 2);
			history_undo();
			material_cache_set_enabled(1);
			phase = 12;
			frame = 0;
		}
	}
	else if (phase == 12) { // Eviction: a budget for one cache and a half, two more materials cached in turn
		if (ready() && frame > 2) {
			entry_kb = material_cache_stat(1) / material_cache_stat(9);
			evicted  = material_cache_stat(4);
			printf("[matcache] %d caches ready of %d, %d KB each, budget %d KB", material_cache_stat(9), material_cache_stat(0), entry_kb, material_cache_stat(6));
			material_cache_test_budget(entry_kb * 3 / 2 / 1024 + 1);
			m_uv2 = make_checker("CacheUV2", 1);
			t0    = sys_time();
			phase = 13;
			frame = 0;
		}
	}
	else if (phase == 13) {
		int r = wait_ready("a second checker under the small budget", 15.0);
		if (r < 0) {
			return;
		}
		if (r == 1 && frame > 2) {
			script_material_set(m_uv);
			t0    = sys_time();
			phase = 131;
			frame = 0;
		}
	}
	else if (phase == 131) {
		int r = wait_ready("the first checker again under the small budget", 15.0);
		if (r < 0) {
			return;
		}
		if (r == 1 && frame > 2) {
			check(material_cache_stat(9) == 1 && material_cache_stat(4) - evicted >= 2,
			      string("a budget for one cache keeps the current material's, the others go (%d ready, %d evicted)", material_cache_stat(9),
			             material_cache_stat(4) - evicted));
			material_cache_test_budget(1); // Below one cache: the current one stays
			phase = 132;
			frame = 0;
		}
	}
	else if (phase == 132) {
		if (frame == 3) {
			check(ready() && material_cache_stat(9) == 1, "the current material's cache is never evicted, even over the budget");
			script_material_set(m_uv2); // Does not fit: waits, the other cache goes
		}
		if (frame == 90) { // Parsed and compiled by then, waiting for room
			check(material_cache_stat(2) == 1 && material_cache_stat(9) == 0 && material_cache_stat(7) == 0,
			      string("a material over the budget waits and paints live (state %d, %d ready)", material_cache_stat(2), material_cache_stat(9)));
			material_cache_test_budget(0);
			t0    = sys_time();
			phase = 14;
			frame = 0;
		}
	}
	else if (phase == 14) {
		int r = wait_ready("checker once the budget allows", 15.0);
		if (r < 0) {
			return;
		}
		if (r == 1) {
			check(true, "with room again, its cache builds");
			material_cache_test_min_ms(-1.0); // The default: cheap materials keep no cache
			script_material_create("CachePlain");
			t0    = sys_time();
			phase = 141;
			frame = 0;
		}
	}
	else if (phase == 141) { // A plain material: cheap, no cache kept
		if (material_cache_stat(2) == 4 || sys_time() - t0 > 10.0) {
			check(material_cache_stat(2) == 4 && material_cache_stat(7) == 0,
			      string("a plain material keeps no cache (state %d, %.2f ms of GPU for its whole texture)", material_cache_stat(2), material_cache_stat(10) / 1000.0));
			script_project_new();
			phase = 15;
			frame = 0;
		}
	}
	else if (phase == 15) { // A new project
		if (frame == 2) {
			check(material_cache_stat(0) <= 1 && material_cache_stat(9) == 0, string("a new project frees every cache (%d left)", material_cache_stat(0)));
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	script_get_config()->brush_live = 1;
	material_cache_test_min_ms(0.0); // The checkers are cheap: cached all the same
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
