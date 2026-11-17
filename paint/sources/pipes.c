
#include "global.h"

typedef enum {
	PIPES_LAYOUT_POS,         // Screen quad: pos
	PIPES_LAYOUT_POS_TEX_COL, // Draw images: pos, tex, col
	PIPES_LAYOUT_MESH,        // Meshes: pos, nor, tex
} pipes_layout_t;

// A pipeline of <name>.vert and <name>.frag, the caller sets the rest and compiles it
static gpu_pipeline_t *pipes_create(char *name, pipes_layout_t layout) {
	gpu_pipeline_t *pipe       = gpu_create_pipeline();
	pipe->vertex_shader        = sys_get_shader(string("%s.vert", name)); // Kept as the shader cache key
	pipe->fragment_shader      = sys_get_shader(string("%s.frag", name));
	gpu_vertex_structure_t *vs = ALLOC_INIT(gpu_vertex_structure_t, {0});
	if (layout == PIPES_LAYOUT_MESH) {
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
		gpu_vertex_structure_add(vs, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
		gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
	}
	else {
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
		if (layout == PIPES_LAYOUT_POS_TEX_COL) {
			gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_F32_2X);
			gpu_vertex_structure_add(vs, "col", GPU_VERTEX_DATA_F32_4X);
		}
	}
	pipe->input_layout = vs;
	return pipe;
}

// Alpha blending over the target, alpha_source and alpha_destination for the alpha channel
static void pipes_blend(gpu_pipeline_t *pipe, gpu_blend_t alpha_source, gpu_blend_t alpha_destination) {
	pipe->blend_source            = GPU_BLEND_SOURCE_ALPHA;
	pipe->blend_destination       = GPU_BLEND_INV_SOURCE_ALPHA;
	pipe->alpha_blend_source      = alpha_source;
	pipe->alpha_blend_destination = alpha_destination;
}

// A copy pipeline into a target of one format
static gpu_pipeline_t *pipes_make_copy(gpu_texture_format_t format) {
	gpu_pipeline_t *pipe         = pipes_create("layer_copy", PIPES_LAYOUT_POS_TEX_COL);
	pipe->color_attachment_count = 1;
	pipe->color_attachment[0]    = format;
	gpu_pipeline_compile(pipe);
	return pipe;
}

gpu_pipeline_t *_pipes_make_merge(bool red, bool green, bool blue, bool alpha) {
	gpu_pipeline_t *pipe            = pipes_create("layer_merge", PIPES_LAYOUT_POS);
	pipe->color_write_mask_red[0]   = red;
	pipe->color_write_mask_green[0] = green;
	pipe->color_write_mask_blue[0]  = blue;
	pipe->color_write_mask_alpha[0] = alpha;
	gpu_pipeline_compile(pipe);
	return pipe;
}

gpu_pipeline_t *pipes_get_merge_r() {
	if (pipes_merge_r == NULL) {
		pipes_merge_r = _pipes_make_merge(true, false, false, false);
	}
	return pipes_merge_r;
}

gpu_pipeline_t *pipes_get_merge_g() {
	if (pipes_merge_g == NULL) {
		pipes_merge_g = _pipes_make_merge(false, true, false, false);
	}
	return pipes_merge_g;
}

gpu_pipeline_t *pipes_get_merge_b() {
	if (pipes_merge_b == NULL) {
		pipes_merge_b = _pipes_make_merge(false, false, true, false);
	}
	return pipes_merge_b;
}

gpu_pipeline_t *pipes_get_merge_a() {
	if (pipes_merge_a == NULL) {
		pipes_merge_a = _pipes_make_merge(false, false, false, true);
	}
	return pipes_merge_a;
}

