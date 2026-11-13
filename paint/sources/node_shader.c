
#include "global.h"

node_shader_t *node_shader_create(node_shader_context_t *ctx) {
	node_shader_t *raw = ALLOC_INIT(node_shader_t, {0});
	raw->context       = ctx;
	raw->ins           = any_array_create_from_raw((void *[]){}, 0);
	raw->outs          = any_array_create_from_raw((void *[]){}, 0);
	raw->frag_out      = "vec4";
	raw->consts        = any_array_create_from_raw((void *[]){}, 0);
	raw->textures      = any_array_create_from_raw((void *[]){}, 0);
	raw->functions     = any_map_create();

	string_buffer_init(&raw->vert);
	string_buffer_init(&raw->vert_end);
	string_buffer_init(&raw->vert_normal);
	string_buffer_init(&raw->vert_attribs);
	raw->vert_write_normal = 0;

	string_buffer_init(&raw->frag);
	string_buffer_init(&raw->frag_end);
	string_buffer_init(&raw->frag_normal);
	string_buffer_init(&raw->frag_attribs);
	raw->frag_write_normal = 0;

	return raw;
}

void node_shader_add_in(node_shader_t *raw, char *s) {
	any_array_push(raw->ins, s);
}

void node_shader_add_out(node_shader_t *raw, char *s) {
	any_array_push(raw->outs, s);
}

void node_shader_context_add_constant(node_shader_context_t *raw, char *ctype, char *name, char *link) {
	for (i32 i = 0; i < raw->data->constants->length; ++i) {
		shader_const_t *c = raw->data->constants->buffer[i];
		if (string_equals(c->name, name)) {
			return;
		}
	}

	shader_const_t *c = ALLOC_INIT(shader_const_t, {.name = name, .type = ctype});
	if (link != NULL) {
		c->link = string_copy(link);
	}
	shader_const_t_array_t *consts = raw->data->constants;
	any_array_push(consts, c);
}

// GLSL type of a declaration -> engine constant type (shader_context_type_size)
char *node_shader_engine_type(char *t) {
	if (string_equals(t, "vec2")) return "float2";
	if (string_equals(t, "vec3")) return "float3";
	if (string_equals(t, "vec4")) return "float4";
	if (string_equals(t, "mat3")) return "float3x3";
	if (string_equals(t, "mat4")) return "float4x4";
	return t; // float, int
}

void node_shader_add_constant(node_shader_t *raw, char *s, char *link) {
	// float4 inp
	if (string_array_index_of(raw->consts, s) == -1) {
		string_array_t *ar    = string_split(s, " ");
		char           *utype = ar->buffer[0];
		char           *uname = ar->buffer[1];
		any_array_push(raw->consts, s);
		// The context owns the type name, freed with it
		char *etype = node_shader_engine_type(utype);
		if (etype != utype) {
			free(utype);
			utype = string_copy(etype);
		}
		node_shader_context_add_constant(raw->context, utype, uname, link);
		// The context keeps uname / utype
		array_free(ar);
		free(ar);
	}
}

void node_shader_context_add_texture_unit(node_shader_context_t *raw, char *name, char *link) {
	for (i32 i = 0; i < raw->data->texture_units->length; ++i) {
		tex_unit_t *c = raw->data->texture_units->buffer[i];
		if (string_equals(c->name, name)) {
			return;
		}
	}

	tex_unit_t *c = ALLOC_INIT(tex_unit_t, {.name = string_copy(name), .link = string_copy(link)});
	any_array_push(raw->data->texture_units, c);
}

void node_shader_add_texture(node_shader_t *raw, char *name, char *link) {
	if (string_array_index_of(raw->textures, name) == -1) {
		any_array_push(raw->textures, name);
		node_shader_context_add_texture_unit(raw->context, name, link);
	}
}

void node_shader_add_function(node_shader_t *raw, char *s) {
	string_array_t *ar    = string_split(s, "(");
	char           *fname = ar->buffer[0];
	for (i32 i = 1; i < ar->length; ++i) {
		free(ar->buffer[i]);
	}
	array_free(ar);
	free(ar);
	if (any_map_get(raw->functions, fname) != NULL) {
		free(fname);
		return;
	}
	any_map_set(raw->functions, fname, s);
}

static void node_shader_write_line(buffer_t *sb, char *s) {
	string_buffer_append(sb, s);
	string_buffer_append(sb, "\n");
}

void node_shader_write_vert(node_shader_t *raw, char *s) {
	if (raw->vert_write_normal > 0) {
		node_shader_write_line(&raw->vert_normal, s);
	}
	else {
		node_shader_write_line(&raw->vert, s);
	}
}

