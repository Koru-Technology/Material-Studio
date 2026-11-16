
#include "../global.h"
#include "util_hash.h"

// Mask stacks: a mask is a list of effects evaluated bottom to top from black into texmask, which the viewport and
// every other reader of the mask sample (slot_layer_mask_texture). Brushes and fills keep writing texpaint, the stack's
// one paint effect. A mask without a stack (mask_effects NULL) is the implicit stack of one paint effect: it reads
// texpaint directly, with no extra texture and no passes.
//
// Effects are packed MASK_EFFECT_STRIDE floats each in slot_layer_t.mask_effects, saved and undone as is.

// Packing

// Settings of each effect type: what the effect panels draw (native UI, tab_layers.c) and the defaults. Generators end
// with the shared noise block, params[MASK_EFFECT_NOISE_*].

#define MASK_NOISE_PARAMS                                                                                                   \
	{"Noise Scale", MASK_EFFECT_NOISE_SCALE, MASK_PARAM_SLIDER, 0.5, 32.0, 4.0, NULL},                                     \
	    {"Noise Amount", MASK_EFFECT_NOISE_AMOUNT, MASK_PARAM_SLIDER, 0.0, 1.0, 0.3, NULL},                                \
	    {"Noise Detail", MASK_EFFECT_NOISE_DETAIL, MASK_PARAM_SLIDER, 1.0, 6.0, 3.0, NULL},                                \
	{                                                                                                                       \
		"Seed", MASK_EFFECT_NOISE_SEED, MASK_PARAM_SEED, 0.0, 1000.0, 0.0, NULL                                             \
	}

