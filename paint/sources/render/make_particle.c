
#include "../global.h"

static shader_data_t *particle_bullet_material = NULL;

static node_shader_context_t *make_particle_bullet_run() {
	material_t            *mat   = ALLOC_INIT(material_t, {.name = "particle_bullet", .canvas = NULL});
	shader_context_t      *props = ALLOC_INIT(shader_context_t, {
	                                                                .name            = "mesh",
	                                                                .depth_write     = true,
	                                                                .compare_mode    = "less",
	                                                                .cull_mode       = "clockwise",
	                                                                .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                   },
                                                                   3),
	                                                                .color_attachments = any_array_create_from_raw((void *[]){"RGBA64", "RGBA64", "RGBA64"}, 3),
	                                                                .depth_attachment  = "D32",
                                                           });
	node_shader_context_t *con   = node_shader_context_create(mat, props);
	node_shader_t         *shader  = node_shader_context_make_shader(con);

	shader->frag_n   = true;
	shader->frag_out = "vec4[3]";

	node_shader_add_constant(shader, "mat4 WVP", "_world_view_proj_matrix");
	node_shader_write_vert(shader, "output.pos = constants.WVP * vec4(input.pos.xyz, 1.0);");

	node_shader_add_out(shader, "vec2 tex_coord");
	node_shader_write_vert(shader, "output.tex_coord = input.tex;");

	node_shader_add_function(shader, str_octahedron_wrap);
	node_shader_add_function(shader, str_pack_float_int16);

	node_shader_write_frag(shader, "vec3 basecol = vec3(0.8, 0.0, 0.0);");
	node_shader_write_frag(shader, "float roughness = 0.5;");
	node_shader_write_frag(shader, "float metallic = 0.0;");
	node_shader_write_frag(shader, "float occlusion = 1.0;");
	node_shader_write_frag(shader, "n = n / (abs(n.x) + abs(n.y) + abs(n.z));");
	node_shader_write_frag(shader, "if (n.z < 0.0) { n.xy = octahedron_wrap(n.xy); }");
	node_shader_write_frag(shader, "output[0] = vec4(n.xy, roughness, pack_f32_i16(metallic, uint(0)));");
	node_shader_write_frag(shader, "output[1] = vec4(basecol, occlusion);");
	node_shader_write_frag(shader, "vec2 tex_coord_hi = floor(input.tex_coord.xy * 256.0) / 256.0;");
	node_shader_write_frag(shader, "output[2] = vec4(input.tex_coord.xy - tex_coord_hi, tex_coord_hi);");

	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}

shader_data_t *make_particle_get_bullet_material() {
	if (particle_bullet_material != NULL) {
		return particle_bullet_material;
	}

	node_shader_context_t *con = make_particle_bullet_run();
	shader_context_load(con->data);

	particle_bullet_material = ALLOC_INIT(shader_data_t, {
	                                                         .name     = "particle_bullet",
	                                                         .contexts = any_array_create_from_raw((void *[]){con->data}, 1),
	                                                         ._        = ALLOC_INIT(shader_data_runtime_t, {.uid = 0.0}),
	                                                     });
	return particle_bullet_material;
}

void make_particle_mask(node_shader_t *shader) {
	node_shader_add_out(shader, "vec4 wpos");
	node_shader_add_constant(shader, "mat4 W", "_world_matrix");
	node_shader_write_attrib_vert(shader, "output.wpos = constants.W * vec4(input.pos.xyz, 1.0);");
	node_shader_add_constant(shader, "vec3 particle_hit", "_particle_hit");
	node_shader_add_constant(shader, "vec3 particle_hit_last", "_particle_hit_last");
	node_shader_add_constant(shader, "float particle_radius", "_particle_radius");

	node_shader_write_frag(shader, "vec3 pa = input.wpos.xyz - constants.particle_hit;");
	node_shader_write_frag(shader, "vec3 ba = constants.particle_hit_last - constants.particle_hit;");
	node_shader_write_frag(shader, "float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.00000001), 0.0, 1.0);");
	node_shader_write_frag(shader, "dist = length(pa - ba * h) * (5.0 / constants.particle_radius);");
	node_shader_write_frag(shader, "if (dist > 1.0) { discard; }");
	node_shader_write_frag(shader, "float str = clamp(pow(1.0 / dist * constants.brush_hardness * 0.2, 4.0), 0.0, 1.0) * opacity;");
	node_shader_write_frag(shader, "if (str == 0.0) { discard; }");
}