void node_shader_write_end_vert(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->vert_end, s);
}

void node_shader_write_attrib_vert(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->vert_attribs, s);
}

void node_shader_write_frag(node_shader_t *raw, char *s) {
	if (raw->frag_write_normal > 0) {
		node_shader_write_line(&raw->frag_normal, s);
	}
	else {
		node_shader_write_line(&raw->frag, s);
	}
}

void node_shader_write_attrib_frag(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->frag_attribs, s);
}

char *node_shader_data_size(node_shader_t *raw, char *data) {
	if (string_equals(data, "float1")) {
		return "1";
	}
	else if (string_equals(data, "float2") || string_equals(data, "short2norm")) {
		return "2";
	}
	else if (string_equals(data, "float3")) {
		return "3";
	}
	else { // float4 || short4norm
		return "4";
	}
}

// Generator code convention (unchanged call sites): vertex code reads input.<attribute> and writes output.<varying> /
// output.pos, fragment code reads input.<varying> and writes output (output[i] with several targets). Here these
// become in_<attribute>, vout.<varying>, gl_Position, vin.<varying> and frag_out.

static char *node_shader_vec(char *data) {
	char *n = node_shader_data_size(NULL, data);
	return string_equals(n, "1") ? "float" : string_tmp("vec%s", n);
}

// Replaces occurrences of from that start a word (and end one, when from ends with a letter)
void node_shader_rename(buffer_t *out, char *code, char *from, char *to) {
	i32   n = string_length(from);
	char *s = code;
	while (*s) {
		char *hit = strstr(s, from);
		if (hit == NULL) {
			string_buffer_append(out, s);
			return;
		}
		bool word_start = hit == code || !(isalnum((unsigned char)hit[-1]) || hit[-1] == '_' || hit[-1] == '.');
		bool word_end   = !(isalnum((unsigned char)from[n - 1]) || from[n - 1] == '_') || !(isalnum((unsigned char)hit[n]) || hit[n] == '_');
		string_buffer_append_n(out, s, hit - s);
		string_buffer_append(out, word_start && word_end ? to : from);
		s = hit + n;
	}
}

static void node_shader_stage_code(buffer_t *sb, char *code, bool vert) {
	static buffer_t a, b;
	static bool     init = false;
	if (!init) {
		string_buffer_init(&a);
		string_buffer_init(&b);
		init = true;
	}
	string_buffer_reset(&a);
	string_buffer_reset(&b);
	if (vert) {
		node_shader_rename(&a, code, "output.pos", "gl_Position");
		node_shader_rename(&b, string_buffer_get(&a), "output", "vout");
		node_shader_rename(sb, string_buffer_get(&b), "input.", "in_"); // input.pos -> in_pos
		return;
	}
	node_shader_rename(&a, code, "input", "vin");
	node_shader_rename(sb, string_buffer_get(&a), "output", "frag_out");
}

// Prototypes of the functions in a function string (several functions, often on one line), so functions can call each
// other in any order: at brace depth 0, the text before a { that has a ( is a function signature
static void node_shader_prototypes(buffer_t *sb, char *f) {
	i32   depth = 0;
	char *start = f; // After the last top-level } or ;
	for (char *c = f; *c; ++c) {
		if (*c == '{') {
			if (depth == 0) {
				char *sig = start;
				while (sig < c && isspace((unsigned char)*sig)) {
					sig++;
				}
				char *close = c;
				while (close > sig && *close != ')') {
					close--;
				}
				bool is_fn = close > sig && memchr(sig, '(', close - sig) != NULL && strncmp(sig, "struct", 6) != 0;
				if (is_fn) {
					string_buffer_append_n(sb, sig, (i32)(close - sig + 1));
					string_buffer_append(sb, ";\n");
				}
			}
			depth++;
		}
		else if (*c == '}') {
			depth--;
			if (depth == 0) {
				start = c + 1;
			}
		}
		else if (*c == ';' && depth == 0) {
			start = c + 1;
		}
	}
}

