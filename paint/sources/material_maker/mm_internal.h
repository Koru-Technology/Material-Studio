#pragma once

// Internals of the Material Maker generator shared by mm_*.c (see mm.h). Mirrors Material Maker's gen_base.gd
// (ShaderCode, ShaderUniform), gen_context.gd and the generator node classes.

#include "mm.h"
#include <setjmp.h>

// Set while generating: the arena jumps there when it grows past its limit (a runaway graph)
extern jmp_buf *mm_guard;

typedef struct {
	char **items;
	int    count;
} mm_strs_t;
mm_strs_t mm_split_glsl(const char *src);          // Top-level declarations
char     *mm_glsl_declaration_name(const char *d); // The name a declaration defines, "" when none

// Port types (nodes/io_types.mmt)

typedef enum {
	MM_T_F = 0, // float, vec2 uv
	MM_T_RGB,   // vec3, vec2 uv
	MM_T_RGBA,  // vec4, vec2 uv
	MM_T_SDF2D, // float, vec2 uv
	MM_T_SDF3D, // float, vec3 p
	MM_T_SDF3DC, // vec2, vec3 p
	MM_T_TEX3D_GS, // float, vec4 p
	MM_T_TEX3D,    // vec3, vec4 p
	MM_T_V4V4,     // vec4, vec4 p
	MM_T_FILL,     // vec4, vec2 uv
	MM_T_ANY,
	MM_T_COUNT,
} mm_type_t;

typedef struct {
	const char *name;      // "f", "rgb", ...
	const char *glsl;      // float, vec3, ...
	const char *paramdefs; // "vec2 uv"
	const char *params;    // "uv"
	int         slot;
	int         nconvert;
	struct {
		mm_type_t   to;
		const char *expr; // $(value) is the value
	} convert[2];
} mm_type_info_t;

extern const mm_type_info_t mm_types[MM_T_COUNT];
mm_type_t mm_type_from_name(const char *name); // MM_T_COUNT when unknown

// Uniforms: Material Maker's parameters. The generator writes them as constants (values known when generating), except
// those listed as dynamic (an iterate buffer's iteration).

typedef enum {
	MM_U_FLOAT,
	MM_U_INT,
	MM_U_VEC2,
	MM_U_VEC4,
	MM_U_SAMPLER, // A texture: a buffer, iterate buffer, image or text node output
} mm_utype_t;

typedef struct mm_uniform {
	char      *name;
	mm_utype_t type;
	int        size;    // Array size, 0 for a single value
	double    *values;  // size==0: 1 (float/int), 2 (vec2) or 4 (vec4) values; arrays: size * components
	mm_gen_t  *texture; // MM_U_SAMPLER: the generator whose texture this is
	int        texture_output; // Which of its textures (iterate buffer: 0 final, 1 loop)
} mm_uniform_t;

typedef struct {
	char *code;
	char *source;
} mm_global_t;

typedef struct mm_code {
	mm_global_t    *globals;
	int             nglobals;
	int             cap_globals;
	mm_uniform_t   *uniforms;
	int             nuniforms;
	int             cap_uniforms;
	mm_buf_t        defs;
	mm_buf_t        code;
	mm_type_t       output_type;            // MM_T_COUNT: none
	char           *output_values[MM_T_COUNT]; // Expression for each type, NULL when not available
	struct mm_code *alias;
} mm_code_t;

mm_code_t *mm_code_new(void);
mm_code_t *mm_code_default(void); // Material Maker's get_default_generated_shader
void       mm_code_add_global(mm_code_t *c, const char *code, const char *source);
void       mm_code_add_globals(mm_code_t *c, mm_code_t *from);
mm_uniform_t *mm_code_add_uniform(mm_code_t *c, const char *name, mm_utype_t type, int size, const double *values, int nvalues);
void       mm_code_add_uniforms(mm_code_t *c, mm_code_t *from);
char      *mm_code_globals_string(mm_code_t *c, const char *used_by); // get_globals_string: declarations the code uses
char      *mm_uniform_declaration(mm_uniform_t *u);                  // "const float name = 0.5;\n" (or the array form)
char      *mm_uniform_runtime_declaration(mm_uniform_t *u, const char *zero); // "#define name (0.5+zero)": not folded

// Generator context (gen_context.gd): which variants of each generator were emitted

typedef struct mm_context mm_context_t;
mm_context_t *mm_context_new(mm_context_t *parent);
bool          mm_context_has_variant(mm_context_t *c, mm_gen_t *g);
int           mm_context_get_variant(mm_context_t *c, mm_gen_t *g, const char *variant); // -1 the first time
bool          mm_context_has_parent(mm_context_t *c);                                    // A function's context
mm_context_t *mm_context_parent(mm_context_t *c);
mm_context_t *mm_context_copy(mm_context_t *c); // A snapshot of its variants

// Embedding (mm_embed.c)
mm_gen_t  *mm_create_gen(mm_json_t *data, mm_gen_t *parent);           // loader.gd create_gen (mm_graph.c)
mm_code_t *mm_native_code(mm_gen_t *g, const char *uv, mm_context_t *ctx); // MM_G_NATIVE code

// Generators

typedef enum {
	MM_G_SHADER, // A shader_model node (library node or inline), also the material node and Easy SDF
	MM_G_GRAPH,
	MM_G_IOS,
	MM_G_REMOTE,
	MM_G_SWITCH,
	MM_G_BUFFER,
	MM_G_ITERATE,
	MM_G_REROUTE,
	MM_G_PORTAL,
	MM_G_IMAGE,
	MM_G_TEXT,
	MM_G_OTHER, // comment, export, debug: no shader code
	MM_G_NATIVE, // A node of the embedding app (mm_graph_add_native): its code comes from the native code callback
	MM_G_HOIST,  // Made by mm_plan: an iterate buffer loop's input that does not change between iterations, rendered once
	             // into its own pass and read back exactly (texelFetch of an f32 texture) by the loop
} mm_kind_t;

