
#include "../global.h"

i32 util_encode_mesh_data_size(mesh_data_t_array_t *datas) {
	if (datas == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < datas->length; ++i) {
		for (i32 j = 0; j < datas->buffer[i]->vertex_arrays->length; ++j) {
			size += datas->buffer[i]->vertex_arrays->buffer[j]->values->length * 2;
		}
		size += datas->buffer[i]->index_array->length * 4;
	}
	return size;
}

void util_encode_mesh_datas(mesh_data_t_array_t *datas) {
	armpack_encode_string("mesh_datas");
	if (datas == NULL) {
		armpack_encode_null();
		return;
	}

	armpack_encode_array(datas->length);
	for (i32 i = 0; i < datas->length; ++i) {
		armpack_encode_map(5);
		armpack_encode_string("name");
		armpack_encode_string(datas->buffer[i]->name);
		armpack_encode_string("scale_pos");
		armpack_encode_f32(datas->buffer[i]->scale_pos);
		armpack_encode_string("scale_tex");
		armpack_encode_f32(datas->buffer[i]->scale_tex);
		armpack_encode_string("vertex_arrays");
		armpack_encode_array(datas->buffer[i]->vertex_arrays->length);
		for (i32 j = 0; j < datas->buffer[i]->vertex_arrays->length; ++j) {
			armpack_encode_map(3);
			armpack_encode_string("attrib");
			armpack_encode_string(datas->buffer[i]->vertex_arrays->buffer[j]->attrib);
			armpack_encode_string("data");
			armpack_encode_string(datas->buffer[i]->vertex_arrays->buffer[j]->data);
			armpack_encode_string("values");
			armpack_encode_array_i16(datas->buffer[i]->vertex_arrays->buffer[j]->values);
		}
		armpack_encode_string("index_array");
		armpack_encode_array_i32(datas->buffer[i]->index_array);
	}
}

buffer_t *util_encode_scene(scene_t *raw) {
	i32       size    = 8 * 1024 * 1024 + util_encode_mesh_data_size(raw->mesh_datas);
	buffer_t *encoded = buffer_create(size);
	armpack_encode_start(encoded->buffer);
	armpack_encode_map(9);
	armpack_encode_string("name");
	armpack_encode_null();
	armpack_encode_string("objects");
	armpack_encode_null();
	util_encode_mesh_datas(raw->mesh_datas);
	armpack_encode_string("camera_datas");
	armpack_encode_null();
	armpack_encode_string("camera_ref");
	armpack_encode_null();
	armpack_encode_string("material_datas");
	armpack_encode_null();
	armpack_encode_string("shader_datas");
	armpack_encode_null();
	armpack_encode_string("world_datas");
	armpack_encode_null();
	armpack_encode_string("world_ref");
	armpack_encode_null();
	i32 ei          = armpack_encode_end();
	encoded->length = ei;
	return encoded;
}

// ---- Projects ----
// Project files (and material, brush and swatches files, projects without layers) as armpack maps of these fields,
// decoded by key (armpack_schema_t): a field added, removed or moved needs no version bump. A field whose meaning
// changes does: the step that reads the older meaning goes into import_arm_upgrade (io/import_arm.c).

static const armpack_field_t util_packed_asset_fields[] = {
    ARMPACK_FIELD(packed_asset_t, name, ARMPACK_STRING),
    ARMPACK_FIELD(packed_asset_t, bytes, ARMPACK_U8_ARRAY),
};
static const armpack_schema_t util_packed_asset_schema = ARMPACK_SCHEMA(packed_asset_t, util_packed_asset_fields);

