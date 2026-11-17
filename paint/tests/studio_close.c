#include "global.h"

// A fill layer of a heavy Material Maker library material (Medieval Stone Brick Wall, 1403) keeps its detail after the
// Material Studio opens and closes on its material: once the buffers are full again it matches a fill layer made then

int fails  = 0;
int checks = 0;
int phase  = 0;
int frame  = 0;

slot_material_t *m0     = NULL;
slot_layer_t    *fill1  = NULL;
double           t0     = 0.0;
int              detail0 = 0;
buffer_t        *snap_b = NULL;
int              last_log = -1;

int STRIDE = 5;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[close] %s %s", ok ? "ok  " : "FAIL", name);
}

void finish() {
	printf("[close] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "CLOSE PASS" : "CLOSE FAIL");
	script_quit();
}

buffer_t *sample(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = p->length / (4 * STRIDE);
	buffer_t *c = buffer_create(n * 4);
	for (int i = 0; i < n; ++i) {
		int s = i * 4 * STRIDE;
		for (int k = 0; k < 4; ++k) {
			buffer_set_u8(c, i * 4 + k, buffer_get_u8(p, s + k));
		}
	}
	return c;
}

int absi(int v) {
	return v < 0 ? -v : v;
}

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

// Fine detail: mean difference of the red channel between neighbouring texels inside the uv islands, x100
int detail(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	int       w = 1;
	while (w * w * 4 < p->length) {
		w = w * 2;
	}
	int sum = 0;
	int n   = 0;
	for (int y = 0; y < w; y += 7) {
		for (int x = 0; x < w - 1; x += 7) {
			int i = (y * w + x) * 4;
			if (buffer_get_u8(p, i + 3) < 250 || buffer_get_u8(p, i + 7) < 250) {
				continue;
			}
			sum = sum + absi(buffer_get_u8(p, i) - buffer_get_u8(p, i + 4));
			n   = n + 1;
		}
	}
	return n > 0 ? sum * 100 / n : 0;
}

int picked() {
	return script_get_context()->material != m0 && mm_buffers_stat(11) > 0;
}

int full() {
	return mm_buffers_stat(11) == 0 && fill_stat(1) == 0;
}

void log_progress() {
	int s = (int)(sys_time() - t0);
	if (s != last_log) {
		last_log = s;
		printf("[close] %d s: %d passes queued, %d fill tiles waiting, painted again %d times", s, mm_buffers_stat(11), fill_stat(1),
		       fill_stat(3));
	}
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 3) {
			m0 = script_get_context()->material;
			mm_library_count();
			int i = mm_library_find(1403);
			printf("[close] picking %s", mm_library_name(i));
			mm_library_use(i);
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) {
		if (picked()) {
			fill1 = script_layer_new_fill();
			t0    = sys_time();
			phase = 2;
			frame = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles");
			finish();
			return;
		}
	}
	else if (phase == 2) {
		log_progress();
		if (frame > 5 && (full() || sys_time() - t0 > 120.0)) {
			check(full(), "the buffers become full");
			script_layer_set(fill1);
			detail0 = detail(fill1->texpaint);
			snap_b  = sample(fill1->texpaint);
			printf("[close] fill layer detail %d before the studio", detail0);
			script_screenshot(string("%s/close_before.png", script_save_path()));
			material_studio_open(script_get_context()->material);
			phase = 3;
			frame = 0;
		}
	}
	else if (phase == 3) {
		if (frame == 90) {
			check(material_studio_is_open(), "the studio opened");
			material_studio_close();
			t0    = sys_time();
			phase = 4;
			frame = 0;
		}
	}
	else if (phase == 4) {
		if (frame == 3) {
			printf("[close] right after close: detail %d, %d passes queued, %d fill tiles waiting", detail(fill1->texpaint), mm_buffers_stat(11),
			       fill_stat(1));
		}
		log_progress();
		if (frame > 10 && (full() || sys_time() - t0 > 120.0)) {
			check(full(), "the buffers become full again after the studio closes");
			int d = detail(fill1->texpaint);
			int c = differ(sample(fill1->texpaint), snap_b, 6);
			int s = snap_b->length / 4;
			printf("[close] fill layer detail %d after (before %d), %d of %d samples differ from before", d, detail0, c, s);
			check(d * 10 >= detail0 * 8, string("the fill layer keeps its detail (%d, before %d)", d, detail0));
			script_screenshot(string("%s/close_after.png", script_save_path()));
			slot_layer_t *ref = script_layer_new_fill();
			phase             = 5;
			frame             = 0;
			snap_b            = sample(ref->texpaint);
			printf("[close] fresh fill layer detail %d", detail(ref->texpaint));
		}
	}
	else if (phase == 5) {
		if (frame == 5) {
			int c = differ(sample(fill1->texpaint), snap_b, 6);
			int s = snap_b->length / 4;
			check(c * 100 <= s, string("it matches a fill layer made now (%d of %d samples differ)", c, s));
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
