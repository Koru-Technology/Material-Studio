
#include "../global.h"

// Angle reject: wn is the normal under the brush, fragments facing away from it are dropped
static void make_brush_angle_reject(node_shader_t *shader) {
	node_shader_add_function(shader, str_octahedron_wrap);
	node_shader_add_texture(shader, "gbuffer0", NULL);
	node_shader_write_frag(shader, "vec2 g0 = textureLod(sampler2D(gbuffer0, sampler_linear), constants.inp.xy, 0.0).rg;");
	node_shader_write_frag(shader, "vec3 wn;");
	node_shader_write_frag(shader, "wn.z = 1.0 - abs(g0.x) - abs(g0.y);");
	node_shader_write_frag(shader, "if (wn.z >= 0.0) { wn.x = g0.x; wn.y = g0.y; } else { vec2 f2 = octahedron_wrap(g0.xy); wn.x = f2.x; wn.y = f2.y; }");
	node_shader_write_frag(shader, "wn = normalize(wn);");
	node_shader_write_frag(shader, "float plane_dist = dot(wn, winp.xyz - input.wposition);");
	if (g_config->brush_angle_reject && !g_context->xray && !g_project->_->translucency) {
		node_shader_write_frag(shader, "if (plane_dist < -0.03) { discard; }");
		shader->frag_n = true;
		f32 angle    = g_context->brush_angle_reject_dot;
		node_shader_write_frag(shader, string_tmp("if (dot(wn, n) < %s) { discard; }", f32_to_string(angle)));
	}
}

// Tilt shading: scales the offsets pa and ba along the pen direction, the dab becomes an ellipse stretched along the pen.
// shape: x the scale along the direction minus 1, y the direction on screen, see util_brush_tilt_shape.
static bool make_brush_tilt_shading() {
	tool_type_t tool = g_context->tool;
	return util_brush_shaped() && (tool == TOOL_TYPE_BRUSH || tool == TOOL_TYPE_ERASER || tool == TOOL_TYPE_CLONE || tool == TOOL_TYPE_BLUR);
}

static void make_brush_tilt_stretch(node_shader_t *shader, char *shape, char *pa, char *ba) {
	node_shader_add_constant(shader, "vec3 camera_right", "_camera_right");
	node_shader_add_constant(shader, "vec3 camera_up", "_camera_up");
	node_shader_write_frag(shader, string_tmp("vec3 tilt_axis = normalize(normalize(constants.camera_right) * cos(%s.y) + normalize(constants.camera_up) * sin(%s.y));",
	                                        shape, shape));
	node_shader_write_frag(shader, string_tmp("%s = %s + tilt_axis * (dot(%s, tilt_axis) * %s.x);", pa, pa, pa, shape));
	node_shader_write_frag(shader, string_tmp("%s = %s + tilt_axis * (dot(%s, tilt_axis) * %s.x);", ba, ba, ba, shape));
}

static void make_brush_depth_reject(node_shader_t *shader) {
	node_shader_write_frag(shader, "if (sp.z > textureLod(sampler2D(gbufferD, sampler_linear), sp.xy, 0.0).r - 0.00008) { discard; }");
}

