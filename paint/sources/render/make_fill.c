#include "../global.h"

// Fill layers in a shader: the GLSL that evaluates a fill's sources (util/util_fill.c) at a point of the surface. The
// viewport composites fill layers with it (make_mesh.c), the fill rasterizer renders them into textures with it
// (make_fill_raster_run) for what reads pixels.
//
// Per fill layer L, functions named fill<L>_*: one per channel (fill<L>_c<C>(t), a vec4 at a point t of the fill's
// own space: rgb for color channels, x for values), the three layer textures' worth of them (fill<L>_base: base color
// and opacity, fill<L>_nor: tangent normal and the emission flag, fill<L>_pack: occlusion, roughness, metallic,
// height), and the same projected from the surface (fill<L>_base_at(uv, wposition, wnormal), ...).
//
// Every number a fill has (colors, values, pattern settings, projection) is read from the fill parameters texture
// (texfill, util_fill.c: a row per layer, FILL_TEXELS texels), not from constants: a slider moves without a new shader,
// and the 512 bytes of constants a draw has stay free. A shader is made again only when the fill's shape changes:
// projection, the kind of a source, its pattern, image or material (fill_shape).

static char *str_fill_common = "\
vec4 fill_texel(int x, int y) { \
	return texelFetch(sampler2D(texfill, sampler_linear), ivec2(x, y), 0); \
} \
vec2 fill_xf(vec2 p, vec4 xf, vec4 ro) { \
	return vec2(p.x * ro.x - p.y * ro.y, p.x * ro.y + p.y * ro.x) * xf.xy + xf.zw; \
} \
vec3 fill_tri_weights(vec3 wn, float hardness) { \
	vec3 w = abs(wn); \
	w = w / max(max(w.x, w.y), max(w.z, 0.0001)); \
	w = pow(w, vec3(hardness, hardness, hardness)); \
	return w / (w.x + w.y + w.z); \
} \
";

// Texel of the parameters of channel c of a fill: value, value2, then the pattern's t, d, a, b (FILL_TEXELS_CHANNEL)
static i32 make_fill_texel(i32 c, i32 k) {
	return FILL_TEXELS_HEAD + c * FILL_TEXELS_CHANNEL + k;
}

static bool make_fill_is_color(i32 c) {
	return c == FILL_CHANNEL_BASE || c == FILL_CHANNEL_NORMAL;
}

char *make_fill_image_texture(i32 asset_id) {
	return string("fillimg%d", asset_id);
}

char *make_fill_tile_texture(u32 material_uid, i32 k) {
	return string("filltile%u_%d", material_uid, k);
}

// The tile texture and component a channel of a material reads (util_fill.c renders base / normal / pack tiles)
static void make_fill_material_slot(i32 c, i32 *k, char **comp) {
	switch (c) {
	case FILL_CHANNEL_BASE:
		*k = 0, *comp = "rgb";
		return;
	case FILL_CHANNEL_OPACITY:
		*k = 0, *comp = "a";
		return;
	case FILL_CHANNEL_NORMAL:
		*k = 1, *comp = "rgb";
		return;
	case FILL_CHANNEL_EMISSION:
		*k = 1, *comp = "a";
		return;
	case FILL_CHANNEL_OCCLUSION:
		*k = 2, *comp = "r";
		return;
	case FILL_CHANNEL_ROUGHNESS:
		*k = 2, *comp = "g";
		return;
	case FILL_CHANNEL_METALLIC:
		*k = 2, *comp = "b";
		return;
	default: // Height
		*k = 2, *comp = "a";
		return;
	}
}

// The pattern of a procedural source as a function of t, its height (0 recess .. 1 surface) after contrast
static void make_fill_pattern_fn(node_shader_t *shader, i32 id, i32 c, fill_source_t *s) {
	i32 pattern = s->pattern >= 0 && s->pattern < pattern_node_count() ? s->pattern : 0;
	pattern_lib_add(shader, pattern);
	char *fn = string("float fill%d_h%d(vec2 t) { \
	vec4 pt = fill_texel(%d, %d); \
	vec4 pd = fill_texel(%d, %d); \
	vec4 pa = fill_texel(%d, %d); \
	vec4 pb = fill_texel(%d, %d); \
	vec2 sc = pat_cells(pt, 1.0); \
	vec2 r = %s(pat_coord(t, pt, pd, sc, 1.0), sc, pa, pb, pt.w); \
	return clamp((r.x - 0.5) * pd.z + 0.5, 0.0, 1.0); \
}",
	                  id, c, make_fill_texel(c, 2), id, make_fill_texel(c, 3), id, make_fill_texel(c, 4), id, make_fill_texel(c, 5), id,
	                  pattern_def_fn(pattern));
	node_shader_add_function(shader, fn);
}