static mask_effect_param_t mask_params_fill[]   = {{"Value", 0, MASK_PARAM_SLIDER, 0.0, 1.0, 1.0, NULL}};
static mask_effect_param_t mask_params_levels[] = {
    {"In Low", 0, MASK_PARAM_SLIDER, 0.0, 1.0, 0.0, NULL},  {"In High", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 1.0, NULL},
    {"Gamma", 2, MASK_PARAM_SLIDER, 0.1, 10.0, 1.0, NULL},  {"Out Low", 3, MASK_PARAM_SLIDER, 0.0, 1.0, 0.0, NULL},
    {"Out High", 4, MASK_PARAM_SLIDER, 0.0, 1.0, 1.0, NULL},
};
static mask_effect_param_t mask_params_edge_wear[] = {
    {"Amount", 0, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Sharpness", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    MASK_NOISE_PARAMS,
};
static mask_effect_param_t mask_params_dirt[] = {
    {"Amount", 0, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Sharpness", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"AO Weight", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    MASK_NOISE_PARAMS,
};
static mask_effect_param_t mask_params_position[] = {
    {"Axis", 0, MASK_PARAM_CHOICE, 0.0, 2.0, 1.0, "X,Y,Z"},
    {"Start", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.0, NULL},
    {"End", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 1.0, NULL},
    MASK_NOISE_PARAMS,
};
static mask_effect_param_t mask_params_directional[] = {
    {"Direction", 0, MASK_PARAM_CHOICE, 0.0, 5.0, 0.0, "+Y,-Y,+X,-X,+Z,-Z"},
    {"Spread", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Sharpness", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    MASK_NOISE_PARAMS,
};

static mask_effect_param_t mask_params_noise[] = {
    {"Pattern", 0, MASK_PARAM_CHOICE, 0.0, 3.0, 0.0, "Clouds,Cells,Scratches,Streaks"},
    {"Coverage", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Sharpness", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Scale", MASK_EFFECT_NOISE_SCALE, MASK_PARAM_SLIDER, 0.5, 64.0, 8.0, NULL},
    {"Distortion", MASK_EFFECT_NOISE_AMOUNT, MASK_PARAM_SLIDER, 0.0, 1.0, 0.0, NULL},
    {"Detail", MASK_EFFECT_NOISE_DETAIL, MASK_PARAM_SLIDER, 1.0, 6.0, 4.0, NULL},
    {"Seed", MASK_EFFECT_NOISE_SEED, MASK_PARAM_SEED, 0.0, 1000.0, 0.0, NULL},
};
static mask_effect_param_t mask_params_light[] = {
    {"Azimuth", 0, MASK_PARAM_SLIDER, 0.0, 360.0, 45.0, NULL},
    {"Elevation", 1, MASK_PARAM_SLIDER, 0.0, 90.0, 45.0, NULL},
    {"Softness", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"AO Weight", 3, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    MASK_NOISE_PARAMS,
};
static mask_effect_param_t mask_params_thickness[] = {
    {"Side", 0, MASK_PARAM_CHOICE, 0.0, 1.0, 0.0, "Thin,Thick"},
    {"Amount", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Sharpness", 2, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    MASK_NOISE_PARAMS,
};
static mask_effect_param_t mask_params_histogram[] = {
    {"Position", 0, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
    {"Contrast", 1, MASK_PARAM_SLIDER, 0.0, 1.0, 0.5, NULL},
};
static mask_effect_param_t mask_params_blur[] = {
    {"Radius", 0, MASK_PARAM_SLIDER, 1.0, 32.0, 4.0, NULL},
};
static mask_effect_param_t mask_params_anchor[] = {
    {"Layer", 0, MASK_PARAM_LAYER, -1.0, 1000000.0, -1.0, NULL},
    {"Channel", 1, MASK_PARAM_CHOICE, 0.0, 2.0, 0.0, "Mask,Height,Opacity"},
};

#define MASK_PARAMS_OF(a) *count = ARRAY_LENGTH(a); return a

// The settings of an effect type, NULL with count 0 when it has none (paint, invert)
mask_effect_param_t *mask_effect_params(i32 type, i32 *count) {
	switch (type) {
	case MASK_EFFECT_FILL:
		MASK_PARAMS_OF(mask_params_fill);
	case MASK_EFFECT_LEVELS:
		MASK_PARAMS_OF(mask_params_levels);
	case MASK_EFFECT_EDGE_WEAR:
		MASK_PARAMS_OF(mask_params_edge_wear);
	case MASK_EFFECT_DIRT:
		MASK_PARAMS_OF(mask_params_dirt);
	case MASK_EFFECT_POSITION:
		MASK_PARAMS_OF(mask_params_position);
	case MASK_EFFECT_DIRECTIONAL:
		MASK_PARAMS_OF(mask_params_directional);
	case MASK_EFFECT_NOISE:
		MASK_PARAMS_OF(mask_params_noise);
	case MASK_EFFECT_LIGHT:
		MASK_PARAMS_OF(mask_params_light);
	case MASK_EFFECT_THICKNESS:
		MASK_PARAMS_OF(mask_params_thickness);
	case MASK_EFFECT_HISTOGRAM:
		MASK_PARAMS_OF(mask_params_histogram);
	case MASK_EFFECT_BLUR:
		MASK_PARAMS_OF(mask_params_blur);
	case MASK_EFFECT_ANCHOR:
		MASK_PARAMS_OF(mask_params_anchor);
	}
	*count = 0;
	return NULL;
}

// The setting of type that writes params[index], NULL when none does
mask_effect_param_t *mask_effect_param_at(i32 type, i32 index) {
	i32                  count = 0;
	mask_effect_param_t *ps    = mask_effect_params(type, &count);
	for (i32 i = 0; i < count; ++i) {
		if (ps[i].index == index) {
			return &ps[i];
		}
	}
	return NULL;
}

// value clamped to the range of params[index] of type, whole for a choice or seed
f32 mask_effect_param_clamp(i32 type, i32 index, f32 value) {
	mask_effect_param_t *p = mask_effect_param_at(type, index);
	if (p == NULL) {
		return value;
	}
	f32 v = math_max(p->min, math_min(p->max, value));
	return p->kind == MASK_PARAM_SLIDER ? v : math_floor(v + 0.5); // Choices, seeds and layer ids are whole
}

// Painter's rule: a layer or group has at most one mask, anything more goes into that mask's effects. Masks of older
// projects stay as they are (they multiply).
bool slot_layer_takes_mask(slot_layer_t *l) {
	if (l == NULL || slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return false;
	}
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
	return masks == NULL || masks->length == 0;
}

char *mask_effect_name(i32 type) {
	switch (type) {
	case MASK_EFFECT_PAINT:
		return "Paint";
	case MASK_EFFECT_FILL:
		return "Fill";
	case MASK_EFFECT_LEVELS:
		return "Levels";
	case MASK_EFFECT_INVERT:
		return "Invert";
	case MASK_EFFECT_EDGE_WEAR:
		return "Edge Wear";
	case MASK_EFFECT_DIRT:
		return "Dirt";
	case MASK_EFFECT_POSITION:
		return "Position Gradient";
	case MASK_EFFECT_DIRECTIONAL:
		return "Directional";
	case MASK_EFFECT_ID_SELECTION:
		return "ID Selection";
	case MASK_EFFECT_NOISE:
		return "3D Noise";
	case MASK_EFFECT_LIGHT:
		return "Light";
	case MASK_EFFECT_THICKNESS:
		return "Thickness";
	case MASK_EFFECT_HISTOGRAM:
		return "Histogram Scan";
	case MASK_EFFECT_BLUR:
		return "Blur";
	case MASK_EFFECT_ANCHOR:
		return "Anchor";
	}
	return "Effect";
}

// English name of a mask effect blending, tr() it to show it
char *mask_blend_name(i32 blending) {
	static char *names[] = {"Normal", "Multiply", "Add", "Subtract", "Max", "Min", "Overlay"};
	_Static_assert(ARRAY_LENGTH(names) == MASK_BLEND_COUNT, "a name per mask_blend_t");
	return blending >= 0 && blending < MASK_BLEND_COUNT ? names[blending] : "";
}

// Filters transform the result below (their blending is ignored), the other effects are sources blended over it
bool mask_effect_is_filter(i32 type) {
	return type == MASK_EFFECT_LEVELS || type == MASK_EFFECT_INVERT || type == MASK_EFFECT_HISTOGRAM || type == MASK_EFFECT_BLUR;
}

// Generators are computed from the mesh (util_mask_generators.c)
bool mask_effect_is_generator(i32 type) {
	return (type >= MASK_EFFECT_EDGE_WEAR && type <= MASK_EFFECT_DIRECTIONAL) ||
	       (type >= MASK_EFFECT_NOISE && type <= MASK_EFFECT_THICKNESS);
}

mask_effect_t mask_effect_default(i32 type) {
	mask_effect_t e = {.type = type, .visible = true, .opacity = 1.0, .blending = MASK_BLEND_NORMAL};
	if (mask_effect_is_generator(type) || type == MASK_EFFECT_ID_SELECTION || type == MASK_EFFECT_ANCHOR) {
		e.blending = MASK_BLEND_MAX; // Stacked sources add up (wear here and dirt there), the first one looks the same
	}
	for (i32 i = 0; i < MASK_EFFECT_PARAMS; ++i) {
		e.params[i] = 0.0;
	}
	i32                  count = 0;
	mask_effect_param_t *ps    = mask_effect_params(type, &count);
	for (i32 i = 0; i < count; ++i) {
		e.params[ps[i].index] = ps[i].def;
	}
	return e;
}

i32 mask_stack_count(slot_layer_t *m) {
	return m->mask_effects == NULL ? 1 : (i32)(m->mask_effects->length / MASK_EFFECT_STRIDE);
}

mask_effect_t mask_stack_get(slot_layer_t *m, i32 i) {
	if (m->mask_effects == NULL) {
		return mask_effect_default(MASK_EFFECT_PAINT);
	}
	f32          *f = m->mask_effects->buffer + i * MASK_EFFECT_STRIDE;
	mask_effect_t e = {.type = (i32)f[0], .visible = f[1] > 0.5, .opacity = f[2], .blending = (i32)f[3]};
	for (i32 j = 0; j < MASK_EFFECT_PARAMS; ++j) {
		e.params[j] = f[4 + j];
	}
	return e;
}

static void mask_stack_pack(f32 *f, mask_effect_t e) {
	f[0] = (f32)e.type;
	f[1] = e.visible ? 1.0 : 0.0;
	f[2] = e.opacity;
	f[3] = (f32)e.blending;
	for (i32 j = 0; j < MASK_EFFECT_PARAMS; ++j) {
		f[4 + j] = e.params[j];
	}
}

// Turns the implicit stack into an explicit one of a single paint effect
void mask_stack_materialize(slot_layer_t *m) {
	if (m->mask_effects != NULL) {
		return;
	}
	m->mask_effects = f32_array_create(MASK_EFFECT_STRIDE);
	mask_stack_pack(m->mask_effects->buffer, mask_effect_default(MASK_EFFECT_PAINT));
}

void mask_stack_set(slot_layer_t *m, i32 i, mask_effect_t e) {
	mask_stack_materialize(m);
	mask_stack_pack(m->mask_effects->buffer + i * MASK_EFFECT_STRIDE, e);
	mask_stack_mark_dirty(m);
}

i32 mask_stack_paint_index(slot_layer_t *m) {
	i32 n = mask_stack_count(m);
	for (i32 i = 0; i < n; ++i) {
		if (mask_stack_get(m, i).type == MASK_EFFECT_PAINT) {
			return i;
		}
	}
	return -1;
}

// The stack reads as texpaint itself: no stack, or only a visible paint effect at full opacity
bool mask_stack_is_trivial(slot_layer_t *m) {
	if (m->mask_effects == NULL) {
		return true;
	}
	if (mask_stack_count(m) != 1) {
		return false;
	}
	mask_effect_t e = mask_stack_get(m, 0);
	return e.type == MASK_EFFECT_PAINT && e.visible && e.opacity >= 1.0 &&
	       (e.blending == MASK_BLEND_NORMAL || e.blending == MASK_BLEND_ADD || e.blending == MASK_BLEND_MAX);
}

f32_array_t *mask_stack_copy_effects(f32_array_t *effects) {
	return effects == NULL ? NULL : f32_array_create_from_array(effects);
}

// Inserts a new effect of type at index (-1: on top), returns its index. A second paint effect is refused (-1).
i32 mask_stack_add(slot_layer_t *m, i32 type, i32 index) {
	if (type == MASK_EFFECT_PAINT && mask_stack_paint_index(m) >= 0) {
		return -1;
	}
	mask_stack_materialize(m);
	i32 n = mask_stack_count(m);
	if (index < 0 || index > n) {
		index = n;
	}
	f32_array_t *a = f32_array_create((n + 1) * MASK_EFFECT_STRIDE);
	for (i32 i = 0, j = 0; i <= n; ++i) {
		f32 *dst = a->buffer + i * MASK_EFFECT_STRIDE;
		if (i == index) {
			mask_stack_pack(dst, mask_effect_default(type));
		}
		else {
			memcpy(dst, m->mask_effects->buffer + j * MASK_EFFECT_STRIDE, sizeof(f32) * MASK_EFFECT_STRIDE);
			j++;
		}
	}
	m->mask_effects = a;
	mask_stack_mark_dirty(m);
	return index;
}

// Removes effect i, the paint effect stays. Returns whether it was removed.
bool mask_stack_remove(slot_layer_t *m, i32 i) {
	i32 n = mask_stack_count(m);
	if (i < 0 || i >= n || mask_stack_get(m, i).type == MASK_EFFECT_PAINT) {
		return false;
	}
	f32_array_t *a = f32_array_create((n - 1) * MASK_EFFECT_STRIDE);
	for (i32 k = 0, j = 0; k < n; ++k) {
		if (k == i) {
			continue;
		}
		memcpy(a->buffer + j * MASK_EFFECT_STRIDE, m->mask_effects->buffer + k * MASK_EFFECT_STRIDE, sizeof(f32) * MASK_EFFECT_STRIDE);
		j++;
	}
	m->mask_effects = a;
	mask_stack_mark_dirty(m);
	return true;
}

// Moves effect from to position to (both indices into the current stack)
void mask_stack_move(slot_layer_t *m, i32 from, i32 to) {
	i32 n = mask_stack_count(m);
	if (from < 0 || from >= n || to < 0 || to >= n || from == to) {
		return;
	}
	f32 tmp[MASK_EFFECT_STRIDE];
	f32 *b = m->mask_effects->buffer;
	memcpy(tmp, b + from * MASK_EFFECT_STRIDE, sizeof(tmp));
	if (from < to) {
		memmove(b + from * MASK_EFFECT_STRIDE, b + (from + 1) * MASK_EFFECT_STRIDE, sizeof(f32) * MASK_EFFECT_STRIDE * (to - from));
	}
	else {
		memmove(b + (to + 1) * MASK_EFFECT_STRIDE, b + to * MASK_EFFECT_STRIDE, sizeof(f32) * MASK_EFFECT_STRIDE * (from - to));
	}
	memcpy(b + to * MASK_EFFECT_STRIDE, tmp, sizeof(tmp));
	mask_stack_mark_dirty(m);
}

void mask_stack_mark_dirty(slot_layer_t *m) {
	if (m == NULL || !slot_layer_is_mask(m)) {
		return;
	}
	m->mask_stack_dirty            = true;
	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 3;
	g_context->rtdirty             = 1;
}

// Evaluation (GPU passes)
// One pass per visible effect. The passes ping-pong between texmask and one scratch texture, starting where the last pass
// lands in texmask; the first reads black. Generators and the masks anchors read evaluate before through the scratch, a
// blur diffuses in its own textures before its pass.

static gpu_texture_t *mask_stack_scratch = NULL;

static gpu_texture_t *mask_stack_black() {
	render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
	return rt->_image;
}

static void mask_stack_alloc_texture(slot_layer_t *m) {
	if (m->texmask != NULL && (m->texmask->width != m->texpaint->width || m->texmask->height != m->texpaint->height)) {
		mask_stack_free_texture(m); // Resized
	}
	if (m->texmask != NULL) {
		return;
	}
	render_target_t *t = render_target_create();
	t->name            = string("texmask%s", m->ext);
	t->width           = m->texpaint->width;
	t->height          = m->texpaint->height;
	t->format          = string_copy("RGBA32");
	m->texmask         = render_path_create_render_target(t)->_image;
}

gpu_texture_t *mask_stack_get_scratch(i32 w, i32 h) {
	if (mask_stack_scratch != NULL && (mask_stack_scratch->width != w || mask_stack_scratch->height != h)) {
		gpu_delete_texture(mask_stack_scratch);
		mask_stack_scratch = NULL;
	}
	if (mask_stack_scratch == NULL) {
		mask_stack_scratch = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA32);
	}
	return mask_stack_scratch;
}

// The layers panel thumbnail shows the evaluated stack (util_layer.c, same unit)
static void mask_stack_draw_preview(slot_layer_t *m) {
	if (m->texpaint_preview != NULL) {
		util_layer_draw_preview(m, m->texpaint_preview);
		ui_dock_redraw(TAB_AREA_SIDEBAR0);
	}
}

// Anchors and ID Selection read outside the stack: another layer, the project's ID map. A mask evaluates again when
// what they read changes (mask_stack_inputs_key), a layer's revision counts the changes of its pixels.

// The pixels of l changed (a stroke, a fill, a clear, an evaluated stack), NULL: of every layer (undo, redo)
void mask_stack_layer_changed(slot_layer_t *l) {
	if (l != NULL) {
		l->revision++;
		return;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		g_project->_->layers->buffer[i]->revision++;
	}
}

// Layer of the project with runtime id, NULL when gone
slot_layer_t *mask_stack_layer_by_id(i32 id) {
	for (i32 i = 0; id >= 0 && i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->id == id) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

// The anchor reads the mask of l: a mask itself, or the Mask channel
static bool mask_stack_anchor_reads_mask(slot_layer_t *l, mask_effect_t *e) {
	return slot_layer_is_mask(l) || (i32)e->params[1] == 0;
}

// Mask an anchor reading the mask of l samples: l when it is one, else its first mask, NULL when it has none
static slot_layer_t *mask_stack_anchor_mask(slot_layer_t *l) {
	if (slot_layer_is_mask(l)) {
		return l;
	}
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
	return masks != NULL && masks->length > 0 ? masks->buffer[0] : NULL;
}

// The anchors of from read target, directly or through other masks
static bool mask_stack_anchor_reaches(slot_layer_t *from, slot_layer_t *target, i32 depth) {
	if (from == NULL || from->mask_effects == NULL || depth > 6) {
		return false;
	}
	i32 n = mask_stack_count(from);
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e = mask_stack_get(from, i);
		if (!e.visible || e.type != MASK_EFFECT_ANCHOR) {
			continue;
		}
		slot_layer_t *l = mask_stack_layer_by_id((i32)e.params[0]);
		if (l == NULL || !mask_stack_anchor_reads_mask(l, &e)) {
			continue;
		}
		slot_layer_t *r = mask_stack_anchor_mask(l);
		if (r == target || mask_stack_anchor_reaches(r, target, depth + 1)) {
			return true;
		}
	}
	return false;
}

static gpu_texture_t *mask_stack_id_map() {
	return project_get_image(project_id_map_asset());
}

// What the anchors and ID selections of m read. Inside a cycle (A anchors B, B anchors A) the other mask's revision is
// left out: each would evaluate again after the other forever.
static u64 mask_stack_inputs_key(slot_layer_t *m) {
	u64 h = 1469598103934665603ull;
	if (m->mask_effects == NULL) {
		return h;
	}
	i32 n = mask_stack_count(m);
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e    = mask_stack_get(m, i);
		u64           v[3] = {0, 0, 0};
		if (!e.visible) {
			continue;
		}
		if (e.type == MASK_EFFECT_ID_SELECTION) {
			v[0] = (u64)(uintptr_t)mask_stack_id_map();
		}
		else if (e.type == MASK_EFFECT_ANCHOR) {
			slot_layer_t *l = mask_stack_layer_by_id((i32)e.params[0]);
			v[0]            = (u64)(uintptr_t)l;
			if (l != NULL && mask_stack_anchor_reads_mask(l, &e)) {
				slot_layer_t *r = mask_stack_anchor_mask(l);
				v[1]            = (u64)(uintptr_t)r;
				v[2]            = r != NULL && r != m && !mask_stack_anchor_reaches(r, m, 0) ? r->revision : 0;
			}
			else if (l != NULL) {
				v[2] = l->revision;
			}
		}
		else {
			continue;
		}
		h = hash_fnv1a(h, v, sizeof(v));
	}
	return h;
}

// Masks the anchors of m read evaluate first, their passes use the same scratch. One evaluating already (a cycle) is
// read as it was.
static void mask_stack_anchors_prepare(slot_layer_t *m) {
	if (m->mask_effects == NULL) {
		return;
	}
	i32 n = mask_stack_count(m);
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e = mask_stack_get(m, i);
		if (!e.visible || e.type != MASK_EFFECT_ANCHOR) {
			continue;
		}
		slot_layer_t *l = mask_stack_layer_by_id((i32)e.params[0]);
		slot_layer_t *r = l != NULL && mask_stack_anchor_reads_mask(l, &e) ? mask_stack_anchor_mask(l) : NULL;
		if (r != NULL && r != m && !r->mask_evaluating && !mask_stack_is_trivial(r) && (r->mask_stack_dirty || r->texmask == NULL)) {
			mask_stack_evaluate(r);
		}
	}
}

// Texture anchor e of m samples, its channel set to the one the shader reads (0 red, else alpha)
static gpu_texture_t *mask_stack_anchor_source(slot_layer_t *m, mask_effect_t *e) {
	slot_layer_t *l = mask_stack_layer_by_id((i32)e->params[0]);
	if (l == NULL) { // None picked, or deleted
		e->params[1] = 0.0;
		return mask_stack_black();
	}
	if (mask_stack_anchor_reads_mask(l, e)) {
		e->params[1]    = 0.0;
		slot_layer_t *r = mask_stack_anchor_mask(l);
		if (r == NULL) { // No mask: all of the layer shows
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
			return rt->_image;
		}
		if (r == m) { // Itself, texmask is being written
			return mask_stack_black();
		}
		if (mask_stack_is_trivial(r)) {
			return r->texpaint;
		}
		return r->texmask != NULL ? r->texmask : mask_stack_black();
	}
	gpu_texture_t *t = (i32)e->params[1] == 1 ? l->texpaint_pack : l->texpaint; // Height in pack alpha, opacity in base alpha
	if (t == NULL) { // A group
		e->params[1] = 0.0;
		return mask_stack_black();
	}
	return t;
}

// Saving: the effects with each anchor's layer id written as the index of that layer in layers, -1 when it is not among
// them (a smart material keeps some layers). Layer ids are not saved, loading assigns new ones. The effects themselves
// when there is no anchor.
f32_array_t *mask_stack_anchors_to_index(f32_array_t *effects, slot_layer_t **layers, i32 count) {
	if (effects == NULL) {
		return NULL;
	}
	f32_array_t *out = effects;
	i32          n   = effects->length / MASK_EFFECT_STRIDE;
	for (i32 i = 0; i < n; ++i) {
		f32 *f = effects->buffer + i * MASK_EFFECT_STRIDE;
		if ((i32)f[0] != MASK_EFFECT_ANCHOR) {
			continue;
		}
		if (out == effects) {
			out = mask_stack_copy_effects(effects);
		}
		i32 index = -1;
		for (i32 k = 0; k < count; ++k) {
			if (layers[k]->id == (i32)f[4]) {
				index = k;
				break;
			}
		}
		out->buffer[i * MASK_EFFECT_STRIDE + 4] = (f32)index;
	}
	return out;
}

// Loading: each anchor's saved index into layers back to the id of that layer, once every layer exists
void mask_stack_anchors_from_index(f32_array_t *effects, slot_layer_t **layers, i32 count) {
	if (effects == NULL) {
		return;
	}
	i32 n = effects->length / MASK_EFFECT_STRIDE;
	for (i32 i = 0; i < n; ++i) {
		f32 *f = effects->buffer + i * MASK_EFFECT_STRIDE;
		if ((i32)f[0] == MASK_EFFECT_ANCHOR) {
			i32 index = (i32)f[4];
			f[4]      = index >= 0 && index < count ? (f32)layers[index]->id : -1.0;
		}
	}
}

// Blur: the result below downsampled, diffused along the surface across uv seams (util_uv_blur_seamless), sampled back
// by the blur's pass. The radius is in texels of a 1024 mask, the same on the model at any texture size. Larger radii
// blur at a lower resolution, a handful of diffusion steps each.

#define MASK_STACK_BLUR_MAX_RES 1024 // Seam maps are built on the cpu at the blur's size, two RGBA128 textures
#define MASK_STACK_BLUR_SIGMA   2.0  // Texels of the blur resolution, 10 diffusion steps

static gpu_texture_t *mask_stack_blur_textures[3]; // Downsampled (RGBA32), blurred and its scratch (RGBA64)
static i32            mask_stack_blur_timing = -1;

static gpu_texture_t *mask_stack_blur_texture(i32 k, i32 w, i32 h, gpu_texture_format_t format) {
	gpu_texture_t *t = mask_stack_blur_textures[k];
	if (t != NULL && (t->width != w || t->height != h)) {
		gpu_delete_texture(t);
		t = NULL;
	}
	if (t == NULL) {
		t = mask_stack_blur_textures[k] = gpu_create_render_target(w, h, format);
	}
	return t;
}

static void mask_stack_blur_free() {
	for (i32 k = 0; k < 3; ++k) {
		if (mask_stack_blur_textures[k] != NULL) {
			gpu_delete_texture(mask_stack_blur_textures[k]);
			mask_stack_blur_textures[k] = NULL;
		}
	}
}

typedef struct {
	i32 w;
	i32 h;
	i32 bw;
	i32 passes;
	f64 seam;
	f64 cpu;
} mask_stack_blur_timing_t;

static void mask_stack_blur_timing_done(void *data, f64 gpu_ms) {
	mask_stack_blur_timing_t *p = data;
	fprintf(stderr, "[mask_blur] %dx%d at %d, %d passes, seam map %.2f, cpu %.2f gpu %.2f ms\n", p->w, p->h, p->bw, p->passes, p->seam, p->cpu,
	        gpu_ms);
	free(p);
}

static void mask_stack_pass(gpu_texture_t *dst, gpu_texture_t *acc, gpu_texture_t *src, mask_effect_t *e, f32 src_w, f32 src_h);

// acc (w x h) blurred by blur effect e. No pass may be open.
static gpu_texture_t *mask_stack_blur(gpu_texture_t *acc, mask_effect_t *e, i32 w, i32 h) {
	if (mask_stack_blur_timing < 0) {
		mask_stack_blur_timing = getenv("IRON_PAINT_TIMING") != NULL ? 1 : 0;
	}
	f64 t0 = 0.0;
	if (mask_stack_blur_timing) {
		gpu_commit(NULL, NULL);
		t0 = iron_glsl_now_ms();
	}

	f32 sigma = e->params[0] * 0.5 * w / 1024.0; // Texels of the mask
	i32 s     = 1;                                // Downsampling, the shader averages up to 32 x 32 texels
	while (s < 32 && w / (s * 2) >= 16 && h / (s * 2) >= 16 && (sigma / s > MASK_STACK_BLUR_SIGMA || w / s > MASK_STACK_BLUR_MAX_RES)) {
		s *= 2;
	}
	i32 bw     = math_max(w / s, 1);
	i32 bh     = math_max(h / s, 1);
	f32 sb     = sigma / s;
	i32 passes = math_max(1, (i32)math_ceil(2.5 * sb * sb)); // A step spreads 0.4 texel^2 each way

	gpu_texture_t *src = acc;
	if (s > 1) {
		mask_effect_t d = {.type = -1, .opacity = 1.0};
		d.params[0]     = (f32)s;
		src             = mask_stack_blur_texture(0, bw, bh, GPU_TEXTURE_FORMAT_RGBA32);
		mask_stack_pass(src, acc, mask_stack_black(), &d, (f32)w, (f32)h);
	}
	f64 ts = mask_stack_blur_timing ? iron_glsl_now_ms() : 0.0;
	util_uv_cache_seam_map(bw, bh); // Built on the cpu once a size and mesh
	if (mask_stack_blur_timing) {
		ts = iron_glsl_now_ms() - ts;
	}
	gpu_texture_t *dst = mask_stack_blur_texture(1, bw, bh, GPU_TEXTURE_FORMAT_RGBA64);
	gpu_texture_t *tmp = mask_stack_blur_texture(2, bw, bh, GPU_TEXTURE_FORMAT_RGBA64);
	util_uv_blur_seamless(dst, tmp, src, passes);

	if (mask_stack_blur_timing) {
		mask_stack_blur_timing_t *p = malloc(sizeof(mask_stack_blur_timing_t));
		*p                          = (mask_stack_blur_timing_t){.w = w, .h = h, .bw = bw, .passes = passes, .seam = ts, .cpu = iron_glsl_now_ms() - t0};
		gpu_commit(&mask_stack_blur_timing_done, p);
	}
	return dst;
}

// Passes

// Source effect e of m reads, its size in *w x *h (ID map texels)
static gpu_texture_t *mask_stack_source(slot_layer_t *m, mask_effect_t *e, f32 *w, f32 *h) {
	*w = 1.0;
	*h = 1.0;
	if (e->type == MASK_EFFECT_PAINT) {
		return m->texpaint;
	}
	if (mask_effect_is_generator(e->type)) {
		return mask_gen_texture(m, *e);
	}
	if (e->type == MASK_EFFECT_ID_SELECTION) {
		gpu_texture_t *t = mask_stack_id_map();
		if (t != NULL) {
			*w = t->width;
			*h = t->height;
			return t;
		}
	}
	if (e->type == MASK_EFFECT_ANCHOR) {
		return mask_stack_anchor_source(m, e);
	}
	return mask_stack_black();
}

static void mask_stack_pass(gpu_texture_t *dst, gpu_texture_t *acc, gpu_texture_t *src, mask_effect_t *e, f32 src_w, f32 src_h) {
	_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_mask_stack);
	gpu_set_texture(pipes_texa_mask_stack, acc);
	gpu_set_texture(pipes_texsrc_mask_stack, src);
	gpu_set_int(pipes_type_mask_stack, e->type);
	gpu_set_int(pipes_blending_mask_stack, e->blending);
	gpu_set_float(pipes_opac_mask_stack, e->opacity);
	gpu_set_float4(pipes_params0_mask_stack, e->params[0], e->params[1], e->params[2], e->params[3]);
	gpu_set_float4(pipes_params1_mask_stack, e->params[4], e->params[5], e->params[6], e->params[7]);
	gpu_set_float4(pipes_params2_mask_stack, e->params[8], e->params[9], e->params[10], e->params[11]);
	gpu_set_float4(pipes_params3_mask_stack, e->params[12], e->params[13], e->params[14], e->params[15]);
	gpu_set_float2(pipes_src_size_mask_stack, src_w, src_h);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

// Texture every reader of the mask samples: the evaluated stack, or texpaint for an implicit stack.
// A dirty stack is evaluated first when no pass is open, inside a pass the last result is read.
gpu_texture_t *slot_layer_mask_texture(slot_layer_t *m) {
	if (mask_stack_is_trivial(m)) {
		return m->texpaint;
	}
	if ((m->mask_stack_dirty || m->texmask == NULL) && !gpu_in_use && !m->mask_evaluating) {
		mask_stack_evaluate(m);
	}
	return m->texmask != NULL ? m->texmask : m->texpaint;
}

// Evaluates the dirty stacks of every mask, called once per frame before the viewport renders
void mask_stack_evaluate_dirty() {
	bool any = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *m = g_project->_->layers->buffer[i];
		if (!slot_layer_is_mask(m)) {
			continue;
		}
		if (mask_stack_is_trivial(m)) {
			if (m->texmask != NULL) {
				mask_stack_evaluate(m); // Frees texmask
			}
			m->mask_stack_dirty = false;
			continue;
		}
		any = true;
		if (mask_gen_stale(m)) { // Mesh, transform or mesh maps changed
			m->mask_stack_dirty = true;
		}
		mask_stack_anchors_prepare(m);
		if (mask_stack_inputs_key(m) != m->mask_inputs_key) { // An anchored layer or the ID map changed
			m->mask_stack_dirty = true;
			g_context->ddirty   = math_max(g_context->ddirty, 2);
			g_context->rtdirty  = 1;
		}
		if (m->mask_stack_dirty || m->texmask == NULL || m->texmask->width != m->texpaint->width || m->texmask->height != m->texpaint->height) {
			mask_stack_evaluate(m);
		}
	}
	if (!any && mask_stack_scratch != NULL) { // Only implicit stacks left
		gpu_delete_texture(mask_stack_scratch);
		mask_stack_scratch = NULL;
		mask_stack_blur_free();
	}
}

// Evaluates the stack of m into texmask now
void mask_stack_evaluate(slot_layer_t *m) {
	if (m == NULL || !slot_layer_is_mask(m) || m->mask_evaluating) {
		return;
	}
	m->mask_stack_dirty = false;
	if (mask_stack_is_trivial(m)) {
		bool had = m->texmask != NULL;
		mask_stack_free_texture(m);
		if (had && !gpu_in_use) {
			mask_stack_draw_preview(m);
		}
		return;
	}
	mask_stack_alloc_texture(m);

	// Passes can not run inside an open one
	gpu_texture_t *current     = _draw_current;
	bool           in_use      = gpu_in_use;
	bool           render_path = in_use && _render_path_current_image != NULL;
	if (render_path) {
		render_path_end();
	}
	else if (in_use) {
		draw_end();
	}

	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}

	m->mask_evaluating = true;
	mask_stack_anchors_prepare(m);
	m->mask_inputs_key = mask_stack_inputs_key(m);

	i32 n       = mask_stack_count(m);
	i32 visible = 0;
	for (i32 i = 0; i < n; ++i) {
		if (mask_stack_get(m, i).visible) {
			visible++;
		}
	}

	mask_gen_update(m); // Generators render first, through the scratch the passes below use

	if (visible == 0) {
		_gpu_begin(m->texmask, NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		gpu_end();
	}
	else {
		i32            w       = m->texmask->width;
		i32            h       = m->texmask->height;
		gpu_texture_t *scratch = visible > 1 ? mask_stack_get_scratch(w, h) : NULL;
		gpu_texture_t *acc     = mask_stack_black();
		i32            k       = 0;
		for (i32 i = 0; i < n; ++i) {
			mask_effect_t e = mask_stack_get(m, i);
			if (!e.visible) {
				continue;
			}
			gpu_texture_t *dst   = (visible - 1 - k) % 2 == 0 ? m->texmask : scratch;
			f32            src_w = 1.0;
			f32            src_h = 1.0;
			gpu_texture_t *src   = e.type != MASK_EFFECT_BLUR ? mask_stack_source(m, &e, &src_w, &src_h)
			                       : k == 0                    ? mask_stack_black() // Black blurs to black
			                                                   : mask_stack_blur(acc, &e, w, h);
			mask_stack_pass(dst, acc, src, &e, src_w, src_h);
			acc = dst;
			k++;
		}
	}
	m->mask_evaluating = false;
	m->revision++; // Masks anchoring this one follow
	mask_stack_draw_preview(m);

	if (in_use && !render_path) {
		draw_begin(current, false, 0);
	}
}

// Frees texmask (unload, or the stack became trivial)
void mask_stack_free_texture(slot_layer_t *m) {
	mask_gen_free(m);
	if (m->texmask == NULL) {
		return;
	}
	gpu_delete_texture(m->texmask);
	map_delete(render_path_render_targets, string("texmask%s", m->ext));
	m->texmask = NULL;
}