static const armpack_field_t util_swatch_fields[] = {
    ARMPACK_FIELD(swatch_color_t, base, ARMPACK_I32),
    ARMPACK_FIELD(swatch_color_t, opacity, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(swatch_color_t, occlusion, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(swatch_color_t, roughness, ARMPACK_F32),
    ARMPACK_FIELD(swatch_color_t, metallic, ARMPACK_F32),
    ARMPACK_FIELD(swatch_color_t, normal, ARMPACK_I32),
    ARMPACK_FIELD(swatch_color_t, emission, ARMPACK_F32),
    ARMPACK_FIELD(swatch_color_t, height, ARMPACK_F32),
    ARMPACK_FIELD(swatch_color_t, subsurface, ARMPACK_F32),
};
static const armpack_schema_t util_swatch_schema = ARMPACK_SCHEMA(swatch_color_t, util_swatch_fields);

static const armpack_field_t util_material_data_fields[] = {
    ARMPACK_FIELD(material_data2_t, paint_base, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_opac, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_occ, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_rough, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_met, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_nor, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_height, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_emis, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, paint_subs, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(material_data2_t, uv_scale, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(material_data2_t, uv_angle, ARMPACK_F32),
    ARMPACK_FIELD(material_data2_t, uid, ARMPACK_I32),
};
static const armpack_schema_t util_material_data_schema = ARMPACK_SCHEMA(material_data2_t, util_material_data_fields);

static const armpack_field_t util_layer_data_fields[] = {
    ARMPACK_FIELD(layer_data_t, name, ARMPACK_STRING),
    ARMPACK_FIELD(layer_data_t, type, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, res_x, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, res_y, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, bpp, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, decal_mat, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, opacity_mask, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(layer_data_t, fill, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, object_mask, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, blending, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, parent, ARMPACK_I32, .def = -1),
    ARMPACK_FIELD(layer_data_t, visible, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_base, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_opac, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_occ, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_rough, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_met, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_nor, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_nor_blend, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_height, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_height_blend, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, height_raise, ARMPACK_F32),
    ARMPACK_FIELD(layer_data_t, paint_emis, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, paint_subs, ARMPACK_BOOL, .def = 1),
    ARMPACK_FIELD(layer_data_t, uv_map, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, path_points, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, path_points_world, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, path_points_camera, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, path_points_parent, ARMPACK_I32_ARRAY),
    ARMPACK_FIELD(layer_data_t, path_tool, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, path_curved, ARMPACK_BOOL),
    ARMPACK_FIELD(layer_data_t, path_material, ARMPACK_I32, .def = -1),
    ARMPACK_FIELD(layer_data_t, path_text, ARMPACK_BOOL),
    ARMPACK_FIELD(layer_data_t, mask_effects, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(layer_data_t, uid, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, timeline, ARMPACK_I32),
    ARMPACK_FIELD(layer_data_t, tilemap, ARMPACK_U8_ARRAY),
};
static const armpack_schema_t util_layer_data_schema = ARMPACK_SCHEMA(layer_data_t, util_layer_data_fields);

static const armpack_field_t util_vertex_array_fields[] = {
    ARMPACK_FIELD(vertex_array_t, attrib, ARMPACK_STRING),
    ARMPACK_FIELD(vertex_array_t, data, ARMPACK_STRING),
    ARMPACK_FIELD(vertex_array_t, values, ARMPACK_I16_ARRAY),
};
static const armpack_schema_t util_vertex_array_schema = ARMPACK_SCHEMA(vertex_array_t, util_vertex_array_fields);

static const armpack_field_t util_mesh_data_fields[] = {
    ARMPACK_FIELD(mesh_data_t, name, ARMPACK_STRING),
    ARMPACK_FIELD(mesh_data_t, scale_pos, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(mesh_data_t, scale_tex, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(mesh_data_t, vertex_arrays, ARMPACK_STRUCTS, .schema = &util_vertex_array_schema),
    ARMPACK_FIELD(mesh_data_t, index_array, ARMPACK_I32_ARRAY),
};
static const armpack_schema_t util_mesh_data_schema = ARMPACK_SCHEMA(mesh_data_t, util_mesh_data_fields);

static const armpack_field_t util_project_fields[] = {
    ARMPACK_FIELD(project_t, version, ARMPACK_STRING), // First: import_arm_version reads it at a fixed place
    ARMPACK_FIELD(project_t, assets, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, is_bgra, ARMPACK_BOOL),
    ARMPACK_FIELD(project_t, packed_assets, ARMPACK_STRUCTS, .schema = &util_packed_asset_schema),
    ARMPACK_FIELD(project_t, envmap, ARMPACK_STRING),
    ARMPACK_FIELD(project_t, envmap_strength, ARMPACK_F32, .def = 1.0f),
    ARMPACK_FIELD(project_t, envmap_angle, ARMPACK_F32),
    ARMPACK_FIELD(project_t, envmap_blur, ARMPACK_BOOL),
    ARMPACK_FIELD(project_t, camera_world, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(project_t, camera_origin, ARMPACK_F32_ARRAY),
    ARMPACK_FIELD(project_t, camera_fov, ARMPACK_F32),
    ARMPACK_FIELD(project_t, camera_near, ARMPACK_F32),
    ARMPACK_FIELD(project_t, camera_far, ARMPACK_F32),
    ARMPACK_FIELD(project_t, camera_frustum_culling, ARMPACK_BOOL),
    ARMPACK_FIELD(project_t, swatches, ARMPACK_STRUCTS, .schema = &util_swatch_schema),
    ARMPACK_FIELD(project_t, brush_nodes, ARMPACK_STRUCTS, .schema = &ui_node_canvas_schema),
    ARMPACK_FIELD(project_t, brush_icons, ARMPACK_U8_ARRAYS),
    ARMPACK_FIELD(project_t, material_nodes, ARMPACK_STRUCTS, .schema = &ui_node_canvas_schema),
    ARMPACK_FIELD(project_t, material_groups, ARMPACK_STRUCTS, .schema = &ui_node_canvas_schema),
    ARMPACK_FIELD(project_t, material_icons, ARMPACK_U8_ARRAYS),
    ARMPACK_FIELD(project_t, material_datas, ARMPACK_STRUCTS, .schema = &util_material_data_schema),
    ARMPACK_FIELD(project_t, font_assets, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, layer_datas, ARMPACK_STRUCTS, .schema = &util_layer_data_schema),
    ARMPACK_FIELD(project_t, mesh_datas, ARMPACK_STRUCTS, .schema = &util_mesh_data_schema),
    ARMPACK_FIELD(project_t, mesh_assets, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, mesh_icons, ARMPACK_U8_ARRAYS),
    ARMPACK_FIELD(project_t, mesh_transforms, ARMPACK_F32_ARRAYS),
    ARMPACK_FIELD(project_t, mesh_parents, ARMPACK_I32_ARRAY),
    ARMPACK_FIELD(project_t, atlas_objects, ARMPACK_I32_ARRAY),
    ARMPACK_FIELD(project_t, atlas_names, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, linear_blending, ARMPACK_BOOL),
    ARMPACK_FIELD(project_t, id_map, ARMPACK_STRING),
    ARMPACK_FIELD(project_t, id_map_parts, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, mesh_maps, ARMPACK_STRING_ARRAY),
    ARMPACK_FIELD(project_t, uuid, ARMPACK_STRING),
    ARMPACK_FIELD(project_t, next_uid, ARMPACK_I32),
    ARMPACK_FIELD(project_t, timelines, ARMPACK_U8_ARRAY),
    ARMPACK_FIELD(project_t, history, ARMPACK_U8_ARRAY),
};
static const armpack_schema_t util_project_schema = ARMPACK_SCHEMA(project_t, util_project_fields);

buffer_t *util_encode_project(project_t *raw) {
	buffer_t *encoded = buffer_create(armpack_size_schema(raw, &util_project_schema));
	armpack_encode_start(encoded->buffer);
	armpack_encode_schema(raw, &util_project_schema);
	encoded->length = armpack_encode_end();
	return encoded;
}

// A project, material, brush or swatches file of any version, its fields as that version wrote them: NULL when b is
// not one. import_arm_decode reads it as this build's version.
project_t *util_decode_project(buffer_t *b) {
	return armpack_decode_schema(b, &util_project_schema);
}