// fill<id>_c<c>(t): channel c of the fill at t, a vec4
static void make_fill_channel_fn(node_shader_t *shader, slot_layer_t *l, i32 c) {
	fill_source_t *s     = &l->fill->ch[c];
	i32            id    = l->id;
	bool           color = make_fill_is_color(c);
	char          *body  = NULL;
	switch (s->kind) {
	case FILL_SOURCE_TEXTURE: {
		asset_t *a = fill_source_asset(s);
		if (a == NULL) {
			body = string("return fill_texel(%d, %d);", make_fill_texel(c, 0), id); // The color it had, until an image is picked
			break;
		}
		char *tex = make_fill_image_texture(a->id);
		node_shader_add_texture(shader, tex, string("_fillimg%d", a->id));
		if (color) {
			body = string("return textureLod(sampler2D(%s, sampler_linear), t, 0.0);", tex);
		}
		else {
			char *ch[5] = {"s.r", "s.g", "s.b", "s.a", "dot(s.rgb, vec3(0.299, 0.587, 0.114))"};
			i32   k     = s->image_channel >= 0 && s->image_channel < 5 ? s->image_channel : 4;
			body        = string("vec4 s = textureLod(sampler2D(%s, sampler_linear), t, 0.0); return vec4(%s, 0.0, 0.0, 1.0);", tex, ch[k]);
		}
		break;
	}
	case FILL_SOURCE_PROCEDURAL:
		make_fill_pattern_fn(shader, id, c, s);
		if (c == FILL_CHANNEL_NORMAL) { // A bump: the pattern's slope, value.x its strength
			body = string("vec4 pt = fill_texel(%d, %d); \
	vec2 sc = pat_cells(pt, 1.0); \
	float e = 0.02 / max(sc.x, sc.y); \
	float h0 = fill%d_h%d(t); \
	float hx = fill%d_h%d(t + vec2(e, 0.0)); \
	float hy = fill%d_h%d(t + vec2(0.0, e)); \
	float k = fill_texel(%d, %d).x * 0.02 / e; \
	vec3 nn = normalize(vec3((h0 - hx) * k / sc.x, (h0 - hy) * k / sc.y, 1.0)); \
	return vec4(nn * 0.5 + vec3(0.5, 0.5, 0.5), 1.0);",
			              make_fill_texel(c, 2), id, id, c, id, c, id, c, make_fill_texel(c, 0), id);
		}
		else {
			body = string("return mix(fill_texel(%d, %d), fill_texel(%d, %d), fill%d_h%d(t));", make_fill_texel(c, 0), id, make_fill_texel(c, 1), id, id, c);
		}
		break;
	case FILL_SOURCE_MATERIAL: {
		slot_material_t *m = fill_source_material(s);
		if (m == NULL) {
			body = string("return fill_texel(%d, %d);", make_fill_texel(c, 0), id);
			break;
		}
		i32   k;
		char *comp;
		make_fill_material_slot(c, &k, &comp);
		char *tex = make_fill_tile_texture(m->uid, k);
		node_shader_add_texture(shader, tex, string("_filltile%u_%d", m->uid, k));
		if (color) {
			body = string("vec4 s = textureLod(sampler2D(%s, sampler_linear), t, 0.0); return vec4(s.rgb, 1.0);", tex);
		}
		else {
			body = string("vec4 s = textureLod(sampler2D(%s, sampler_linear), t, 0.0); return vec4(s.%s, 0.0, 0.0, 1.0);", tex, comp);
		}
		break;
	}
	default: // Color
		body = string("return fill_texel(%d, %d);", make_fill_texel(c, 0), id);
		break;
	}
	node_shader_add_function(shader, string("vec4 fill%d_c%d(vec2 t) { %s }", id, c, body));
}

