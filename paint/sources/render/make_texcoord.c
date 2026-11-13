
#include "../global.h"

void make_texcoord_run(node_shader_t *shader, bool is_atlas) {

	bool      fill_layer = g_context->layer->fill_material != NULL;
	uv_type_t uv_type    = fill_layer ? g_context->layer->uv_type : g_context->brush_paint;
	bool      decal      = context_is_decal();

	char *scale = "1.0";
	if (!is_atlas) {
		node_shader_add_constant(shader, "float brush_scale", "_brush_scale");
		scale = "constants.brush_scale";
	}

	if (uv_type == UV_TYPE_PROJECT || decal) { // TexCoords - project
		node_shader_write_attrib_frag(shader, "vec2 uvsp = sp.xy;");

		if (fill_layer) { // Decal layer
			node_shader_write_attrib_frag(shader, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");

			{ // Rotation is a uniform, turning it needs no new shader
				node_shader_add_constant(shader, "vec2 uv_angle", "_uv_angle");
				node_shader_write_attrib_frag(shader, "uvsp = vec2(uvsp.x * constants.uv_angle.x - uvsp.y * constants.uv_angle.y, uvsp.x * "
				                                    "constants.uv_angle.y + uvsp.y * constants.uv_angle.x);");
			}

			shader->frag_n = true;
			node_shader_add_constant(shader, "vec3 decal_layer_nor", "_decal_layer_nor");
			f32 dot_angle = g_context->brush_angle_reject_dot;
			node_shader_write_frag(shader, string_tmp("if (abs(dot(n, constants.decal_layer_nor) - 1.0) > %s) { discard; }", f32_to_string(dot_angle)));

			shader->frag_wposition = true;
			node_shader_add_constant(shader, "vec3 decal_layer_loc", "_decal_layer_loc");
			node_shader_add_constant(shader, "float decal_layer_dim", "_decal_layer_dim");
			node_shader_write_attrib_frag(
			    shader, "if (abs(dot(constants.decal_layer_nor, constants.decal_layer_loc - input.wposition)) > constants.decal_layer_dim) { discard; }");
		}
		else if (decal) {
			shader->frag_wposition = true;
			node_shader_add_function(shader, str_octahedron_wrap);
			node_shader_add_texture(shader, "gbuffer0", NULL);
			node_shader_add_constant(shader, "vec4 decal_mask", "_decal_mask");
			node_shader_add_constant(shader, "mat4 invVP", "_inv_view_proj_matrix");
			node_shader_add_constant(shader, "mat4 VP", "_view_proj_matrix");
			node_shader_add_constant(shader, "vec3 camera_right", "_camera_right");
			node_shader_add_constant(shader, "vec3 camera_up", "_camera_up");
			node_shader_add_constant(shader, "float camera_align", "_brush_camera_align");

			// Off the mesh, anchor the decal at the depth of the world origin
			node_shader_write_attrib_frag(shader, "vec4 origin_clip = constants.VP * vec4(0.0, 0.0, 0.0, 1.0);");
			node_shader_write_attrib_frag(shader, "if (constants.camera_align > 0.0) {");
			node_shader_write_attrib_frag(shader, "float ca_depth = textureLod(sampler2D(gbufferD, sampler_linear), constants.decal_mask.xy, 0.0).r;");
			node_shader_write_attrib_frag(shader, "if (ca_depth >= 1.0 && origin_clip.w > 0.0) { ca_depth = origin_clip.z / origin_clip.w; }");
			node_shader_write_attrib_frag(shader, "vec2 ca_coord = vec2(constants.decal_mask.x * 2.0 - 1.0, 1.0 - constants.decal_mask.y * 2.0);");
			node_shader_write_attrib_frag(shader, "vec4 ca_homog = constants.invVP * vec4(ca_coord.x, ca_coord.y, ca_depth, 1.0);");
			node_shader_write_attrib_frag(shader, "float ca_clip_w = 1.0 / ca_homog.w;");
			node_shader_write_attrib_frag(shader, "float vp_up_y = (constants.VP * vec4(constants.camera_up, 0.0)).y;");
			node_shader_write_attrib_frag(shader, "uvsp = sp.xy - constants.decal_mask.xy;");
			node_shader_write_attrib_frag(shader, "uvsp.x *= constants.aspect_ratio;");
			node_shader_write_attrib_frag(shader, "uvsp = uvsp * (ca_clip_w / (vp_up_y * constants.brush_radius));");
			node_shader_write_attrib_frag(shader, "}");

			node_shader_write_attrib_frag(shader, "else {");

			// When mask is active, anchor the decal at the frozen position; otherwise follow the mouse
			node_shader_write_attrib_frag(shader, "vec2 decal_xy = constants.inp.xy;");
			node_shader_write_attrib_frag(shader, "if (constants.decal_mask.z > 0.0) { decal_xy = constants.decal_mask.xy; }");

			// Unproject the decal anchor point from the depth buffer
			node_shader_write_attrib_frag(shader, "float decal_depth = textureLod(sampler2D(gbufferD, sampler_linear), decal_xy, 0.0).r;");
			node_shader_write_attrib_frag(shader, "float decal_off_mesh = 0.0;");
			node_shader_write_attrib_frag(
			    shader, "if (decal_depth >= 1.0 && origin_clip.w > 0.0) { decal_depth = origin_clip.z / origin_clip.w; decal_off_mesh = 1.0; }");
			node_shader_write_attrib_frag(shader, "vec4 decal_wpos4 = vec4(vec2(decal_xy.x, 1.0 - decal_xy.y) * 2.0 - 1.0, decal_depth, 1.0);");
			node_shader_write_attrib_frag(shader, "decal_wpos4 = constants.invVP * decal_wpos4;");
			node_shader_write_attrib_frag(shader, "vec3 decal_wpos = decal_wpos4.xyz / decal_wpos4.w;");

			// Decode face normal at anchor point
			node_shader_write_attrib_frag(shader, "vec2 dg0 = textureLod(sampler2D(gbuffer0, sampler_linear), decal_xy, 0.0).rg;");
			node_shader_write_attrib_frag(shader, "vec3 dn;");
			node_shader_write_attrib_frag(shader, "dn.z = 1.0 - abs(dg0.x) - abs(dg0.y);");
			node_shader_write_attrib_frag(
			    shader, "if (dn.z >= 0.0) { dn.x = dg0.x; dn.y = dg0.y; } else { vec2 fw = octahedron_wrap(dg0.xy); dn.x = fw.x; dn.y = fw.y; }");
			node_shader_write_attrib_frag(shader, "dn = normalize(dn);");

			// Build tangent basis
			node_shader_write_attrib_frag(shader, "vec3 d_right = constants.camera_right;");
			node_shader_write_attrib_frag(shader, "if (abs(dot(dn, d_right)) > 0.999) { d_right = vec3(0.0, 0.0, 1.0); }");
			node_shader_write_attrib_frag(shader, "vec3 d_tan = normalize(d_right - dn * dot(d_right, dn));");
			node_shader_write_attrib_frag(shader, "vec3 d_bin = cross(d_tan, dn);");

			node_shader_write_attrib_frag(shader, "vec3 d_offset = input.wposition - decal_wpos;");
			node_shader_write_attrib_frag(shader, "float decal_radius = constants.brush_radius;");
			node_shader_write_attrib_frag(shader, "if (constants.decal_mask.z > 0.0) { decal_radius = constants.decal_mask.w; }");
			node_shader_write_attrib_frag(shader, "if (decal_off_mesh == 0.0 && abs(dot(d_offset, dn)) > decal_radius) { discard; }");
			node_shader_write_attrib_frag(shader, "uvsp = vec2(dot(d_offset, d_tan), dot(d_offset, d_bin));");
			node_shader_write_attrib_frag(shader, "uvsp = uvsp / decal_radius * 0.5;");
			// Off the mesh there is no surface to project from, map in screen space like camera align
			node_shader_write_attrib_frag(shader, "if (decal_off_mesh > 0.0) {");
			node_shader_write_attrib_frag(shader, "float off_up_y = (constants.VP * vec4(constants.camera_up, 0.0)).y;");
			node_shader_write_attrib_frag(shader, "uvsp = sp.xy - decal_xy;");
			node_shader_write_attrib_frag(shader, "uvsp.x *= constants.aspect_ratio;");
			node_shader_write_attrib_frag(shader, "uvsp = uvsp * ((1.0 / decal_wpos4.w) / (off_up_y * decal_radius));");
			node_shader_write_attrib_frag(shader, "}");

			node_shader_write_attrib_frag(shader, "}");

			if (g_context->brush_directional) {
				node_shader_add_constant(shader, "vec3 brush_direction", "_brush_direction");
				node_shader_write_attrib_frag(shader, "if (constants.brush_direction.z == 0.0) { discard; }");
				node_shader_write_attrib_frag(shader, "uvsp = vec2(uvsp.x * constants.brush_direction.x - uvsp.y * constants.brush_direction.y, uvsp.x * "
				                                    "constants.brush_direction.y + uvsp.y * constants.brush_direction.x);");
			}

			{ // Rotation is a uniform, turning it needs no new shader
				node_shader_add_constant(shader, "vec2 uv_angle", "_uv_angle");
				node_shader_write_attrib_frag(shader, "uvsp = vec2(uvsp.x * constants.uv_angle.x - uvsp.y * constants.uv_angle.y, uvsp.x * "
				                                    "constants.uv_angle.y + uvsp.y * constants.uv_angle.x);");
			}

			node_shader_add_constant(shader, "float brush_scale_x", "_brush_scale_x");
			node_shader_write_attrib_frag(shader, "uvsp.x *= constants.brush_scale_x;");
			node_shader_write_attrib_frag(shader, "uvsp += vec2(0.5, 0.5);");
			node_shader_write_attrib_frag(shader, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");
		}
		else {
			node_shader_write_attrib_frag(shader, "uvsp.x *= constants.aspect_ratio;");
			{ // Rotation is a uniform, turning it needs no new shader
				node_shader_add_constant(shader, "vec2 uv_angle", "_uv_angle");
				node_shader_write_attrib_frag(shader, "uvsp = vec2(uvsp.x * constants.uv_angle.x - uvsp.y * constants.uv_angle.y, uvsp.x * "
				                                    "constants.uv_angle.y + uvsp.y * constants.uv_angle.x);");
			}
		}

		node_shader_write_attrib_frag(shader, string_tmp("vec2 tex_coord = uvsp * %s;", scale));
	}
	else if (uv_type == UV_TYPE_UVMAP) { // TexCoords - uvmap
		node_shader_add_out(shader, "vec2 tex_coord");

		if (g_context->layer->uv_map == 1) {
			node_shader_write_vert(shader, string_tmp("output.tex_coord = input.tex1 * %s;", scale));
		}
		else if (!is_atlas && util_mesh_udim_layer(g_context->layer)) {
			// Materials repeat per tile, bake reads layer textures in atlas space
			char *uv = "atlas_uv";
			if (g_context->tool != TOOL_TYPE_BAKE) {
				node_shader_add_constant(shader, "float atlas_stride", "_atlas_stride");
				uv = "(atlas_uv * constants.atlas_stride)";
			}
			node_shader_write_vert(shader, string_tmp("output.tex_coord = %s * %s;", uv, scale));
		}
		else {
			node_shader_write_vert(shader, string_tmp("output.tex_coord = input.tex * %s;", scale));
		}

		{ // Rotation is a uniform, turning it needs no new shader
			node_shader_add_constant(shader, "vec2 uv_angle", "_uv_angle");
			node_shader_write_vert(shader,
			                       "output.tex_coord = vec2(output.tex_coord.x * constants.uv_angle.x - output.tex_coord.y * constants.uv_angle.y, "
			                       "output.tex_coord.x * constants.uv_angle.y + output.tex_coord.y * constants.uv_angle.x);");
		}
		node_shader_write_attrib_frag(shader, "vec2 tex_coord = input.tex_coord;");
	}
	else { // UV_TYPE_TRIPLANAR, TexCoords - triplanar
		shader->frag_wposition = true;
		shader->frag_n         = true;
		node_shader_write_attrib_frag(shader, "vec3 tri_weight = input.wnormal * input.wnormal;");
		node_shader_write_attrib_frag(shader, "float tri_max = max(tri_weight.x, max(tri_weight.y, tri_weight.z));");
		node_shader_write_attrib_frag(shader, "tri_weight = max(tri_weight - vec3(tri_max * 0.75, tri_max * 0.75, tri_max * 0.75), vec3(0.0, 0.0, 0.0));");
		node_shader_write_attrib_frag(shader, "vec3 tex_coord_blend = tri_weight * (1.0 / (tri_weight.x + tri_weight.y + tri_weight.z));");
		node_shader_write_attrib_frag(shader, string_tmp("vec2 tex_coord = input.wposition.yz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(shader, string_tmp("vec2 tex_coord1 = input.wposition.xz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(shader, string_tmp("vec2 tex_coord2 = input.wposition.xy * %s * 0.5;", scale));
		{ // Rotation is a uniform, turning it needs no new shader
			node_shader_add_constant(shader, "vec2 uv_angle", "_uv_angle");
			node_shader_write_attrib_frag(shader, "tex_coord = vec2(tex_coord.x * constants.uv_angle.x - tex_coord.y * constants.uv_angle.y, tex_coord.x "
			                                    "* constants.uv_angle.y + tex_coord.y * constants.uv_angle.x);");
			node_shader_write_attrib_frag(shader, "tex_coord1 = vec2(tex_coord1.x * constants.uv_angle.x - tex_coord1.y * constants.uv_angle.y, "
			                                    "tex_coord1.x * constants.uv_angle.y + tex_coord1.y * constants.uv_angle.x);");
			node_shader_write_attrib_frag(shader, "tex_coord2 = vec2(tex_coord2.x * constants.uv_angle.x - tex_coord2.y * constants.uv_angle.y, "
			                                    "tex_coord2.x * constants.uv_angle.y + tex_coord2.y * constants.uv_angle.x);");
		}
	}
}
