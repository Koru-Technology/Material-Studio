#pragma once

// Material Maker (.ptex) support: loads Material Maker graphs and generates the GLSL Material Maker would, a port of its
// generator (addons/material_maker/engine/nodes/gen_*.gd, Material Maker 1.8). Plain C, the app renders the passes
// (mm_bake.c). Everything a load allocates lives in an arena freed with mm_free.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Arena

typedef struct mm_arena mm_arena_t;
void *mm_alloc(size_t size); // Zeroed, from the current arena
void  mm_arena_begin(void);  // Starts an arena, allocations go there until mm_arena_end
void  mm_arena_end(void);    // Frees everything allocated since mm_arena_begin

typedef struct {
	void  *block;
	size_t used;
	void  *big;
} mm_mark_t;
mm_mark_t mm_arena_mark(void);
void      mm_arena_release(mm_mark_t m); // Frees everything allocated since the mark
void      mm_arena_limit(size_t bytes);  // mm_load / mm_plan fail ("Too large") past it, 1 GB by default

// Strings

char *mm_strdup(const char *s);
char *mm_strndup(const char *s, size_t n);
char *mm_printf(const char *fmt, ...);
char *mm_replace(const char *s, const char *from, const char *to); // Every occurrence
bool  mm_starts_with(const char *s, const char *prefix);
bool  mm_contains(const char *s, const char *what);
char *mm_strip(const char *s); // Without leading and trailing whitespace

typedef struct mm_buf {
	char  *data;
	size_t len;
	size_t cap;
} mm_buf_t;
void mm_buf_add(mm_buf_t *b, const char *s);
void mm_buf_addn(mm_buf_t *b, const char *s, size_t n);
void mm_buf_printf(mm_buf_t *b, const char *fmt, ...);
char *mm_buf_get(mm_buf_t *b); // Never NULL

// Json

typedef enum {
	MM_JSON_NULL,
	MM_JSON_BOOL,
	MM_JSON_NUMBER,
	MM_JSON_STRING,
	MM_JSON_ARRAY,
	MM_JSON_OBJECT,
} mm_json_type_t;

typedef struct mm_json {
	mm_json_type_t   type;
	bool             boolean;
	double           number;
	bool             is_int; // The number had no fraction or exponent
	char            *string;
	int              count; // Array items or object members
	struct mm_json **items;
	char           **keys; // Object member names
} mm_json_t;

mm_json_t *mm_json_parse(const char *text, size_t len); // Lenient: trailing commas, NULL on error
mm_json_t *mm_json_parse_persistent(const char *text, size_t len); // Not in the arena, never freed
mm_json_t *mm_json_get(mm_json_t *obj, const char *key);
mm_json_t *mm_json_at(mm_json_t *arr, int i);
const char *mm_json_str(mm_json_t *v, const char *fallback);   // A string, or lines of an array joined
double      mm_json_num(mm_json_t *v, double fallback);        // Numbers, bools and numeric strings
bool        mm_json_bool(mm_json_t *v, bool fallback);
bool        mm_json_truthy(mm_json_t *v); // GDScript truthiness: "true" and any non-empty string are true
char       *mm_json_text(mm_json_t *v);   // Compact JSON, in the arena
mm_json_t  *mm_json_new(mm_json_type_t type);
void        mm_json_set(mm_json_t *obj, const char *key, mm_json_t *v);
void        mm_json_push(mm_json_t *arr, mm_json_t *v);
mm_json_t  *mm_json_copy(mm_json_t *v); // Deep

// Library (nodes.pack, library.pack)

typedef struct mm_pack mm_pack_t;
mm_pack_t  *mm_pack_open(const uint8_t *data, size_t size); // The data stays owned by the caller and must live as long
int         mm_pack_count(mm_pack_t *p);
mm_json_t  *mm_pack_entry(mm_pack_t *p, int i); // Index entry (name, author, ...)
char       *mm_pack_read(mm_pack_t *p, int i, size_t *len); // Inflated blob, malloc'd, caller frees
int         mm_pack_find(mm_pack_t *p, const char *key, const char *value);

void        mm_set_nodes(mm_pack_t *nodes); // The node library used to resolve node types
const char *mm_shader_functions(void);      // Material Maker's common shader functions

// Graph and generation

typedef struct mm_gen mm_gen_t;

typedef struct mm_material {
	mm_gen_t   *root;     // The top-level graph
	mm_gen_t   *material; // The material node, NULL when none
	const char *kind;     // material, material_tesselated, material_unlit, material_dynamic, material_export
	int         size;     // log2 of the resolution Material Maker exports at
	char       *error;    // Why loading failed
} mm_material_t;

mm_material_t *mm_load(const char *ptex, size_t len); // In the current arena

// A pass renders one expression into a texture: either a buffer of the graph or a map of the material
typedef struct mm_pass {
	char *glsl;         // Fragment + vertex source in the iron_glsl.h conventions
	int   size;         // Resolution
	bool  greyscale;    // R16 / R32 target
	bool  f32;          // 32 bits per channel
	int   textures;     // Buffer textures sampled, bound at 2 + i
	int  *texture_pass; // For each texture, the index of the pass that renders it
	bool  iterate;      // An iterate buffer: rendered once with glsl, then iterations times with glsl_loop
	char *glsl_loop;
	int   iterations;
	bool  shrink;
	int   loop_textures;
	int  *loop_texture_pass; // -1 for the pass's own previous iteration
	int   loop_of;           // A buffer inside an iterate buffer's loop: rendered before each of that pass's iterations,
	                         // not in the main walk. -1 otherwise
	char *name;              // Debug name
	bool  exact;             // 32-bit floats even where they can not be filtered, read with texelFetch: a loop-invariant
	                         // input of an iterate buffer's loop, rendered once (mm_plan), must read back bit for bit
	mm_gen_t *texture_gen;   // A CPU texture (image or text node): no glsl, its pixels come from mm_texture_pixels and
	                         // size is their width
} mm_pass_t;