// The three textures' worth of a fill at t, and the same projected from the surface
static void make_fill_layer_fns(node_shader_t *shader, slot_layer_t *l) {
	i32 id = l->id;
	node_shader_add_function(shader, str_fill_common);
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		make_fill_channel_fn(shader, l, c);
	}
	// The emission flag as the layers store it (matid, make_mesh.c): 1 / 255 emits
	node_shader_add_function(shader, string("vec4 fill%d_base(vec2 t) { return vec4(fill%d_c%d(t).rgb, fill%d_c%d(t).x); }", id, id, FILL_CHANNEL_BASE, id,
	                                        FILL_CHANNEL_OPACITY));
	node_shader_add_function(shader, string("vec4 fill%d_nor(vec2 t) { return vec4(fill%d_c%d(t).rgb, fill%d_c%d(t).x > 0.0 ? 1.0 / 255.0 : 0.0); }", id, id,
	                                        FILL_CHANNEL_NORMAL, id, FILL_CHANNEL_EMISSION));
	node_shader_add_function(shader, string("vec4 fill%d_pack(vec2 t) { return vec4(fill%d_c%d(t).x, fill%d_c%d(t).x, fill%d_c%d(t).x, fill%d_c%d(t).x); }", id,
	                                        id, FILL_CHANNEL_OCCLUSION, id, FILL_CHANNEL_ROUGHNESS, id, FILL_CHANNEL_METALLIC, id, FILL_CHANNEL_HEIGHT));

	char *outs[3] = {"base", "nor", "pack"};
	for (i32 k = 0; k < 3; ++k) {
		char *o    = outs[k];
		char *head = string("vec4 fill%d_%s_at(vec2 uv, vec3 wp, vec3 wn) { vec4 xf = fill_texel(0, %d); vec4 ro = fill_texel(1, %d);", id, o, id, id);
		char *body;
		switch (l->fill->projection) {
		case FILL_PROJECTION_TRIPLANAR:
			body = string("vec3 w = fill_tri_weights(wn, ro.z); vec3 p = wp * 0.5; \
	return fill%d_%s(fill_xf(p.yz, xf, ro)) * w.x + fill%d_%s(fill_xf(p.xz, xf, ro)) * w.y + fill%d_%s(fill_xf(p.xy, xf, ro)) * w.z; }",
			              id, o, id, o, id, o);
			break;
		case FILL_PROJECTION_PLANAR:
		case FILL_PROJECTION_DECAL: {
			// The decal box (decal_mat) is the unit cube around its origin, its xy face maps to 0..1 as the gizmo shows. A
			// decal covers the faces turned to its +z side, inside the box.
			char *clip = l->fill->projection == FILL_PROJECTION_DECAL && k == 0
			                 ? string("vec3 axis = fill_texel(4, %d).xyz; \
	float facing = dot(wn, axis / max(length(axis), 0.000001)); \
	if (abs(q.x) > 0.5 || abs(q.y) > 0.5 || abs(q.z) > 0.5 || facing < 0.1) { r.a = 0.0; }",
			                          id)
			                 : "";
			body = string("vec4 wp4 = vec4(wp, 1.0); \
	vec3 q = vec3(dot(fill_texel(2, %d), wp4), dot(fill_texel(3, %d), wp4), dot(fill_texel(4, %d), wp4)); \
	vec4 r = fill%d_%s(fill_xf(vec2(q.x + 0.5, 0.5 - q.y), xf, ro)); \
	%s \
	return r; }",
			              id, id, id, id, o, clip);
			break;
		}
		default: // UV
			body = string("return fill%d_%s(fill_xf(uv, xf, ro)); }", id, o);
			break;
		}
		node_shader_add_function(shader, string("%s %s", head, body));
	}
}

// Declares what the fill of l reads (the parameters texture, its images and material tiles) and its functions
void make_fill_declare(node_shader_t *shader, slot_layer_t *l) {
	node_shader_add_texture(shader, "texfill", "_texfill");
	make_fill_layer_fns(shader, l);
}

// A channel of a fill that adds nothing: a color source of 0 (emission, height)
static bool make_fill_zero(fill_source_t *s) {
	return s->kind == FILL_SOURCE_COLOR && s->value[0] == 0.0;
}

