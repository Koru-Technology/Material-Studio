
#include "../global.h"

char *str_sh_irradiance = "\
vec3 sh_irradiance(vec3 nor) { \
	float c1 = 0.429043; \
	float c2 = 0.511664; \
	float c3 = 0.743125; \
	float c4 = 0.886227; \
	float c5 = 0.247708; \
	vec3 cl00 = vec3(constants.shirr0.x, constants.shirr0.y, constants.shirr0.z); \
	vec3 cl1m1 = vec3(constants.shirr0.w, constants.shirr1.x, constants.shirr1.y); \
	vec3 cl10 = vec3(constants.shirr1.z, constants.shirr1.w, constants.shirr2.x); \
	vec3 cl11 = vec3(constants.shirr2.y, constants.shirr2.z, constants.shirr2.w); \
	vec3 cl2m2 = vec3(constants.shirr3.x, constants.shirr3.y, constants.shirr3.z); \
	vec3 cl2m1 = vec3(constants.shirr3.w, constants.shirr4.x, constants.shirr4.y); \
	vec3 cl20 = vec3(constants.shirr4.z, constants.shirr4.w, constants.shirr5.x); \
	vec3 cl21 = vec3(constants.shirr5.y, constants.shirr5.z, constants.shirr5.w); \
	vec3 cl22 = vec3(constants.shirr6.x, constants.shirr6.y, constants.shirr6.z); \
	return ( \
		cl22 * c1 * (nor.x * nor.x - nor.y * nor.y) + \
		cl20 * c3 * nor.z * nor.z + \
		cl00 * c4 - \
		cl20 * c5 + \
		cl2m2 * 2.0 * c1 * nor.x * nor.y + \
		cl21  * 2.0 * c1 * nor.x * nor.z + \
		cl2m1 * 2.0 * c1 * nor.y * nor.z + \
		cl11  * 2.0 * c2 * nor.x + \
		cl1m1 * 2.0 * c2 * nor.y + \
		cl10  * 2.0 * c2 * nor.z \
	); \
} \
";

char *str_envmap_equirect = "\
vec2 envmap_equirect(vec3 normal, float angle) { \
	float PI = 3.1415926535; \
	float PI2 = PI * 2.0; \
	float phi = acos(normal.z); \
	float theta = atan(-normal.y, normal.x) + PI + angle; \
	return vec2(theta / PI2, phi / PI); \
} \
";

char *str_envmap_sample = "\
vec3 envmap_sample(float lod, vec2 coord) { \
	if (lod == 0.0) { \
		return textureLod(sampler2D(senvmap_radiance, sampler_linear), coord, 0.0).rgb; \
	} \
	if (lod == 1.0) { \
		return textureLod(sampler2D(senvmap_radiance0, sampler_linear), coord, 0.0).rgb; \
	} \
	if (lod == 2.0) { \
		return textureLod(sampler2D(senvmap_radiance1, sampler_linear), coord, 0.0).rgb; \
	} \
	if (lod == 3.0) { \
		return textureLod(sampler2D(senvmap_radiance2, sampler_linear), coord, 0.0).rgb; \
	} \
	if (lod == 4.0) { \
		return textureLod(sampler2D(senvmap_radiance3, sampler_linear), coord, 0.0).rgb; \
	} \
	return textureLod(sampler2D(senvmap_radiance4, sampler_linear), coord, 0.0).rgb; \
} \
";

// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
char *str_env_brdf_approx = "\
vec3 env_brdf_approx(vec3 specular, float roughness, float dotnv) { \
	vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022); \
	vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04); \
	vec4 r = c0 * roughness + c1; \
	float a004 = min(r.x * r.x, exp((-9.28 * dotnv) * log(2.0))) * r.x + r.y; \
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw; \
	return specular * ab.x + ab.y; \
} \
";

char *make_mesh_context_id(i32 layer_pass) {
	static char *ids[256] = {0};
	if (layer_pass <= 0) {
		return "mesh";
	}
	if (layer_pass >= 256) {
		return string("mesh%d", layer_pass);
	}
	if (ids[layer_pass] == NULL) {
		ids[layer_pass] = string("mesh%d", layer_pass);
	}
	return ids[layer_pass];
}

// A visible mask of the layer has a Height Blend effect: its edge follows the height, the normals sample it around each
// texel
static bool make_mesh_layer_height_blended(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		slot_layer_t *m = masks->buffer[i];
		if (slot_layer_is_visible(m) && mask_stack_has_height_blend(m)) {
			return true;
		}
	}
	return false;
}

static bool make_mesh_layer_has_visible_mask(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		if (slot_layer_is_visible(masks->buffer[i])) {
			return true;
		}
	}
	return false;
}

// The pack (occlusion, roughness, metallic, height) of layer l at coord into texpaint_pack_sample: its texture, or its
// fill evaluated
static void make_mesh_write_pack(node_shader_t *shader, slot_layer_t *l, char *coord) {
	if (l->fill != NULL) {
		node_shader_write_frag(shader, string_tmp("texpaint_pack_sample = fill%d_pack_at(%s, input.wposition, n);", l->id, coord));
		return;
	}
	node_shader_add_texture(shader, string_tmp("texpaint_pack%s", i32_to_string(l->id)), NULL);
	node_shader_write_frag(shader, string_tmp("texpaint_pack_sample = textureLod(sampler2D(texpaint_pack%s, sampler_linear), %s, 0.0);", i32_to_string(l->id), coord));
}

// World position of height neighbour k (-u, +u, -v, +v by a texel, tex_step) for fills
static char *make_mesh_fill_neighbour_wp(i32 k) {
	char *wp[4] = {"input.wposition - fill_dpdu * tex_step", "input.wposition + fill_dpdu * tex_step", "input.wposition - fill_dpdv * tex_step",
	               "input.wposition + fill_dpdv * tex_step"};
	return wp[k];
}

// Opacity of a layer or mask as a shader constant, dragging the opacity slider then needs no new shader
static char *make_mesh_opacity(node_shader_t *shader, slot_layer_t *l) {
	node_shader_add_constant(shader, string("float layer_opac%d", l->id), string("_layer_opacity%d", l->id));
	return string("constants.layer_opac%d", l->id);
}

// Multiplies the masks of the layer at coord into var: their evaluated stacks (a Height Blend effect is in there)
static void make_mesh_write_masks(node_shader_t *shader, slot_layer_t *l, char *var, char *coord) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		slot_layer_t *m = masks->buffer[i];
		if (!slot_layer_is_visible(m)) {
			continue;
		}
		char *mid  = string_copy(i32_to_string(m->id));
		char *opac = make_mesh_opacity(shader, m);
		node_shader_add_texture(shader, string_tmp("texmask%s", mid), NULL); // Evaluated stack or texpaint, see render_path_paint_bind_layers
		node_shader_write_frag(shader, string_tmp("%s *= mix(1.0, textureLod(sampler2D(texmask%s, sampler_linear), %s, 0.0).r, float(%s));", var, mid, coord, opac));
	}
}