// The constants block of every pass: vec4 mm, x = target size, y = iteration (iterate buffer loop passes), z = 1.0,
// w = 0.0 (added to parameters so that the compiler does not fold them, see mm_uniform_runtime_declaration)

typedef enum {
	MM_MAP_ALBEDO = 0,   // rgba: albedo, opacity
	MM_MAP_ORM,          // rgb: occlusion, roughness, metallic
	MM_MAP_EMISSION,     // rgb
	MM_MAP_NORMAL,       // rgb, Material Maker's export convention
	MM_MAP_HEIGHT,       // f, 1 - depth
	MM_MAP_SSS,          // f
	MM_MAP_COUNT,
} mm_map_t;

typedef struct mm_plan {
	int        count;
	mm_pass_t *passes;              // Buffers first, in render order
	int        map_pass[MM_MAP_COUNT];      // Pass of each map, -1 when the material has no such output
	bool       map_connected[MM_MAP_COUNT]; // The material sets the map (Material Maker only exports those)
	char      *error;
} mm_plan_t;

mm_plan_t *mm_plan(mm_material_t *m, int size); // Size 0: the material's own

// Embedding (mm_embed.c): a graph mixing Material Maker nodes with the app's own nodes, generated into the app's
// shaders. Node names are the connection names. Types are Material Maker's ("f", "rgb", "rgba", "sdf2d", ...).

#define MM_MAIN_UV "tex_coord" // The uv expression of the shader's main scope (the native code callback gets main)

typedef struct {
	char *code;  // Statements computing the value, NULL when none. When not in main, the native code redeclares the
	             // built-ins it uses (tex_coord = uv, ...) in this scope
	char *value; // Expression of the native output's type
} mm_native_t;
typedef mm_native_t (*mm_native_fn)(void *node, int output, const char *uv, bool main);

void      mm_set_native_code(mm_native_fn fn);
void      mm_reset_ids(void); // Generator ids (names o<id>_...) restart: once per shader
mm_gen_t *mm_graph_new(void);
mm_gen_t *mm_graph_add_node(mm_gen_t *graph, const char *name, const char *type, mm_json_t *parameters, double seed, double x,
                            double y); // A library node; seed < 0: from the position, as Material Maker does
mm_gen_t *mm_graph_add_data(mm_gen_t *graph, const char *name, mm_json_t *data); // A node as Material Maker saves one
mm_gen_t *mm_graph_add_native(mm_gen_t *graph, const char *name, void *node, int output, const char *type);
mm_gen_t *mm_graph_add_constant(mm_gen_t *graph, const char *name, const char *type, const char *value); // A literal
void      mm_graph_connect(mm_gen_t *graph, const char *from, int from_port, const char *to, int to_port);

typedef struct mm_emitter mm_emitter_t; // One per shader: variants, uniforms and globals are emitted once
typedef struct {
	char       *declarations;  // For the shader's global scope: constants, functions
	char       *code;          // Statements for the current scope (main)
	char       *value;         // The output's expression
	const char *type;          // Its type: the one asked for when a conversion exists, the output's own otherwise
	bool        needs_buffers; // It samples buffers: textures, see buffers and mm_plan_buffers
	int         nbuffers;      // Textures sampled: the declaration name (oN_tex, oN_it_tex, ...), the buffer, which texture
	char      **buffer_names;
	mm_gen_t  **buffer_gens;
	int        *buffer_outputs;
} mm_emit_t;
mm_emitter_t *mm_emitter_new(const char *zero); // zero: an expression worth 0.0 the compiler can not fold (a uniform),
                                               // added to parameters (see mm_uniform_runtime_declaration)
mm_emit_t     mm_emit(mm_emitter_t *e, mm_gen_t *g, int output, const char *uv, const char *type);
mm_plan_t    *mm_plan_buffers(mm_gen_t **gens, int n, int *pass); // Passes rendering buffer_gens; pass[i]: buffer i's
                                                                  // pass, -1 when not rendered (error set)
typedef struct {
	struct mm_context *ctx;
	int                count;
} mm_emit_mark_t;
mm_emit_mark_t mm_emitter_mark(mm_emitter_t *e);                   // Before an emission the caller may drop:
void           mm_emitter_reset(mm_emitter_t *e, mm_emit_mark_t m); // undone, it is emitted again next time
void          mm_emitter_push(mm_emitter_t *e); // A nested scope: what was emitted outside is emitted again inside
void          mm_emitter_pop(mm_emitter_t *e);

// CPU textures (mm_texture.c): the image and text nodes' outputs, uploaded as RGBA8 unorm (not sRGB)
typedef uint8_t *(*mm_read_file_t)(const char *path, size_t *size); // malloc'd contents, NULL when missing
void mm_add_font(const uint8_t *ttf, size_t size);  // The text node's font (Material Maker uses Godot's default, Open Sans
                                                    // SemiBold), then fallbacks for the characters it lacks. Caller owned
void mm_set_file_reader(mm_read_file_t read);       // Reads image node files: Material Maker's own resources come as
                                                    // file names in assets/material_maker (mm_icon.png)
const char *mm_texture_key(mm_gen_t *g); // What its pixels depend on (kind, parameters), in the arena: a cache key
bool mm_texture_pixels(mm_gen_t *g, int *w, int *h, uint8_t **rgba); // malloc'd, caller frees. False when the image
                                                                     // is missing (1x1 black then, as Material Maker)