// depth_reject drops fragments hidden behind the surface under the brush.
// Most texels of the layer are far from the brush, so the cheap distance test runs first when possible.
void make_brush_run(node_shader_t *shader, bool depth_reject) {

	node_shader_write_frag(shader, "float dist = 0.0;");

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		return;
	}

	bool fill_layer = g_context->layer->fill_material != NULL;
	bool decal      = context_is_decal();
	bool fast       = !decal && !g_context->xray && !context_is_pixel_snap(); // Distance first
	if (depth_reject && !fast) {
		make_brush_depth_reject(shader);
	}
	if (decal && !fill_layer) {
		node_shader_write_frag(shader, "if (constants.decal_mask.z > 0.0) {");
	}

	node_shader_write_frag(shader, "float depth = textureLod(sampler2D(gbufferD, sampler_linear), constants.inp.xy, 0.0).r;");

	node_shader_add_constant(shader, "mat4 invVP", "_inv_view_proj_matrix");
	node_shader_write_frag(shader, "vec4 winp = vec4(vec2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0, depth, 1.0);");
	node_shader_write_frag(shader, "winp = constants.invVP * winp;");
	node_shader_write_frag(shader, "winp.xyz = winp.xyz / winp.w;");
	shader->frag_wposition = true;

	if (!fast && (g_config->brush_angle_reject || g_context->xray)) {
		make_brush_angle_reject(shader);
	}

	node_shader_write_frag(shader, "float depthlast = textureLod(sampler2D(gbufferD, sampler_linear), constants.inplast.xy, 0.0).r;");

	node_shader_write_frag(shader, "vec4 winplast = vec4(vec2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0, depthlast, 1.0);");
	node_shader_write_frag(shader, "winplast = constants.invVP * winplast;");
	node_shader_write_frag(shader, "winplast.xyz = winplast.xyz / winplast.w;");

	node_shader_write_frag(shader, "vec3 pa = input.wposition - winp.xyz;");
	if (g_context->xray) {
		node_shader_write_frag(shader, "pa += wn * vec3(plane_dist, plane_dist, plane_dist);");
	}
	node_shader_write_frag(shader, "vec3 ba = winplast.xyz - winp.xyz;");
	if (make_brush_tilt_shading() && !context_is_pixel_snap()) {
		node_shader_add_constant(shader, "vec2 brush_tilt", "_brush_tilt");
		make_brush_tilt_stretch(shader, "constants.brush_tilt", "pa", "ba");
	}

	node_shader_add_constant(shader, "mat4 VP", "_view_proj_matrix");
	node_shader_add_constant(shader, "vec3 camera_up", "_camera_up");
	node_shader_add_constant(shader, "float aspect_ratio", "_aspect_ratio_window");
	node_shader_add_constant(shader, "float camera_align", "_brush_camera_align");

	if (context_is_pixel_snap()) {
		node_shader_add_texture(shader, "gbuffer2", NULL);
		node_shader_add_constant(shader, "vec2 gbuffer_size", "_gbuffer_size");
		node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
		node_shader_add_constant(shader, "float pixel_snap_2d", "_pixel_snap_2d");
		node_shader_add_constant(shader, "vec4 pixel_snap_uv", "_pixel_snap_uv");
		node_shader_write_frag(shader, "vec2 pixel_inp = constants.pixel_snap_uv.xy;");
		node_shader_write_frag(shader, "vec2 pixel_last = constants.pixel_snap_uv.zw;");
		node_shader_write_frag(shader, "if (constants.pixel_snap_2d == 0.0) {");
		node_shader_write_frag(shader, "if (depth >= 1.0) { discard; }");
		node_shader_write_frag(shader, "vec4 pg = texelFetch(sampler2D(gbuffer2, sampler_linear), ivec2(uint(constants.inp.x * constants.gbuffer_size.x), "
		                             "uint(constants.inp.y * constants.gbuffer_size.y)), 0);");
		node_shader_write_frag(shader, "pixel_inp = pg.ba + pg.rg;");
		node_shader_write_frag(shader, "vec4 pgl = texelFetch(sampler2D(gbuffer2, sampler_linear), ivec2(uint(constants.inplast.x * constants.gbuffer_size.x), "
		                             "uint(constants.inplast.y * constants.gbuffer_size.y)), 0);");
		node_shader_write_frag(shader, "pixel_last = pgl.ba + pgl.rg;");
		node_shader_write_frag(shader, "if (depthlast >= 1.0 || length(pixel_last - pixel_inp) > 0.1) { pixel_last = pixel_inp; }");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "float pixel_width = max(floor(constants.brush_radius * 750.0 + 0.5), 1.0);");
		node_shader_write_frag(shader, "float pixel_radius = pixel_width * 0.5;");
		node_shader_write_frag(shader, "vec2 pixel_off = vec2(0.5, 0.5);");
		node_shader_write_frag(shader, "if (floor(pixel_radius) == pixel_radius) { pixel_off = vec2(0.0, 0.0); }");
		node_shader_write_frag(shader, "vec2 pixel_a = floor(pixel_inp * constants.texpaint_size + vec2(0.5, 0.5) - pixel_off) + pixel_off;");
		node_shader_write_frag(shader, "vec2 pixel_b = floor(pixel_last * constants.texpaint_size + vec2(0.5, 0.5) - pixel_off) + pixel_off;");
		node_shader_write_frag(shader, "vec2 pixel_frac = input.pixel_uv - floor(input.pixel_uv);");
		node_shader_write_frag(shader, "vec2 pixel_p = floor(pixel_frac * constants.texpaint_size) + vec2(0.5, 0.5);");
		node_shader_write_frag(shader, "vec2 pixel_pa = pixel_p - pixel_a;");
		node_shader_write_frag(shader, "vec2 pixel_ba = pixel_b - pixel_a;");
		node_shader_write_frag(shader, "float pixel_h = clamp(dot(pixel_pa, pixel_ba) / max(dot(pixel_ba, pixel_ba), 0.000001), 0.0, 1.0);");
		node_shader_write_frag(shader, "float pixel_dist = length(pixel_pa - pixel_ba * pixel_h);");
		node_shader_write_frag(shader, "if (pixel_dist > pixel_radius) { discard; }");
		node_shader_write_frag(shader, "dist = pixel_dist / pixel_radius * constants.brush_radius;");
		node_shader_write_frag(shader, "pixel_pa = pixel_pa / pixel_radius * constants.brush_radius;");
	}
	else {
		node_shader_write_frag(shader, "if (constants.camera_align > 0.0) {");
		node_shader_write_frag(shader, "float vp_up_y = (constants.VP * vec4(constants.camera_up, 0.0)).y;");
		node_shader_write_frag(shader, "float ca_scale = (1.0 / winp.w) / (vp_up_y * constants.brush_radius);");
		node_shader_write_frag(shader, "vec2 sa = sp.xy - constants.inp.xy;");
		node_shader_write_frag(shader, "sa.x *= constants.aspect_ratio;");
		node_shader_write_frag(shader, "sa = sa * ca_scale;");
		// Capsule
		node_shader_write_frag(shader, "vec2 sb = constants.inplast.xy - constants.inp.xy;");
		node_shader_write_frag(shader, "sb.x *= constants.aspect_ratio;");
		node_shader_write_frag(shader, "sb = sb * ca_scale;");
		node_shader_write_frag(shader, "float sh = clamp(dot(sa, sb) / max(dot(sb, sb), 0.00000001), 0.0, 1.0);");
		node_shader_write_frag(shader, "dist = length(sa - sb * sh) * 2.0 * constants.brush_radius;");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "else {");
		node_shader_write_frag(shader, "float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.00000001), 0.0, 1.0);"); // A stamp is a zero length capsule
		node_shader_write_frag(shader, "dist = length(pa - ba * h);");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "if (dist > constants.brush_radius) { discard; }");
	}

	if (fast) { // Only for fragments under the brush
		if (depth_reject) {
			make_brush_depth_reject(shader);
		}
		if (g_config->brush_angle_reject) {
			make_brush_angle_reject(shader);
		}
	}

	if (decal && !fill_layer) {
		node_shader_write_frag(shader, "}");
	}
}