// The height raise of l (slot_layer_t.height_raise, a shader constant: dragging it needs no new shader): its coverage times
// the raise, at the texel and at each height neighbour with that neighbour's own coverage, so the edges of its strokes slope
static void make_mesh_write_raise(node_shader_t *shader, slot_layer_t *l, char *coord) {
	node_shader_add_constant(shader, string("float layer_raise%d", l->id), string("_layer_raise%d", l->id));
	char *raise = string("constants.layer_raise%d", l->id);
	char *opac  = make_mesh_opacity(shader, l);
	bool  masks = make_mesh_layer_has_visible_mask(l);
	node_shader_write_frag(shader, string_tmp("height += %s * texpaint_opac;", raise));
	node_shader_write_frag(shader, "{");
	node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
	node_shader_write_frag(shader, "float tex_step = 1.0 / constants.texpaint_size.x;");
	char *offsets[] = {"vec2(%s.x - tex_step, %s.y)", "vec2(%s.x + tex_step, %s.y)", "vec2(%s.x, %s.y - tex_step)", "vec2(%s.x, %s.y + tex_step)"};
	for (i32 k = 0; k < 4; ++k) {
		node_shader_write_frag(shader, "{");
		char *nco_fmt = string("vec2 nco = %s;", offsets[k]);
		node_shader_write_frag(shader, string_tmp(nco_fmt, coord, coord));
		if (l->fill != NULL) {
			node_shader_write_frag(shader, string_tmp("float ncov = fill%d_base_at(nco, %s, n).a;", l->id, make_mesh_fill_neighbour_wp(k)));
		}
		else {
			node_shader_add_texture(shader, string_tmp("texpaint%s", i32_to_string(l->id)), NULL);
			node_shader_write_frag(shader, string_tmp("float ncov = textureLod(sampler2D(texpaint%s, sampler_linear), nco, 0.0).a;", i32_to_string(l->id)));
		}
		if (masks) {
			node_shader_write_frag(shader, "float nmask = 1.0;");
			make_mesh_write_masks(shader, l, "nmask", "nco");
			node_shader_write_frag(shader, "ncov *= clamp(nmask, 0.0, 1.0);");
		}
		node_shader_write_frag(shader, string_tmp("height%d += %s * ncov * %s;", k, raise, opac));
		node_shader_write_frag(shader, "}");
	}
	node_shader_write_frag(shader, "}");
}