void pipes_init() {
	pipes_merge    = _pipes_make_merge(true, true, true, true);
	pipes_tex0     = 0; // Always binding texpaint.a for blending
	pipes_tex1     = 1;
	pipes_texmask  = 2;
	pipes_texa     = 3;
	pipes_offset   = 0;
	pipes_opac     = pipes_get_constant_location("float");
	pipes_blending = pipes_get_constant_location("int");
	pipes_tex1w    = pipes_get_constant_location("float");

	pipes_copy = pipes_create("layer_copy", PIPES_LAYOUT_POS_TEX_COL);
	gpu_pipeline_compile(pipes_copy);

	pipes_copy_bgra = pipes_create("layer_copy_bgra", PIPES_LAYOUT_POS_TEX_COL);
	gpu_pipeline_compile(pipes_copy_bgra);

	pipes_copy8   = pipes_make_copy(GPU_TEXTURE_FORMAT_R8);
	pipes_copy64  = pipes_make_copy(GPU_TEXTURE_FORMAT_RGBA64);
	pipes_copy128 = pipes_make_copy(GPU_TEXTURE_FORMAT_RGBA128);

	pipes_invert_mask = pipes_create("layer_invert", PIPES_LAYOUT_POS_TEX_COL);
	gpu_pipeline_compile(pipes_invert_mask);

	pipes_apply_mask = pipes_create("mask_apply", PIPES_LAYOUT_POS);
	gpu_pipeline_compile(pipes_apply_mask);
	pipes_tex0_mask       = 0;
	pipes_texa_mask       = 1;
	pipes_offset          = 0;
	pipes_opac_apply_mask = pipes_get_constant_location("float");

	pipes_merge_mask = pipes_create("mask_merge", PIPES_LAYOUT_POS);
	gpu_pipeline_compile(pipes_merge_mask);
	pipes_tex0_merge_mask     = 0;
	pipes_texa_merge_mask     = 1;
	pipes_offset              = 0;
	pipes_opac_merge_mask     = pipes_get_constant_location("float");
	pipes_blending_merge_mask = pipes_get_constant_location("int");

	pipes_colorid_to_mask = pipes_create("mask_colorid", PIPES_LAYOUT_POS);
	gpu_pipeline_compile(pipes_colorid_to_mask);
	pipes_texpaint_colorid = 0;
	pipes_tex_colorid      = 1;
	pipes_offset           = 0;
	pipes_size_colorid     = pipes_get_constant_location("float2");

	pipes_mask_stack = pipes_create("mask_stack", PIPES_LAYOUT_POS);
	gpu_pipeline_compile(pipes_mask_stack);
	pipes_texa_mask_stack     = 0;
	pipes_texsrc_mask_stack   = 1;
	pipes_offset              = 0;
	pipes_type_mask_stack     = pipes_get_constant_location("int");
	pipes_blending_mask_stack = pipes_get_constant_location("int");
	pipes_opac_mask_stack     = pipes_get_constant_location("float");
	pipes_params0_mask_stack  = pipes_get_constant_location("float4");
	pipes_params1_mask_stack  = pipes_get_constant_location("float4");
	pipes_params2_mask_stack  = pipes_get_constant_location("float4");
	pipes_params3_mask_stack  = pipes_get_constant_location("float4");
	pipes_src_size_mask_stack = pipes_get_constant_location("float2");

	pipes_copy_rgb                            = pipes_create("layer_copy", PIPES_LAYOUT_POS_TEX_COL);
	pipes_copy_rgb->color_write_mask_alpha[0] = false;
	gpu_pipeline_compile(pipes_copy_rgb);

	pipes_cursor = pipes_create("cursor", PIPES_LAYOUT_MESH);
	pipes_blend(pipes_cursor, GPU_BLEND_ONE, GPU_BLEND_INV_SOURCE_ALPHA);
	pipes_cursor->depth_write = false;
	pipes_cursor->depth_mode  = GPU_COMPARE_MODE_ALWAYS;
	gpu_pipeline_compile(pipes_cursor);
	pipes_offset              = 0;
	pipes_cursor_vp           = pipes_get_constant_location("float4x4");
	pipes_cursor_inv_vp       = pipes_get_constant_location("float4x4");
	pipes_cursor_mouse        = pipes_get_constant_location("float2");
	pipes_cursor_tex_step     = pipes_get_constant_location("float2");
	pipes_cursor_radius       = pipes_get_constant_location("float");
	pipes_cursor_camera_right = pipes_get_constant_location("float3");
	pipes_cursor_tint         = pipes_get_constant_location("float3");
	pipes_cursor_camera_up    = pipes_get_constant_location("float3");
	pipes_cursor_camera_align = pipes_get_constant_location("float");
	pipes_cursor_gbufferd     = 0;

	pipes_cursor_decal = pipes_create("cursor_decal", PIPES_LAYOUT_MESH);
	pipes_blend(pipes_cursor_decal, GPU_BLEND_ONE, GPU_BLEND_INV_SOURCE_ALPHA);
	pipes_cursor_decal->depth_write = false;
	pipes_cursor_decal->depth_mode  = GPU_COMPARE_MODE_ALWAYS;
	gpu_pipeline_compile(pipes_cursor_decal);
	pipes_offset                    = 0;
	pipes_cursor_decal_vp           = pipes_get_constant_location("float4x4");
	pipes_cursor_decal_inv_vp       = pipes_get_constant_location("float4x4");
	pipes_cursor_decal_mouse        = pipes_get_constant_location("float2");
	pipes_cursor_decal_tex_step     = pipes_get_constant_location("float2");
	pipes_cursor_decal_radius       = pipes_get_constant_location("float");
	pipes_cursor_decal_camera_right = pipes_get_constant_location("float3");
	pipes_cursor_decal_opacity      = pipes_get_constant_location("float");
	pipes_cursor_decal_angle        = pipes_get_constant_location("float2");
	pipes_cursor_decal_scale_x      = pipes_get_constant_location("float");
	pipes_cursor_decal_camera_up    = pipes_get_constant_location("float3");
	pipes_cursor_decal_camera_align = pipes_get_constant_location("float");
	pipes_cursor_decal_gbufferd     = 0;
	pipes_cursor_decal_texdecal     = 1;
	pipes_cursor_decal_gbuffer0     = 2;

	pipes_cursor_2d = pipes_create("cursor_2d", PIPES_LAYOUT_POS_TEX_COL);
	pipes_blend(pipes_cursor_2d, GPU_BLEND_ONE, GPU_BLEND_INV_SOURCE_ALPHA);
	gpu_pipeline_compile(pipes_cursor_2d);
	pipes_offset           = 0;
	pipes_cursor_2d_center = pipes_get_constant_location("float2");
	pipes_cursor_2d_size   = pipes_get_constant_location("float2");
	pipes_cursor_2d_radius = pipes_get_constant_location("float");
	pipes_cursor_2d_width  = pipes_get_constant_location("float");

	pipes_outline = pipes_create("outline", PIPES_LAYOUT_POS);
	pipes_blend(pipes_outline, GPU_BLEND_ZERO, GPU_BLEND_ONE);
	gpu_pipeline_compile(pipes_outline);
	pipes_offset           = 0;
	pipes_outline_color    = pipes_get_constant_location("float4");
	pipes_outline_tex_step = pipes_get_constant_location("float2");
	pipes_outline_tex      = 0;
}

i32 pipes_get_constant_location(char *type) {
	i32 size = shader_context_type_size(type);
	pipes_offset += shader_context_type_pad(pipes_offset, size);
	i32 loc = pipes_offset;
	pipes_offset += size;
	return loc;
}