struct mm_gen {
	mm_kind_t  kind;
	int        id;       // Material Maker's instance id: names are o<id>_...
	char      *name;     // Name in its graph, connections refer to it
	char      *type;     // Node type as saved (library name, "shader", "graph", ...)
	mm_gen_t  *parent;   // The graph it is in, NULL for the top level
	double     pos_x, pos_y;
	double     seed_value;
	bool       seed_locked;
	mm_json_t *params;   // Parameter values by name (object)
	mm_json_t *data;     // The saved node

	// Shader
	mm_json_t *model;      // shader_model as defined
	mm_json_t *inputs;     // Preprocessed (generic expanded, instance functions fixed)
	mm_json_t *outputs;
	mm_json_t *param_defs;
	char      *code;
	char      *instance;
	char      *global;
	mm_json_t *includes;
	bool       model_uses_seed;
	bool       params_use_seed;
	int        generic_size;

	// Graph
	mm_gen_t **children;
	int        nchildren;
	mm_json_t *connections; // [{from, from_port, to, to_port}]
	bool       transmits_seed;

	// Ios
	mm_json_t *ports;

	// Remote
	mm_json_t *widgets;

	// Buffer / iterate buffer
	int  version;
	bool greyscale;
	int  pass;      // Pass rendering it (mm_plan), -1 before planning
	int  analyzed;  // mm_buffer_analyze: 0 not yet, 1 in progress, 2 done

	// Material
	bool is_material;

	// Native (MM_G_NATIVE)
	void *native;        // The app's node
	int   native_output; // Its output
	int   native_type;   // mm_type_t of that output
	char *native_value;  // A constant (mm_graph_add_constant): this expression, no callback

	// Hoist (MM_G_HOIST)
	mm_gen_t *hoist_src;    // The generator and output it renders
	int       hoist_output;
	int       hoist_type;   // mm_type_t of that output: MM_T_F, MM_T_RGB or MM_T_RGBA
	int       hoist_size;   // The iterate buffer's size
};

// Code requests (mm_get_shader_code): when set, the hook sees every request and may answer it with another generator's
// output 0 (mm_plan: recording an iterate buffer's loop source, then substituting its loop-invariant inputs)
typedef mm_gen_t *(*mm_code_hook_t)(mm_gen_t *g, const char *uv, int output, mm_context_t *ctx, void *data);
extern mm_code_hook_t mm_code_hook;
extern void          *mm_code_hook_data;

// Graph
mm_gen_t *mm_gen_child(mm_gen_t *graph, const char *name);
typedef struct {
	mm_gen_t *gen;
	int       output;
} mm_port_t;
mm_port_t mm_get_source(mm_gen_t *g, int input_index); // gen NULL when not connected
double    mm_gen_seed(mm_gen_t *g);                     // get_seed: own seed plus the parents' unless locked
bool      mm_gen_has_randomness(mm_gen_t *g);
mm_json_t *mm_gen_param(mm_gen_t *g, const char *name); // Value, or the parameter's default
mm_json_t *mm_gen_param_def(mm_gen_t *g, const char *name);
void       mm_gen_set_param(mm_gen_t *g, const char *name, mm_json_t *v); // With remote / graph propagation
mm_json_t *mm_gen_output_defs(mm_gen_t *g);
void       mm_buffer_analyze(mm_gen_t *g); // Finds whether a buffer renders greyscale
void       mm_graph_reset(void);           // After a guard jump: clears recursion state
mm_json_t *mm_gen_input_defs(mm_gen_t *g);
const char *mm_gen_hier_name(mm_gen_t *g);
void       mm_shader_set_model(mm_gen_t *g, mm_json_t *model); // set_shader_model: preprocessing and generic expansion

// Code generation
mm_code_t *mm_get_shader_code(mm_gen_t *g, const char *uv, int output, mm_context_t *ctx); // With type conversions

// Parameter types (types/*.gd). Each adds its uniforms to c (when declarations are generated) and returns the code
// referring to them, as Material Maker's get_parameters / get_shader.
char *mm_type_gradient(mm_code_t *c, mm_json_t *value, const char *name, bool declare); // Returns <name>_gradient_fct
char *mm_type_curve(mm_code_t *c, mm_json_t *value, const char *name, bool declare);    // Returns <name>_curve_fct
char *mm_type_polygon(mm_code_t *c, mm_json_t *value, const char *name, bool declare, bool closed); // polygon / polyline
char *mm_type_splines(mm_code_t *c, mm_json_t *value, const char *name, bool declare);
char *mm_type_pixels(mm_code_t *c, mm_json_t *value, const char *name, bool declare, char **size_expr); // <name>_pixels_fct
char *mm_type_lattice(mm_code_t *c, mm_json_t *value, const char *name, bool declare, char **size_expr);

// Easy SDF (gen_sdf.gd + sdf_builder): the shader model a sdf_scene makes, NULL when it can not be built
mm_json_t *mm_sdf_shader_model(mm_json_t *sdf_scene);

// CPU textures (mm_texture.c)
bool  mm_texture_size(mm_gen_t *g, int *w, int *h);           // An image or text node's texture size
char *mm_image_adjusted_uv(mm_gen_t *g, const char *uv);       // gen_image.gd get_adjusted_uv

// Library
mm_json_t *mm_library_node(const char *type); // The .mmg definition (parsed, cached for the app's life), NULL if unknown