// Batched dabs: one draw paints every dab of a frame. Each dab is three texels of texpaintdabs:
// inp.xy inplast.xy, radius opacity hardness flow, mask rotation cos sin and tilt shape (util_brush_tilt_shape).
// Painting them one after another in a loop gives the same result as one draw per dab.

// Declares dab0, dab1, dab_winp and dab_dist for dab k
void make_brush_dab_dist(node_shader_t *shader) {
	node_shader_write_frag(shader, "vec4 dab0 = texelFetch(sampler2D(texpaintdabs, sampler_linear), ivec2(uint(k * 3), uint(0)), 0);");
	node_shader_write_frag(shader, "vec4 dab1 = texelFetch(sampler2D(texpaintdabs, sampler_linear), ivec2(uint(k * 3 + 1), uint(0)), 0);");
	node_shader_write_frag(shader, "float dab_depth = textureLod(sampler2D(gbufferD, sampler_linear), dab0.xy, 0.0).r;");
	node_shader_write_frag(shader, "vec4 dab_winp = vec4(vec2(dab0.x, 1.0 - dab0.y) * 2.0 - 1.0, dab_depth, 1.0);");
	node_shader_write_frag(shader, "dab_winp = constants.invVP * dab_winp;");
	node_shader_write_frag(shader, "dab_winp.xyz = dab_winp.xyz / dab_winp.w;");
	node_shader_write_frag(shader, "float dab_depthlast = textureLod(sampler2D(gbufferD, sampler_linear), dab0.zw, 0.0).r;");
	node_shader_write_frag(shader, "vec4 dab_winplast = vec4(vec2(dab0.z, 1.0 - dab0.w) * 2.0 - 1.0, dab_depthlast, 1.0);");
	node_shader_write_frag(shader, "dab_winplast = constants.invVP * dab_winplast;");
	node_shader_write_frag(shader, "dab_winplast.xyz = dab_winplast.xyz / dab_winplast.w;");
	node_shader_write_frag(shader, "vec3 dab_pa = input.wposition - dab_winp.xyz;");
	node_shader_write_frag(shader, "vec3 dab_ba = dab_winplast.xyz - dab_winp.xyz;");
	if (make_brush_tilt_shading()) { // Shape of each dab in the third texel, zw
		node_shader_write_frag(shader, "vec2 dab_tilt = texelFetch(sampler2D(texpaintdabs, sampler_linear), ivec2(uint(k * 3 + 2), uint(0)), 0).zw;");
		make_brush_tilt_stretch(shader, "dab_tilt", "dab_pa", "dab_ba");
	}
	node_shader_write_frag(shader, "float dab_h = clamp(dot(dab_pa, dab_ba) / max(dot(dab_ba, dab_ba), 0.00000001), 0.0, 1.0);");
	node_shader_write_frag(shader, "float dab_dist = length(dab_pa - dab_ba * dab_h);");
}