char *node_shader_get(node_shader_t *raw) {
	static buffer_t out;
	static bool     out_first = true;
	if (out_first) {
		string_buffer_init(&out);
		out_first = false;
	}
	string_buffer_reset(&out);
	buffer_t *sb = &out;

	string_buffer_append(sb, "#version 450\n\n");

	// Constants, offsets as the engine computes them (shader_context_bind_constants)
	// Always declared: the argument buffer layout has no gaps (iron_glsl.h)
	{
		string_buffer_append(sb, "layout(set = 0, binding = 0, std140) uniform Constants {\n");
		if (raw->consts->length == 0) {
			string_buffer_append(sb, "\tvec4 empty;\n");
		}
		i32 offset = 0;
		for (i32 i = 0; i < raw->consts->length; ++i) {
			char           *decl = raw->consts->buffer[i];
			string_array_t *ar   = string_split(decl, " ");
			i32             size = shader_context_type_size(node_shader_engine_type(ar->buffer[0]));
			offset += shader_context_type_pad(offset, size);
			string_buffer_append(sb, string_tmp("\tlayout(offset = %d) %s;\n", offset, decl));
			offset += size;
			array_free(ar);
			free(ar);
		}
		string_buffer_append(sb, "} constants;\n\n");
	}
	string_buffer_append(sb, "layout(set = 0, binding = 1) uniform sampler sampler_linear;\n");
	for (i32 i = 0; i < raw->textures->length; ++i) {
		string_buffer_append(sb, string_tmp("layout(set = 0, binding = %d) uniform texture2D %s;\n", 2 + i, (char *)raw->textures->buffer[i]));
	}
	string_buffer_append(sb, "\n");

	// Functions with derivatives exist in the fragment stage only
	string_array_t *keys = map_keys(raw->functions);
	for (i32 pass = 0; pass < 2; ++pass) {
		for (i32 i = 0; i < keys->length; ++i) {
			char *f    = any_map_get(raw->functions, keys->buffer[i]);
			bool  frag = strstr(f, "dFdx") != NULL || strstr(f, "dFdy") != NULL || strstr(f, "fwidth") != NULL;
			if (frag) {
				string_buffer_append(sb, "#ifdef FRAG\n");
			}
			if (pass == 0) {
				node_shader_prototypes(sb, f);
			}
			else {
				string_buffer_append(sb, f);
				string_buffer_append(sb, "\n");
			}
			if (frag) {
				string_buffer_append(sb, "#endif\n");
			}
		}
		string_buffer_append(sb, "\n");
	}
	array_free(keys);
	free(keys);

	// Vertex stage
	string_buffer_append(sb, "#ifdef VERT\n");
	vertex_element_t_array_t *vs = raw->context->data->vertex_elements;
	for (i32 i = 0; i < vs->length; ++i) {
		vertex_element_t *e = vs->buffer[i];
		string_buffer_append(sb, string_tmp("layout(location = %d) in %s in_%s;\n", i, node_shader_vec(e->data), e->name));
	}
	if (raw->outs->length > 0) {
		string_buffer_append(sb, "layout(location = 0) out Varyings {\n");
		for (i32 i = 0; i < raw->outs->length; ++i) {
			string_buffer_append(sb, string_tmp("\t%s;\n", (char *)raw->outs->buffer[i]));
		}
		string_buffer_append(sb, "} vout;\n");
	}
	string_buffer_append(sb, "\nvoid main() {\n");
	node_shader_stage_code(sb, string_buffer_get(&raw->vert_attribs), true);
	node_shader_stage_code(sb, string_buffer_get(&raw->vert_normal), true);
	node_shader_stage_code(sb, string_buffer_get(&raw->vert), true);
	node_shader_stage_code(sb, string_buffer_get(&raw->vert_end), true);
	string_buffer_append(sb, "}\n#endif\n\n");

	// Fragment stage
	string_buffer_append(sb, "#ifdef FRAG\n");
	if (raw->outs->length > 0) {
		string_buffer_append(sb, "layout(location = 0) in Varyings {\n");
		for (i32 i = 0; i < raw->outs->length; ++i) {
			string_buffer_append(sb, string_tmp("\t%s;\n", (char *)raw->outs->buffer[i]));
		}
		string_buffer_append(sb, "} vin;\n");
	}
	// "vec4" or "vec4[N]" -> out vec4 frag_out / out vec4 frag_out[N]
	i32 bracket = string_index_of(raw->frag_out, "[");
	if (bracket > -1) {
		string_buffer_append(sb, string_tmp("layout(location = 0) out %s frag_out%s;\n", substring(raw->frag_out, 0, bracket), raw->frag_out + bracket));
	}
	else {
		string_buffer_append(sb, string_tmp("layout(location = 0) out %s frag_out;\n", raw->frag_out));
	}
	string_buffer_append(sb, "\nvoid main() {\n");
	node_shader_stage_code(sb, string_buffer_get(&raw->frag_attribs), false);
	node_shader_stage_code(sb, string_buffer_get(&raw->frag_normal), false);
	node_shader_stage_code(sb, string_buffer_get(&raw->frag), false);
	node_shader_stage_code(sb, string_buffer_get(&raw->frag_end), false);
	string_buffer_append(sb, "}\n#endif\n");

	string_buffer_free(&raw->vert);
	string_buffer_free(&raw->vert_end);
	string_buffer_free(&raw->vert_normal);
	string_buffer_free(&raw->vert_attribs);
	string_buffer_free(&raw->frag);
	string_buffer_free(&raw->frag_end);
	string_buffer_free(&raw->frag_normal);
	string_buffer_free(&raw->frag_attribs);

	return string_buffer_get(&out);
}