// Visible fill layers that emit or have height switch those on for the viewport (make_material_emis_used, _height_used)
void make_fill_uses(bool *emis, bool *height) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill == NULL || !slot_layer_is_layer(l) || !slot_layer_is_visible(l)) {
			continue;
		}
		if (l->paint_emis && !make_fill_zero(&l->fill->ch[FILL_CHANNEL_EMISSION])) {
			*emis = true;
		}
		if (l->paint_height && !make_fill_zero(&l->fill->ch[FILL_CHANNEL_HEIGHT])) {
			*height = true;
		}
	}
}

// Textures the fill of l takes in a shader: its images and material tiles (the parameters texture counted apart)
i32 make_fill_texture_count(slot_layer_t *l) {
	i32 n       = 0;
	u32 seen[FILL_CHANNEL_COUNT * 2];
	i32 nseen   = 0;
	for (i32 c = 0; l->fill != NULL && c < FILL_CHANNEL_COUNT; ++c) {
		fill_source_t *s   = &l->fill->ch[c];
		u32            key = 0;
		if (s->kind == FILL_SOURCE_TEXTURE && fill_source_asset(s) != NULL) {
			key = 0x80000000u | (u32)s->image;
		}
		else if (s->kind == FILL_SOURCE_MATERIAL && fill_source_material(s) != NULL) {
			i32   k;
			char *comp;
			make_fill_material_slot(c, &k, &comp);
			key = (s->material << 2) | (u32)k;
		}
		if (key == 0) {
			continue;
		}
		bool dup = false;
		for (i32 i = 0; i < nseen; ++i) {
			dup = dup || seen[i] == key;
		}
		if (!dup) {
			seen[nseen++] = key;
			n++;
		}
	}
	return n;
}

// The fill of l renders in uv space: into a layer's base, normal and pack (or a mask) at the uv rectangle _fill_uv_tile
// (u0, v0, 1 / width, 1 / height of the part rendered), what the viewport shows of it. Drawn with the paint objects.
node_shader_context_t *make_fill_raster_run(material_t *data, slot_layer_t *l, char *format) {
	bool mask = slot_layer_is_mask(l);
	shader_context_t *props =
	    ALLOC_INIT(shader_context_t, {.name              = "fill",
	                                  .depth_write       = false,
	                                  .compare_mode      = "always",
	                                  .cull_mode         = "none",
	                                  .vertex_elements   = node_shader_mesh_elements(),
	                                  .color_attachments = mask ? any_array_create_from_raw((void *[]){format}, 1)
	                                                            : any_array_create_from_raw((void *[]){format, format, format}, 3)});
	node_shader_context_t *con = node_shader_context_create(data, props);
	if (g_context->paint_object != NULL && mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL) {
		node_shader_context_add_elem(con, "tex1", "short2norm");
	}
	node_shader_t *shader = node_shader_context_make_shader(con);
	char          *tuv    = l->uv_map == 1 && node_shader_context_is_elem(con, "tex1") ? "input.tex1" : "input.tex";
	node_shader_add_constant(shader, "vec4 fill_uv_tile", "_fill_uv_tile");
	if (util_mesh_udim_layer(l)) { // Shared layers of udim tiles live in atlas space
		node_shader_add_constant(shader, "vec3 atlas_transform", "_atlas_transform");
		node_shader_write_vert(shader, string_tmp("vec2 layer_uv = %s * constants.atlas_transform.z + constants.atlas_transform.xy;", tuv));
	}
	else {
		node_shader_write_vert(shader, string_tmp("vec2 layer_uv = %s;", tuv));
	}
	node_shader_write_vert(shader, "vec2 tile_uv = (layer_uv - constants.fill_uv_tile.xy) * constants.fill_uv_tile.zw;");
	node_shader_write_vert(shader, "output.pos = vec4(tile_uv.x * 2.0 - 1.0, (1.0 - tile_uv.y) * 2.0 - 1.0, 0.0, 1.0);");
	node_shader_add_out(shader, "vec2 tex_coord");
	node_shader_write_vert(shader, "output.tex_coord = layer_uv;");
	node_shader_write_attrib_frag(shader, "vec2 tex_coord = input.tex_coord;");
	shader->frag_wposition = true;
	shader->frag_n         = true;

	make_fill_declare(shader, l);
	i32 id = l->id;
	if (mask) {
		shader->frag_out = "vec4";
		node_shader_write_frag(shader,
		                       string_tmp("vec3 fill_mask = fill%d_base_at(tex_coord, input.wposition, n).rgb;", id));
		node_shader_write_frag(shader, "float fill_gray = dot(fill_mask, vec3(0.299, 0.587, 0.114));");
		node_shader_write_frag(shader, "output = vec4(fill_gray, fill_gray, fill_gray, 1.0);");
	}
	else {
		shader->frag_out = "vec4[3]";
		node_shader_write_frag(shader, string_tmp("output[0] = fill%d_base_at(tex_coord, input.wposition, n);", id));
		node_shader_write_frag(shader, string_tmp("output[1] = fill%d_nor_at(tex_coord, input.wposition, n);", id));
		node_shader_write_frag(shader, string_tmp("output[2] = fill%d_pack_at(tex_coord, input.wposition, n);", id));
	}
	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}