node_shader_context_t *make_mesh_run(material_t *data, i32 layer_pass) {
	bool                   bake       = make_mesh_bake;
	bool                   cached     = !bake && layer_pass == 0 && layer_cache_active();
	char                  *context_id = bake ? "layer_cache" : make_mesh_context_id(layer_pass);
	shader_context_t      *props      = ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                  .depth_write     = layer_pass == 0 ? true : false,
	                                                                  .compare_mode    = layer_pass == 0 ? "less" : "equal",
	                                                                  .cull_mode       = (g_context->cull_backfaces || layer_pass > 0) ? "clockwise" : "none",
	                                                                  .vertex_elements = node_shader_mesh_elements(),
	                                                                  .color_attachments = any_array_create_from_raw(
                                                                (void *[]){
                                                                    "RGBA64",
                                                                    "RGBA64",
                                                                    "RGBA64",
                                                                },
                                                                render_path_base_gbuffer_uv() ? 3 : 2),
	                                                                  .depth_attachment = "D32"});
	if (bake) {
		props->depth_write       = false;
		props->compare_mode      = "always";
		props->cull_mode         = "none";
		props->color_attachments = any_array_create_from_raw((void *[]){"RGBA64", "RGBA64", "RGBA64", "RGBA64"}, 4);
		props->depth_attachment  = NULL;
	}
	node_shader_context_t *con_mesh   = node_shader_context_create(data, props);

	if (mesh_data_get_vertex_array(g_context->paint_object->data, "col") != NULL) {
		node_shader_context_add_elem(con_mesh, "col", "short4norm");
	}

	if (mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL) {
		node_shader_context_add_elem(con_mesh, "tex1", "short2norm");
	}

	node_shader_t *shader = node_shader_context_make_shader(con_mesh);

	node_shader_add_out(shader, "vec2 tex_coord");
	shader->frag_wposition = true;

	i32 texture_count = 0;

	if (bake) { // A texel of the layers for each fragment, the camera plays no part
		node_shader_write_vert(shader, "output.pos = vec4(input.tex.x * 2.0 - 1.0, (1.0 - input.tex.y) * 2.0 - 1.0, 0.0, 1.0);");
	}
	else {
		shader->frag_wvpposition = true;
		node_shader_add_constant(shader, "mat4 VP", "_view_proj_matrix");
		node_shader_write_vert(shader, "output.pos = constants.VP * vec4(output.wposition.xyz, 1.0);");
	}
	node_shader_write_vert(shader, "output.tex_coord = input.tex;");
	node_shader_write_attrib_frag(shader, "vec2 tex_coord = input.tex_coord;");

	if (mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL) {
		node_shader_add_out(shader, "vec2 tex_coord1");
		node_shader_write_vert(shader, "output.tex_coord1 = input.tex1;");
		node_shader_write_attrib_frag(shader, "vec2 tex_coord1 = input.tex_coord1;");
	}

	// Shared layers of udim tiles live in atlas space
	if (util_mesh_udim_active()) {
		node_shader_add_out(shader, "vec2 tex_coord_atlas");
		node_shader_add_constant(shader, "vec3 atlas_transform", "_atlas_transform");
		node_shader_write_vert(shader, "output.tex_coord_atlas = input.tex * constants.atlas_transform.z + constants.atlas_transform.xy;");
		node_shader_write_attrib_frag(shader, "vec2 tex_coord_atlas = input.tex_coord_atlas;");
	}
	char *tex_coord_layer = util_mesh_udim_layer(g_context->layer) ? "tex_coord_atlas" : "tex_coord";

	shader->frag_out = bake ? "vec4[4]" : render_path_base_gbuffer_uv() ? "vec4[3]" : "vec4[2]";
	shader->frag_n   = true;
	node_shader_add_function(shader, str_pack_float_int16);

	if (!bake && g_context->picker_hide_other_materials && g_context->tool != TOOL_TYPE_PICKER) {
		make_discard_material_id(shader, tex_coord_layer);
	}

	if (g_context->tool == TOOL_TYPE_COLORID && !bake) {
		// ID mask picking: the ID map unlit, the parts not picked are dimmed once any is picked
		texture_count += 2;
		node_shader_add_texture(shader, "texcolorid", "_texcolorid");
		node_shader_add_texture(shader, "texcoloridsel", "_texcolorid_sel"); // 16x1 picked colors, alpha 0 for free slots
		node_shader_write_frag(shader, "output[0] = vec4(n.xy, 1.0, pack_f32_i16(0.0, uint(0)));");
		node_shader_write_frag(shader, string_tmp("vec3 idraw = textureLod(sampler2D(texcolorid, sampler_linear), %s, 0.0).rgb;", tex_coord_layer));
		node_shader_write_frag(shader, "float idany = 0.0;");
		node_shader_write_frag(shader, "float idhit = 0.0;");
		node_shader_write_frag(shader, "for (int idi = 0; idi < 16; idi += 1) {");
		node_shader_write_frag(shader, "	vec4 idsel = texelFetch(sampler2D(texcoloridsel, sampler_linear), ivec2(uint(idi), uint(0)), 0);");
		node_shader_write_frag(shader, "	if (idsel.a > 0.5) {");
		node_shader_write_frag(shader, "		idany = 1.0;");
		node_shader_write_frag(shader, "		if (abs(idsel.r - idraw.r) < 0.02 && abs(idsel.g - idraw.g) < 0.02 && abs(idsel.b - idraw.b) < 0.02) { idhit = 1.0; }");
		node_shader_write_frag(shader, "	}");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "float iddim = 1.0;");
		node_shader_write_frag(shader, "if (idany > 0.5 && idhit < 0.5) { iddim = 0.15; }");
		node_shader_write_frag(shader, "vec3 idcol = pow(idraw, vec3(2.2, 2.2, 2.2)) * iddim;");
		node_shader_write_frag(shader, "output[1] = vec4(idcol.rgb, 1.0);"); // occ
	}
	else {
		node_shader_add_function(shader, str_octahedron_wrap);
		node_shader_add_function(shader, str_cotangent_frame);
		if (layer_pass > 0) {
			node_shader_add_texture(shader, "gbuffer0", NULL);
			node_shader_add_texture(shader, "gbuffer1", NULL);
			node_shader_add_texture(shader, "gbuffer2", NULL);
			node_shader_write_frag(
			    shader, "vec2 fragcoord = vec2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
			node_shader_write_frag(shader, "fragcoord.y = 1.0 - fragcoord.y;");
			node_shader_write_frag(shader, "vec4 gbuffer0_sample = textureLod(sampler2D(gbuffer0, sampler_linear), fragcoord, 0.0);");
			node_shader_write_frag(shader, "vec4 gbuffer1_sample = textureLod(sampler2D(gbuffer1, sampler_linear), fragcoord, 0.0);");
			node_shader_write_frag(shader, "vec4 gbuffer2_sample = textureLod(sampler2D(gbuffer2, sampler_linear), fragcoord, 0.0);");
			node_shader_write_frag(shader, "vec3 basecol = gbuffer0_sample.rgb;");
			node_shader_write_frag(shader, "float roughness = gbuffer2_sample.g;");
			node_shader_write_frag(shader, "float metallic = gbuffer2_sample.b;");
			node_shader_write_frag(shader, "float occlusion = gbuffer2_sample.r;");
			node_shader_write_frag(shader, "float opacity = 1.0;//gbuffer0_sample.a;");
			node_shader_write_frag(shader, "float matid = gbuffer1_sample.a;");
			node_shader_write_frag(shader, "vec3 ntex = gbuffer1_sample.rgb;");
			node_shader_write_frag(shader, "float height = gbuffer2_sample.a;");
		}
		else if (cached) { // The layers under the edited one as the cache has them (layer_cache.c)
			texture_count += 4;
			node_shader_add_texture(shader, "layer_cache0", NULL);
			node_shader_add_texture(shader, "layer_cache1", NULL);
			node_shader_add_texture(shader, "layer_cache2", NULL);
			node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
			node_shader_write_frag(shader, "vec4 layer_cache0_sample = textureLod(sampler2D(layer_cache0, sampler_linear), tex_coord, 0.0);");
			node_shader_write_frag(shader, "vec4 layer_cache1_sample = textureLod(sampler2D(layer_cache1, sampler_linear), tex_coord, 0.0);");
			node_shader_write_frag(shader, "vec4 layer_cache2_sample = textureLod(sampler2D(layer_cache2, sampler_linear), tex_coord, 0.0);");
			node_shader_write_frag(shader, "vec3 basecol = layer_cache0_sample.rgb;");
			node_shader_write_frag(shader, "float roughness = layer_cache2_sample.g;");
			node_shader_write_frag(shader, "float metallic = layer_cache2_sample.b;");
			node_shader_write_frag(shader, "float occlusion = layer_cache2_sample.r;");
			node_shader_write_frag(shader, "float opacity = 1.0;");
			// The material id of one texel, ids do not blend (as the layers' own are read)
			node_shader_write_frag(shader, "float matid = texelFetch(sampler2D(layer_cache1, sampler_linear), ivec2(uint(tex_coord.x * "
			                               "constants.texpaint_size.x), uint(tex_coord.y * constants.texpaint_size.y)), 0).a;");
			node_shader_write_frag(shader, "vec3 ntex = layer_cache1_sample.rgb;");
			node_shader_write_frag(shader, "float height = layer_cache2_sample.a;");
		}
		else {
			node_shader_write_frag(shader, "vec3 basecol = vec3(0.0, 0.0, 0.0);");
			node_shader_write_frag(shader, "float roughness = 0.0;");
			node_shader_write_frag(shader, "float metallic = 0.0;");
			node_shader_write_frag(shader, "float occlusion = 1.0;");
			node_shader_write_frag(shader, "float opacity = 1.0;");
			node_shader_write_frag(shader, "float matid = 0.0;");
			node_shader_write_frag(shader, "vec3 ntex = vec3(0.5, 0.5, 1.0);");
			node_shader_write_frag(shader, "float height = 0.0;");
		}
		node_shader_write_frag(shader, "vec4 texpaint_sample = vec4(0.0, 0.0, 0.0, 1.0);");
		node_shader_write_frag(shader, "vec4 texpaint_nor_sample;");
		node_shader_write_frag(shader, "vec4 texpaint_pack_sample;");
		node_shader_write_frag(shader, "float texpaint_opac;");

		make_fill_uses(&make_material_emis_used, &make_material_height_used); // Fill layers with emission or height
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {                 // Raised layers
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_is_layer(l) && slot_layer_is_visible(l) && l->paint_height && l->height_raise > 0.0) {
				make_material_height_used = true;
			}
		}

		if (make_material_height_used && cached) { // The height a texel away each way, as the cache has it
			node_shader_add_texture(shader, "layer_cache3", NULL);
			node_shader_write_frag(shader, "vec4 layer_cache3_sample = textureLod(sampler2D(layer_cache3, sampler_linear), tex_coord, 0.0);");
			node_shader_write_frag(shader, "float height0 = layer_cache3_sample.x;");
			node_shader_write_frag(shader, "float height1 = layer_cache3_sample.y;");
			node_shader_write_frag(shader, "float height2 = layer_cache3_sample.z;");
			node_shader_write_frag(shader, "float height3 = layer_cache3_sample.w;");
		}
		else if (make_material_height_used) {
			node_shader_write_frag(shader, "float height0 = 0.0;");
			node_shader_write_frag(shader, "float height1 = 0.0;");
			node_shader_write_frag(shader, "float height2 = 0.0;");
			node_shader_write_frag(shader, "float height3 = 0.0;");
		}

		if (g_context->draw_wireframe && !bake) {
			texture_count++;
			node_shader_add_texture(shader, "texuvmap", "_texuvmap");
		}

		if (box_bake_maps_inpaint_overlay() != NULL && !bake) {
			texture_count++; // Added in the last pass
		}

		if (g_context->viewport_mode == VIEWPORT_MODE_MASK && slot_layer_get_masks(g_context->layer, true) != NULL && !bake) {
			for (i32 i = 0; i < slot_layer_get_masks(g_context->layer, true)->length; ++i) {
				slot_layer_t *m = slot_layer_get_masks(g_context->layer, true)->buffer[i];
				if (!slot_layer_is_visible(m)) {
					continue;
				}
				texture_count++;
				i32 index = array_index_of(g_project->_->layers, m);
				node_shader_add_texture(shader, string_tmp("texpaint_view_mask%s", i32_to_string(m->id)), string_tmp("_texpaint%s", i32_to_string(index)));
			}
		}

		// Get layers for this pass: the bake takes the cached ones (one pass, layer_cache.c fits them), the viewport the others
		i32                   pass_count       = 1;
		slot_layer_t_array_t *layers           = any_array_create_from_raw((void *[]){}, 0);
		i32                   start_count      = texture_count;
		bool                  fills_counted    = false;
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (!slot_layer_is_visible(l)) {
				continue;
			}
			if (!slot_layer_is_layer(l)) {
				continue;
			}
			if (slot_layer_has_visible_filter(l)) {
				continue;
			}
			if (bake != layer_cache_has(l)) {
				continue;
			}

			i32 count = 3;
			if (l->fill != NULL) { // Live: its images and material tiles, the parameters texture once
				count = make_fill_texture_count(l) + (fills_counted ? 0 : 1);
				fills_counted = true;
			}

			slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
			if (masks != NULL) {
				count += masks->length;
			}

			texture_count += count;
			if (texture_count >= GPU_MAX_TEXTURES - 3 && !bake) {
				texture_count = start_count + count + 3; // gbuffer0_copy, gbuffer1_copy, gbuffer2_copy
				pass_count++;
			}

			if (bake || layer_pass == pass_count - 1) {
				any_array_push(layers, l);
			}
		}
		if (!bake) {
			make_mesh_layer_pass_count = pass_count;
		}

		bool last_pass = layer_pass == pass_count - 1;

		// Fill layers' height neighbours: the surface moved by a texel of the uv map, in world space (planar and
		// triplanar fills read the position). Outside any branch, derivatives need every fragment.
		bool fill_neighbours = false;
		for (i32 i = 0; i < layers->length; ++i) {
			slot_layer_t *l = layers->buffer[i];
			fill_neighbours = fill_neighbours || (l->fill != NULL && l->paint_height && make_material_height_used);
		}
		if (fill_neighbours) {
			node_shader_write_frag(shader, "vec3 fill_dpx = dFdx(input.wposition);");
			node_shader_write_frag(shader, "vec3 fill_dpy = dFdy(input.wposition);");
			node_shader_write_frag(shader, "vec2 fill_dux = dFdx(tex_coord);");
			node_shader_write_frag(shader, "vec2 fill_duy = dFdy(tex_coord);");
			node_shader_write_frag(shader, "float fill_det = fill_dux.x * fill_duy.y - fill_dux.y * fill_duy.x;");
			node_shader_write_frag(shader, "float fill_idet = abs(fill_det) > 0.0000000001 ? 1.0 / fill_det : 0.0;");
			node_shader_write_frag(shader, "vec3 fill_dpdu = (fill_dpx * fill_duy.y - fill_dpy * fill_dux.y) * fill_idet;");
			node_shader_write_frag(shader, "vec3 fill_dpdv = (fill_dpy * fill_dux.x - fill_dpx * fill_duy.x) * fill_idet;");
		}

		// Translucency: the bottom layer alpha is the surface opacity, layers above composite over it
		bool transluc = g_project->_->translucency && g_context->viewport_mode != VIEWPORT_MODE_PATH_TRACE && g_project->_->layers->length > 0 &&
		                (array_index_of(layers, g_project->_->layers->buffer[0]) >= 0 || (cached && layer_cache_has(g_project->_->layers->buffer[0])));
		if (transluc) {
			node_shader_write_frag(shader, cached ? "float transluc_opac = layer_cache0_sample.a;" : "float transluc_opac = 1.0;");
		}

		for (i32 i = 0; i < layers->length; ++i) {
			slot_layer_t *l = layers->buffer[i];
			if (slot_layer_get_object_mask(l) > 0) {
				node_shader_add_constant(shader, "int uid", "_uid");
				if (slot_layer_get_object_mask(l) > g_project->_->paint_objects->length) { // Atlas
					mesh_object_t_array_t *visibles = project_get_atlas_objects(slot_layer_get_object_mask(l));
					node_shader_write_frag(shader, "if (");
					for (i32 i = 0; i < visibles->length; ++i) {
						if (i > 0) {
							node_shader_write_frag(shader, " || ");
						}
						i32 uid = visibles->buffer[i]->base->uid;
						node_shader_write_frag(shader, string_tmp("%s == constants.uid", i32_to_string(uid)));
					}
					node_shader_write_frag(shader, ") {");
				}
				else { // Object mask
					i32 uid = g_project->_->paint_objects->buffer[slot_layer_get_object_mask(l) - 1]->base->uid;
					node_shader_write_frag(shader, string_tmp("if (%s == constants.uid) {", i32_to_string(uid)));
				}
			}

			char *tex_coord = l->uv_map == 1 ? "tex_coord1" : util_mesh_udim_layer(l) ? "tex_coord_atlas" : "tex_coord";
			bool  fill      = l->fill != NULL;
			if (fill) { // Evaluated here, at any zoom (make_fill.c)
				make_fill_declare(shader, l);
				node_shader_write_frag(shader, string_tmp("texpaint_sample = fill%d_base_at(%s, input.wposition, n);", l->id, tex_coord));
			}
			else {
				node_shader_add_texture(shader, string_tmp("texpaint%s", i32_to_string(l->id)), NULL);
				node_shader_write_frag(shader, string_tmp("texpaint_sample = textureLod(sampler2D(texpaint%s, sampler_linear), %s, 0.0);", i32_to_string(l->id), tex_coord));
			}
			if (g_project->linear_blending) { // Layers are stored gamma encoded, blended in linear
				node_shader_write_frag(shader, "texpaint_sample.rgb = pow(max(texpaint_sample.rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2));");
			}
			node_shader_write_frag(shader, "texpaint_opac = texpaint_sample.a;");

			bool height_blended = make_mesh_layer_height_blended(l);
			if (make_mesh_layer_has_visible_mask(l)) {
				char *texpaint_mask = string("texpaint_mask%s", i32_to_string(l->id));
				node_shader_write_frag(shader, string_tmp("float %s = 1.0;", texpaint_mask));
				make_mesh_write_masks(shader, l, texpaint_mask, tex_coord);
				node_shader_write_frag(shader, string_tmp("texpaint_opac *= clamp(%s, 0.0, 1.0);", texpaint_mask));
			}

			node_shader_write_frag(shader, string_tmp("texpaint_opac *= %s;", make_mesh_opacity(shader, l)));

			if (transluc) {
				node_shader_write_frag(shader, l == g_project->_->layers->buffer[0] ? "transluc_opac = texpaint_opac;"
				                                                                  : "transluc_opac += texpaint_opac * (1.0 - transluc_opac);");
			}

			if (l->paint_base) {
				if (l == g_project->_->layers->buffer[0]) {
					node_shader_write_frag(shader, "basecol = texpaint_sample.rgb * texpaint_opac;");
				}
				else {
					node_shader_write_frag(shader, string_tmp("basecol = %s;", make_material_blend_mode(shader, slot_layer_get_blending(l), "basecol",
					                                                                                  "texpaint_sample.rgb", "texpaint_opac")));
				}
			}

			if (l->paint_nor || make_material_emis_used) {
				if (fill) {
					node_shader_write_frag(shader, string_tmp("texpaint_nor_sample = fill%d_nor_at(%s, input.wposition, n);", l->id, tex_coord));
				}
				else {
					node_shader_add_texture(shader, string_tmp("texpaint_nor%s", i32_to_string(l->id)), NULL);
					node_shader_write_frag(shader, string_tmp("texpaint_nor_sample = textureLod(sampler2D(texpaint_nor%s, sampler_linear), %s, 0.0);",
					                                        i32_to_string(l->id), tex_coord));
				}

				if (make_material_emis_used && fill) {
					node_shader_write_frag(shader, "if (texpaint_opac > 0.0) { matid = texpaint_nor_sample.a; }");
				}
				else if (make_material_emis_used) {
					node_shader_write_frag(shader, "if (texpaint_opac > 0.0) {");
					node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
					node_shader_write_frag(shader, string_tmp("	vec4 texpaint_nor_raw = texelFetch(sampler2D(texpaint_nor%s, sampler_linear), ivec2(uint(%s.x * constants.texpaint_size.x), "
					                                        "uint(%s.y * constants.texpaint_size.y)), 0);",
					                                        i32_to_string(l->id), tex_coord, tex_coord));
					node_shader_write_frag(shader, "	matid = texpaint_nor_raw.a;");
					node_shader_write_frag(shader, "}");
				}

				if (l->paint_nor) {
					if (l->paint_nor_blend) {
						// Whiteout blend
						node_shader_write_frag(shader, "{");
						node_shader_write_frag(shader, "vec3 n1 = ntex * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);");
						node_shader_write_frag(shader, "vec3 n2 = mix(vec3(0.5, 0.5, 1.0), texpaint_nor_sample.rgb, texpaint_opac) * vec3(2.0, 2.0, "
						                             "2.0) - vec3(1.0, 1.0, 1.0);");
						node_shader_write_frag(shader, "ntex = normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5);");
						node_shader_write_frag(shader, "}");
					}
					else {
						node_shader_write_frag(shader, "ntex = mix(ntex, texpaint_nor_sample.rgb, texpaint_opac);");
					}
				}
			}

			if (l->paint_occ || l->paint_rough || l->paint_met || (l->paint_height && make_material_height_used)) {
				make_mesh_write_pack(shader, l, tex_coord);

				bool pack_blend = make_material_blend_mode_channels(slot_layer_get_blending(l)) && l != g_project->_->layers->buffer[0];
				if (pack_blend && (l->paint_occ || l->paint_rough || l->paint_met)) {
					// The layer blend mode on occlusion, roughness and metallic
					char *prev = string("pack_prev%d", l->id);
					char *top  = string("pack_top%d", l->id);
					node_shader_write_frag(shader, string_tmp("vec3 %s = vec3(occlusion, roughness, metallic);", prev));
					node_shader_write_frag(shader, string_tmp("vec3 %s = texpaint_pack_sample.rgb;", top));
					char *res = string_copy(make_material_blend_mode(shader, slot_layer_get_blending(l), prev, top, "texpaint_opac"));
					node_shader_write_frag(shader, string_tmp("%s = clamp(%s, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));", prev, res));
					if (l->paint_occ) {
						node_shader_write_frag(shader, string_tmp("occlusion = %s.x;", prev));
					}
					if (l->paint_rough) {
						node_shader_write_frag(shader, string_tmp("roughness = %s.y;", prev));
					}
					if (l->paint_met) {
						node_shader_write_frag(shader, string_tmp("metallic = %s.z;", prev));
					}
				}
				else {
					if (l->paint_occ) {
						node_shader_write_frag(shader, "occlusion = mix(occlusion, texpaint_pack_sample.r, texpaint_opac);");
					}
					if (l->paint_rough) {
						node_shader_write_frag(shader, "roughness = mix(roughness, texpaint_pack_sample.g, texpaint_opac);");
					}
					if (l->paint_met) {
						node_shader_write_frag(shader, "metallic = mix(metallic, texpaint_pack_sample.b, texpaint_opac);");
					}
				}
				if (l->paint_height && make_material_height_used) {
					if (!l->paint_height_blend) {
						node_shader_write_frag(shader, "height *= 1.0 - texpaint_opac;");
						if (!height_blended) { // Height blended neighbours use their own opacity
							node_shader_write_frag(shader, "height0 *= 1.0 - texpaint_opac;");
							node_shader_write_frag(shader, "height1 *= 1.0 - texpaint_opac;");
							node_shader_write_frag(shader, "height2 *= 1.0 - texpaint_opac;");
							node_shader_write_frag(shader, "height3 *= 1.0 - texpaint_opac;");
						}
					}
					node_shader_write_frag(shader, "height += texpaint_pack_sample.a * texpaint_opac;");
					node_shader_write_frag(shader, "{");
					node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
					node_shader_write_frag(shader, "float tex_step = 1.0 / constants.texpaint_size.x;");
				}
				if (l->paint_height && make_material_height_used && height_blended) {
					// Each neighbour gets its own mask value, so the normals follow the strand edges
					char *offsets[] = {"vec2(%s.x - tex_step, %s.y)", "vec2(%s.x + tex_step, %s.y)", "vec2(%s.x, %s.y - tex_step)",
					                   "vec2(%s.x, %s.y + tex_step)"};
					for (i32 k = 0; k < 4; ++k) {
						char *h = string("height%d", k);
						node_shader_write_frag(shader, "{");
						char *nco_fmt = string("vec2 nco = %s;", offsets[k]);
						node_shader_write_frag(shader, string_tmp(nco_fmt, tex_coord, tex_coord));
						if (fill) {
							char *nwp = make_mesh_fill_neighbour_wp(k);
							node_shader_write_frag(shader, string_tmp("float npack = fill%d_pack_at(nco, %s, n).a;", l->id, nwp));
							node_shader_write_frag(shader, string_tmp("float nopac = fill%d_base_at(nco, %s, n).a;", l->id, nwp));
						}
						else {
							node_shader_write_frag(shader, string_tmp("float npack = textureLod(sampler2D(texpaint_pack%s, sampler_linear), nco, 0.0).a;", i32_to_string(l->id)));
							node_shader_write_frag(shader, string_tmp("float nopac = textureLod(sampler2D(texpaint%s, sampler_linear), nco, 0.0).a;", i32_to_string(l->id)));
						}
						node_shader_write_frag(shader, "float nmask = 1.0;");
						make_mesh_write_masks(shader, l, "nmask", "nco");
						node_shader_write_frag(shader, "nopac *= clamp(nmask, 0.0, 1.0);");
						node_shader_write_frag(shader, string_tmp("nopac *= %s;", make_mesh_opacity(shader, l)));
						if (!l->paint_height_blend) {
							node_shader_write_frag(shader, string_tmp("%s *= 1.0 - nopac;", h));
						}
						node_shader_write_frag(shader, string_tmp("%s += npack * nopac;", h));
						node_shader_write_frag(shader, "}");
					}
					node_shader_write_frag(shader, "}");
				}
				else if (l->paint_height && make_material_height_used && fill) {
					char *offsets[] = {"vec2(%s.x - tex_step, %s.y)", "vec2(%s.x + tex_step, %s.y)", "vec2(%s.x, %s.y - tex_step)",
					                   "vec2(%s.x, %s.y + tex_step)"};
					for (i32 k = 0; k < 4; ++k) {
						char *nco = string_copy(string_tmp(offsets[k], tex_coord, tex_coord));
						node_shader_write_frag(shader, string_tmp("height%d += fill%d_pack_at(%s, %s, n).a * texpaint_opac;", k, l->id, nco,
						                                        make_mesh_fill_neighbour_wp(k)));
					}
					node_shader_write_frag(shader, "}");
				}
				else if (l->paint_height && make_material_height_used) {
					node_shader_write_frag(
					    shader, string_tmp("height0 += textureLod(sampler2D(texpaint_pack%s, sampler_linear), vec2(%s.x - tex_step, %s.y), 0.0).a * texpaint_opac;",
					                     i32_to_string(l->id), tex_coord, tex_coord));
					node_shader_write_frag(
					    shader, string_tmp("height1 += textureLod(sampler2D(texpaint_pack%s, sampler_linear), vec2(%s.x + tex_step, %s.y), 0.0).a * texpaint_opac;",
					                     i32_to_string(l->id), tex_coord, tex_coord));
					node_shader_write_frag(
					    shader, string_tmp("height2 += textureLod(sampler2D(texpaint_pack%s, sampler_linear), vec2(%s.x, %s.y - tex_step), 0.0).a * texpaint_opac;",
					                     i32_to_string(l->id), tex_coord, tex_coord));
					node_shader_write_frag(
					    shader, string_tmp("height3 += textureLod(sampler2D(texpaint_pack%s, sampler_linear), vec2(%s.x, %s.y + tex_step), 0.0).a * texpaint_opac;",
					                     i32_to_string(l->id), tex_coord, tex_coord));
					node_shader_write_frag(shader, "}");
				}
				if (l->paint_height && make_material_height_used && l->height_raise > 0.0) {
					make_mesh_write_raise(shader, l, tex_coord);
				}
			}

			if (slot_layer_get_object_mask(l) > 0) {
				node_shader_write_frag(shader, "}");
			}
		}
		array_delete(layers);

		if (bake) { // The running state as it is after the cached layers
			node_shader_write_frag(shader, transluc ? "output[0] = vec4(basecol, transluc_opac);" : "output[0] = vec4(basecol, 1.0);");
			node_shader_write_frag(shader, "output[1] = vec4(ntex, matid);");
			node_shader_write_frag(shader, "output[2] = vec4(occlusion, roughness, metallic, height);");
			node_shader_write_frag(shader, make_material_height_used ? "output[3] = vec4(height0, height1, height2, height3);"
			                                                         : "output[3] = vec4(0.0, 0.0, 0.0, 0.0);");
			parser_material_finalize(con_mesh);
			con_mesh->data->shader_from_source = true;
			gpu_create_shaders_from_glsl(node_shader_get(shader), &con_mesh->data->vertex_shader, &con_mesh->data->fragment_shader,
			                             &con_mesh->data->_->vertex_shader_size, &con_mesh->data->_->fragment_shader_size);
			return con_mesh;
		}

		if (transluc) {
			con_mesh->data->cull_mode = "none";
			shader->frag_wvpposition    = true;
			node_shader_add_function(shader, str_dither_bayer);
			node_shader_add_constant(shader, "vec2 gbuffer_size", "_gbuffer_size");
			node_shader_write_frag(
			    shader, "vec2 fragcoord1 = vec2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
			node_shader_write_frag(shader, "float dither = dither_bayer(fragcoord1 * constants.gbuffer_size);");
			node_shader_write_frag(shader, "if (transluc_opac <= dither) { discard; }");
		}

		if (last_pass && g_context->draw_texels) {
			node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
			node_shader_write_frag(shader, string_tmp("vec2 texel0 = %s * constants.texpaint_size * 0.01;", tex_coord_layer));
			node_shader_write_frag(shader, string_tmp("vec2 texel1 = %s * constants.texpaint_size * 0.1;", tex_coord_layer));
			node_shader_write_frag(shader, string_tmp("vec2 texel2 = %s * constants.texpaint_size;", tex_coord_layer));
			node_shader_write_frag(shader, "float texel0xmod = mod(float(int(texel0.x)), 2.0);");
			node_shader_write_frag(shader, "float texel0ymod = mod(float(int(texel0.y)), 2.0);");
			node_shader_write_frag(shader, "float texel1xmod = mod(float(int(texel1.x)), 2.0);");
			node_shader_write_frag(shader, "float texel1ymod = mod(float(int(texel1.y)), 2.0);");
			node_shader_write_frag(shader, "float texel2xmod = mod(float(int(texel2.x)), 2.0);");
			node_shader_write_frag(shader, "float texel2ymod = mod(float(int(texel2.y)), 2.0);");
			node_shader_write_frag(shader, "if (texel0xmod == texel0ymod) { basecol = basecol * 0.9; }");
			node_shader_write_frag(shader, "if (texel1xmod == texel1ymod) { basecol = basecol * 0.9; }");
			node_shader_write_frag(shader, "if (texel2xmod == texel2ymod) { basecol = basecol * 0.9; }");
		}

		if (last_pass && box_bake_maps_inpaint_overlay() != NULL) {
			i32 c = box_bake_maps_inpaint_color();
			node_shader_add_texture(shader, "texinpaint_overlay", "_texinpaint_overlay");
			node_shader_write_frag(shader, string_tmp("float inpaint_overlay = textureLod(sampler2D(texinpaint_overlay, sampler_linear), %s, 0.0).r * %f;", tex_coord_layer,
			                                        color_get_ab(c) / 255.0));
			node_shader_write_frag(shader, string_tmp("basecol = mix(basecol, vec3(%f, %f, %f), inpaint_overlay);", color_get_rb(c) / 255.0,
			                                        color_get_gb(c) / 255.0, color_get_bb(c) / 255.0));
		}

		if (last_pass && g_context->draw_wireframe) {
			node_shader_write_frag(shader, string_tmp("float wireframe = textureLod(sampler2D(texuvmap, sampler_linear), %s, 0.0).a;", tex_coord_layer));
			node_shader_write_frag(shader, "basecol = basecol * (1.0 - wireframe * 0.25);");
			node_shader_write_frag(shader, "roughness = max(roughness, wireframe);");
		}

		if (make_material_height_used) {
			// Everywhere a neighbour has height, also the texels just outside a bump: its slope starts there
			node_shader_add_constant(shader, "float bump_scale", "_bump_scale");
			node_shader_write_frag(shader, "float height_dx = height0 - height1;");
			node_shader_write_frag(shader, "float height_dy = height2 - height3;");
			node_shader_write_frag(shader, "if (height_dx != 0.0 || height_dy != 0.0) {");
			// Whiteout blend
			node_shader_write_frag(shader, "vec3 n1 = ntex * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);");
			node_shader_write_frag(shader, "vec3 n2 = normalize(vec3(height_dx * constants.bump_scale, height_dy * constants.bump_scale, 1.0));");
			node_shader_write_frag(shader, "ntex = normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5);");
			node_shader_write_frag(shader, "}");
		}

		if (!last_pass) {
			node_shader_write_frag(shader, "output[0] = vec4(basecol, opacity);");
			node_shader_write_frag(shader, "output[1] = vec4(ntex, matid);");
			node_shader_write_frag(shader, "output[2] = vec4(occlusion, roughness, metallic, height);");
			parser_material_finalize(con_mesh);
			con_mesh->data->shader_from_source = true;
			gpu_create_shaders_from_glsl(node_shader_get(shader), &con_mesh->data->vertex_shader, &con_mesh->data->fragment_shader,
			                             &con_mesh->data->_->vertex_shader_size, &con_mesh->data->_->fragment_shader_size);
			return con_mesh;
		}
		shader->frag_vvec = true;
		node_shader_write_frag(shader, "mat3 TBN = cotangent_frame(n, input.eye_dir, tex_coord);");
		node_shader_write_frag(shader, "n = ntex * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);");
		node_shader_write_frag(shader, "n.y = -n.y;");
		node_shader_write_frag(shader, "n = normalize(TBN * n);");

		bool lit = g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE;
		if (g_project->linear_blending && !lit) { // Back to gamma for the channel views
			node_shader_write_frag(shader, "basecol = pow(max(basecol, vec3(0.0, 0.0, 0.0)), vec3(0.4545, 0.4545, 0.4545));");
		}
		if (lit) {
			if (!g_project->linear_blending) {
				node_shader_write_frag(shader, "basecol = pow(basecol, vec3(2.2, 2.2, 2.2));");
			}
			node_shader_write_frag(shader, "basecol = max(basecol, vec3(0.0, 0.0, 0.0));");

			if (make_material_emis_used) {
				node_shader_write_frag(shader, "if (mod(float(int(matid * 255.0)), 3.0) == 1.0) { basecol = basecol * 10.0; }");
			}
			node_shader_write_frag(shader, "output[1] = vec4(basecol, occlusion);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_BASE_COLOR && g_context->layer->paint_base) {
			node_shader_write_frag(shader, "output[1] = vec4(basecol, 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_NORMAL_MAP && g_context->layer->paint_nor) {
			node_shader_write_frag(shader, "output[1] = vec4(ntex.rgb, 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_OCCLUSION && g_context->layer->paint_occ) {
			node_shader_write_frag(shader, "output[1] = vec4(vec3(occlusion, occlusion, occlusion), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_ROUGHNESS && g_context->layer->paint_rough) {
			node_shader_write_frag(shader, "output[1] = vec4(vec3(roughness, roughness, roughness), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_METALLIC && g_context->layer->paint_met) {
			node_shader_write_frag(shader, "output[1] = vec4(vec3(metallic, metallic, metallic), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_OPACITY && g_context->layer->paint_opac) {
			node_shader_write_frag(shader, "output[1] = vec4(vec3(texpaint_sample.a, texpaint_sample.a, texpaint_sample.a), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_HEIGHT && g_context->layer->paint_height) {
			node_shader_write_frag(shader, "output[1] = vec4(vec3(height, height, height), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_EMISSION) {
			node_shader_write_frag(shader, "float matid_mod = mod(float(int(matid * 255.0)), 3.0);");
			node_shader_write_frag(shader, "float emis = 0.0; if (matid_mod == 1.0) { emis = 1.0; }");
			node_shader_write_frag(shader, "output[1] = vec4(vec3(emis, emis, emis), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_SUBSURFACE) {
			node_shader_write_frag(shader, "float matid_mod = mod(float(int(matid * 255.0)), 3.0);");
			node_shader_write_frag(shader, "float subs = 0.0; if (matid_mod == 2.0) { subs = 1.0; }");
			node_shader_write_frag(shader, "output[1] = vec4(vec3(subs, subs, subs), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_TEXCOORD) {
			node_shader_write_frag(shader, string_tmp("output[1] = vec4(%s, 0.0, 1.0);", tex_coord_layer));
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_OBJECT_NORMAL) {
			shader->frag_nattr = true;
			node_shader_write_frag(shader, "output[1] = vec4(input.nattr, 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_WORLD_NORMAL) {
			node_shader_write_frag(shader, "output[1] = vec4(n * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5), 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_PLUGIN) {
			node_shader_write_frag(shader, "vec3 output_color = basecol;");
			if (g_context->viewport_texture != NULL) {
				node_shader_add_texture(shader, "texviewport", "_texviewport");
			}
			if (g_context->viewport_shader != NULL) {
				minic_val_t args[1] = {minic_val_ptr(shader)};
				minic_call_fn(g_context->viewport_shader, args, 1);
			}
			node_shader_write_frag(shader, "output[1] = vec4(output_color, 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_MATERIAL_ID) {
			i32 id = g_context->layer->id;
			node_shader_add_texture(shader, string_tmp("texpaint_nor%s", i32_to_string(id)), NULL);
			node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
			node_shader_write_frag(shader, string_tmp("vec2 sample_matid_coord = %s * constants.texpaint_size;", tex_coord_layer));
			node_shader_write_frag(shader, string_tmp("vec4 sample_matid4 = texelFetch(sampler2D(texpaint_nor%s, sampler_linear), ivec2(uint(sample_matid_coord.x), uint(sample_matid_coord.y)), 0);",
			                                        i32_to_string(id)));
			node_shader_write_frag(shader, "float sample_matid = sample_matid4.a + 1.0 / 255.0;");
			node_shader_write_frag(shader, "float matid_r = fract(sin(dot(vec2(sample_matid, sample_matid * 20.0), vec2(12.9898, 78.233))) * 43758.5453);");
			node_shader_write_frag(shader, "float matid_g = fract(sin(dot(vec2(sample_matid * 20.0, sample_matid), vec2(12.9898, 78.233))) * 43758.5453);");
			node_shader_write_frag(shader, "float matid_b = fract(sin(dot(vec2(sample_matid, sample_matid * 40.0), vec2(12.9898, 78.233))) * 43758.5453);");
			node_shader_write_frag(shader, "output[1] = vec4(matid_r, matid_g, matid_b, 1.0);");
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_OBJECT_ID) {
			node_shader_add_constant(shader, "float object_id", "_object_id");
			{
				node_shader_write_frag(shader, "float obid = constants.object_id + 1.0 / 255.0;");
				node_shader_write_frag(shader, "float id_r = fract(sin(dot(vec2(obid, obid * 20.0), vec2(12.9898, 78.233))) * 43758.5453);");
				node_shader_write_frag(shader, "float id_g = fract(sin(dot(vec2(obid * 20.0, obid), vec2(12.9898, 78.233))) * 43758.5453);");
				node_shader_write_frag(shader, "float id_b = fract(sin(dot(vec2(obid, obid * 40.0), vec2(12.9898, 78.233))) * 43758.5453);");
				node_shader_write_frag(shader, "output[1] = vec4(id_r, id_g, id_b, 1.0);");
			}
		}
		else if (g_context->viewport_mode == VIEWPORT_MODE_MASK &&
		         (slot_layer_get_masks(g_context->layer, true) != NULL || slot_layer_is_mask(g_context->layer))) {
			if (slot_layer_is_mask(g_context->layer)) {
				i32 id = g_context->layer->id;
				node_shader_add_texture(shader, string_tmp("texmask%s", i32_to_string(id)), NULL);
				node_shader_write_frag(shader,
				                       string_tmp("float mask_view = textureLod(sampler2D(texmask%s, sampler_linear), %s, 0.0).r;", i32_to_string(id), tex_coord_layer));
			}
			else {
				node_shader_write_frag(shader, "float mask_view = 1.0;");
				for (i32 i = 0; i < slot_layer_get_masks(g_context->layer, true)->length; ++i) {
					slot_layer_t *m = slot_layer_get_masks(g_context->layer, true)->buffer[i];
					if (!slot_layer_is_visible(m)) {
						continue;
					}
					node_shader_write_frag(shader, string_tmp("float mask_sample%s = textureLod(sampler2D(texpaint_view_mask%s, sampler_linear), %s, 0.0).r;",
					                                        i32_to_string(m->id), i32_to_string(m->id), tex_coord_layer));
					node_shader_write_frag(shader, string_tmp("mask_view *= mix(1.0, mask_sample%s, %s);", i32_to_string(m->id), make_mesh_opacity(shader, m)));
				}
			}
			node_shader_write_frag(shader, "output[1] = vec4(mask_view, mask_view, mask_view, 1.0);");
		}
		else {
			node_shader_write_frag(shader, "output[1] = vec4(1.0, 0.0, 1.0, 1.0);");
		}
		node_shader_write_frag(shader, "n = n / (abs(n.x) + abs(n.y) + abs(n.z));");
		node_shader_write_frag(shader, "if (n.z < 0.0) { n.xy = octahedron_wrap(n.xy); }");
		node_shader_write_frag(shader, "output[0] = vec4(n.xy, roughness, pack_f32_i16(metallic, uint(mod(float(int(matid * 255.0)), 3.0))));");
	}

	// Half floats, split the texcoord into a coarse part and a residual
	if (render_path_base_gbuffer_uv()) {
		node_shader_write_frag(shader, string_tmp("vec2 tex_coord_hi = floor(%s.xy * 256.0) / 256.0;", tex_coord_layer));
		node_shader_write_frag(shader, string_tmp("output[2] = vec4(%s.xy - tex_coord_hi, tex_coord_hi);", tex_coord_layer));
	}

	parser_material_finalize(con_mesh);

	con_mesh->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con_mesh->data->vertex_shader, &con_mesh->data->fragment_shader,
	                             &con_mesh->data->_->vertex_shader_size, &con_mesh->data->_->fragment_shader_size);
	return con_mesh;
}