static void node_shader_array_delete(void *a) {
	array_free(a);
	free(a);
}

void node_shader_context_free(node_shader_context_t *raw) {
	node_shader_t *shader = raw->shader;
	if (shader != NULL) {
		node_shader_array_delete(shader->ins);
		node_shader_array_delete(shader->outs);
		node_shader_array_delete(shader->consts);
		node_shader_array_delete(shader->textures);
		string_array_t *keys = shader->functions->keys;
		for (i32 i = 0; i < keys->capacity; ++i) {
			free(keys->buffer[i]);
		}
		node_shader_array_delete(shader->functions->keys);
		node_shader_array_delete(shader->functions->values);
		free(shader->functions);
		free(shader);
	}
	free(raw);
}

node_shader_context_t *node_shader_context_create(material_t *material, shader_context_t *props) {
	node_shader_context_t *raw = ALLOC_INIT(node_shader_context_t, {0});
	raw->material              = material;

	vertex_element_t_array_t *vertex_elements = props->vertex_elements;
	if (vertex_elements == NULL) {
		vertex_elements = any_array_create_from_raw(
		    (void *[]){
		        ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
		        ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
		    },
		    2);
	}

	raw->data = ALLOC_INIT(shader_context_t, {.name                    = props->name,
	                                          .depth_write             = props->depth_write,
	                                          .compare_mode            = props->compare_mode,
	                                          .cull_mode               = props->cull_mode,
	                                          .blend_source            = props->blend_source,
	                                          .blend_destination       = props->blend_destination,
	                                          .alpha_blend_source      = props->alpha_blend_source,
	                                          .alpha_blend_destination = props->alpha_blend_destination,
	                                          .fragment_shader         = "",
	                                          .vertex_shader           = "",
	                                          .vertex_elements         = vertex_elements,
	                                          .bind_textures           = any_array_create_from_raw((void *[]){}, 0),
	                                          .color_attachments       = props->color_attachments,
	                                          .depth_attachment        = props->depth_attachment});

	shader_context_t *rw = raw->data;
	rw->_                = ALLOC_INIT(shader_context_runtime_t, {0});

	if (props->color_writes_red != NULL) {
		raw->data->color_writes_red = props->color_writes_red;
	}
	if (props->color_writes_green != NULL) {
		raw->data->color_writes_green = props->color_writes_green;
	}
	if (props->color_writes_blue != NULL) {
		raw->data->color_writes_blue = props->color_writes_blue;
	}
	if (props->color_writes_alpha != NULL) {
		raw->data->color_writes_alpha = props->color_writes_alpha;
	}

	raw->data->texture_units = any_array_create_from_raw((void *[]){}, 0);
	raw->data->constants     = i32_array_create_from_raw((i32[]){}, 0);

	free(props);
	return raw;
}

void node_shader_context_add_elem(node_shader_context_t *raw, char *name, char *data_type) {
	for (i32 i = 0; i < raw->data->vertex_elements->length; ++i) {
		vertex_element_t *e = raw->data->vertex_elements->buffer[i];
		if (string_equals(e->name, name)) {
			return;
		}
	}
	vertex_element_t *elem = ALLOC_INIT(vertex_element_t, {.name = name, .data = data_type});
	any_array_push(raw->data->vertex_elements, elem);
}

bool node_shader_context_is_elem(node_shader_context_t *raw, char *name) {
	for (i32 i = 0; i < raw->data->vertex_elements->length; ++i) {
		vertex_element_t *elem = raw->data->vertex_elements->buffer[i];
		if (string_equals(elem->name, name)) {
			return true;
		}
	}
	return false;
}

node_shader_t *node_shader_context_make_shader(node_shader_context_t *raw) {
	raw->data->vertex_shader   = string_tmp("%s_%s.vert", raw->material->name, raw->data->name);
	raw->data->fragment_shader = string_tmp("%s_%s.frag", raw->material->name, raw->data->name);
	raw->shader                  = node_shader_create(raw);
	return raw->shader;
}