// A material's outputs over the 0..1 square, tileable as its graph is: base color and opacity, tangent normal and
// emission, occlusion, roughness, metallic and height into three targets of format. Drawn with the screen aligned
// triangle (util_render_screen_aligned_full_vb).
node_shader_context_t *make_fill_tile_run(material_t *data, slot_material_t *m, char *format) {
	shader_context_t *props = ALLOC_INIT(shader_context_t, {.name            = "filltile",
	                                                        .depth_write     = false,
	                                                        .compare_mode    = "always",
	                                                        .cull_mode       = "none",
	                                                        .vertex_elements = any_array_create_from_raw(
	                                                            (void *[]){
	                                                                ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
	                                                                ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
	                                                                ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
	                                                                ALLOC_INIT(vertex_element_t, {.name = "col", .data = "short4norm"}),
	                                                            },
	                                                            4),
	                                                        .color_attachments = any_array_create_from_raw((void *[]){format, format, format}, 3)});
	node_shader_context_t *con = node_shader_context_create(data, props);
	con->allow_vcols           = true;
	node_shader_t *shader      = node_shader_context_make_shader(con);
	node_shader_write_attrib_vert(shader, "output.pos = vec4(input.pos.xy * 3.0, 0.0, 1.0);");
	node_shader_add_out(shader, "vec2 tex_coord");
	node_shader_write_attrib_vert(shader, "output.tex_coord = output.pos.xy * vec2(0.5, 0.5) + vec2(0.5, 0.5);");
	node_shader_write_attrib_vert(shader, "output.tex_coord.y = 1.0 - output.tex_coord.y;");
	node_shader_write_attrib_frag(shader, "vec2 tex_coord = input.tex_coord;");
	shader->frag_out = "vec4[3]";

	parser_material_parse_emission     = true;
	parser_material_parse_subsurface   = false;
	parser_material_parse_height       = true;
	parser_material_triplanar          = false;
	parser_material_sample_keep_aspect = false;
	parser_material_sample_uv_scale    = "1.0";
	shader_out_t *sout                 = parser_material_parse(m->canvas, con, shader);
	parser_material_parse_emission     = false;
	parser_material_parse_height       = false;
	node_shader_write_frag(shader, string_tmp("vec3 basecol = %s;", sout->out_basecol));
	node_shader_write_frag(shader, string_tmp("float roughness = %s;", sout->out_roughness));
	node_shader_write_frag(shader, string_tmp("float metallic = %s;", sout->out_metallic));
	node_shader_write_frag(shader, string_tmp("float occlusion = %s;", sout->out_occlusion));
	node_shader_write_frag(shader, string_tmp("vec3 nortan = %s;", parser_material_out_normaltan));
	node_shader_write_frag(shader, string_tmp("float height = %s;", sout->out_height));
	node_shader_write_frag(shader, string_tmp("float mat_opacity = %s;", sout->out_opacity));
	node_shader_write_frag(shader, string_tmp("float emis = %s;", sout->out_emission));
	node_shader_write_frag(shader, "output[0] = vec4(basecol, mat_opacity);");
	node_shader_write_frag(shader, "output[1] = vec4(nortan, emis);");
	node_shader_write_frag(shader, "output[2] = vec4(occlusion, roughness, metallic, height);");
	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}