// Before the material: texels no dab reaches are dropped
void make_brush_run_batched(node_shader_t *shader, bool depth_reject) {
	node_shader_write_frag(shader, "float dist = 0.0;");
	shader->frag_wposition = true;
	node_shader_add_constant(shader, "mat4 invVP", "_inv_view_proj_matrix");
	node_shader_add_constant(shader, "float paint_dab_count", "_paint_dab_count");
	node_shader_add_texture(shader, "texpaintdabs", "_texpaintdabs");
	if (depth_reject) {
		make_brush_depth_reject(shader);
	}
	node_shader_write_frag(shader, "int dab_count = int(constants.paint_dab_count);");
	node_shader_write_frag(shader, "float dab_near = 0.0;");
	node_shader_write_frag(shader, "for (int k = 0; k < dab_count; k += 1) {");
	make_brush_dab_dist(shader);
	node_shader_write_frag(shader, "if (dab_dist <= dab1.x) { dab_near = 1.0; }");
	node_shader_write_frag(shader, "}");
	node_shader_write_frag(shader, "if (dab_near == 0.0) { discard; }");
}

// After the material: the stroke coverage, from the previous coverage cov and the dabs in order
void make_brush_run_batched_coverage(node_shader_t *shader) {
	bool angle_reject = g_config->brush_angle_reject && !g_project->_->translucency;
	bool mask         = g_context->brush_mask_image != NULL;
	if (angle_reject || mask) {
		node_shader_add_function(shader, str_octahedron_wrap);
		node_shader_add_texture(shader, "gbuffer0", NULL);
	}
	if (mask) {
		node_shader_add_texture(shader, "texbrushmask", "_texbrushmask");
		node_shader_add_constant(shader, "vec3 camera_right", "_camera_right");
	}
	if (angle_reject) {
		shader->frag_n = true;
	}
	node_shader_write_frag(shader, "float dab_touched = 0.0;");
	node_shader_write_frag(shader, "for (int k = 0; k < dab_count; k += 1) {");
	make_brush_dab_dist(shader);
	node_shader_write_frag(shader, "vec4 dab2 = texelFetch(sampler2D(texpaintdabs, sampler_linear), ivec2(uint(k * 3 + 2), uint(0)), 0);");
	node_shader_write_frag(shader, "if (dab_dist <= dab1.x) {");
	node_shader_write_frag(shader, "float dab_op = opacity * dab1.y;");
	if (angle_reject || mask) { // Normal of the surface under the dab
		node_shader_write_frag(shader, "vec2 dg0 = textureLod(sampler2D(gbuffer0, sampler_linear), dab0.xy, 0.0).rg;");
		node_shader_write_frag(shader, "vec3 dwn;");
		node_shader_write_frag(shader, "dwn.z = 1.0 - abs(dg0.x) - abs(dg0.y);");
		node_shader_write_frag(shader, "if (dwn.z >= 0.0) { dwn.x = dg0.x; dwn.y = dg0.y; } else { vec2 df2 = octahedron_wrap(dg0.xy); dwn.x = df2.x; dwn.y = df2.y; }");
		node_shader_write_frag(shader, "dwn = normalize(dwn);");
	}
	if (angle_reject) {
		f32 angle = g_context->brush_angle_reject_dot;
		node_shader_write_frag(shader, string_tmp("if (dot(dwn, dab_winp.xyz - input.wposition) < -0.03 || dot(dwn, n) < %s) { dab_op = 0.0; }",
		                                        f32_to_string(angle)));
	}
	if (mask) { // Tip projected onto the surface, turned by the brush and dab angle
		node_shader_write_frag(shader, "vec3 dmr = constants.camera_right;");
		node_shader_write_frag(shader, "if (abs(dot(dwn, dmr)) > 0.999) { dmr = vec3(0.0, 0.0, 1.0); }");
		node_shader_write_frag(shader, "vec3 dmt = normalize(dmr - dwn * dot(dmr, dwn));");
		node_shader_write_frag(shader, "vec3 dmb = cross(dmt, dwn);");
		node_shader_write_frag(shader, "vec2 dpm = vec2(dot(dab_pa, dmt), dot(dab_pa, dmb));");
		node_shader_write_frag(shader, "dpm = vec2(dpm.x * dab2.x - dpm.y * dab2.y, dpm.x * dab2.y + dpm.y * dab2.x);");
		node_shader_write_frag(shader, "dpm = dpm / dab1.x * 0.5 + 0.5;");
		node_shader_write_frag(shader, "vec4 dms = textureLod(sampler2D(texbrushmask, sampler_linear), dpm, 0.0);");
		node_shader_write_frag(shader, g_context->brush_mask_image_is_alpha ? "dab_op *= dms.a;" : "dab_op *= dms.r * dms.a;");
	}
	node_shader_write_frag(shader, string_tmp("if (dab_op > float(%s)) {", f32_to_string(g_config->brush_alpha_discard)));
	node_shader_write_frag(shader, "float dt = dab_dist / dab1.x;");
	node_shader_write_frag(shader, "float dt2 = clamp((dt - dab1.z) / max(1.0 - dab1.z, 0.001), 0.0, 1.0);");
	node_shader_write_frag(shader, "float ds = (1.0 - dt2 * dt2 * (3.0 - 2.0 * dt2)) * dab_op;");
	node_shader_write_frag(shader, "float dbase = max(ds, cov);");
	node_shader_write_frag(shader, "if (dab1.w < 1.0) { cov = min(cov + ds * dab1.w, dbase); } else { cov = dbase + ds * (1.0 - dbase) * 0.1; }");
	node_shader_write_frag(shader, "dab_touched = 1.0;");
	node_shader_write_frag(shader, "}");
	node_shader_write_frag(shader, "}");
	node_shader_write_frag(shader, "}");
	node_shader_write_frag(shader, "if (dab_touched == 0.0) { discard; }");
}
