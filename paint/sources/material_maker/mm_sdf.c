// Easy SDF: the shader model of a Material Maker "sdf" node, a port of engine/nodes/gen_sdf.gd (set_sdf_scene) and of
// the sdf builder (addons/material_maker/sdf_builder: sdf_builder.gd, base.gd and the item scripts in sdf2d/, sdf3d/,
// tex/, wired up by sdf_builder.tscn), Material Maker 1.8, with editor = false (what a loaded .ptex uses).
//
// The item definitions are GDScript code, not data, so they are ported here: a static table of item types (parameter
// definitions, includes, shape code) plus the few code generation behaviours the scripts implement. Every string is
// built exactly as the GDScript builds it, including its quirks:
// - parameters are substituted one after the other with String.replace in the order of the item's parameter
//   definitions (replace_parameter_values), floats as "%.09f", parmexprs verbatim;
// - only the top-level items of the scene are deserialized (deserialize_scene recurses with serialize_scene), so only
//   their color and polygon values are Godot Color / Vector2, rounded to 32-bit floats;
// - the 2D color items use sdf3d/color.gd (geometry "sdf3d"), as sdf_builder.tscn does;
// - the 2D Round item has no item_type in sdf_builder.tscn, so Material Maker can not find a "Round" item: it is not in
//   the table either.
// Script errors follow Material Maker's release build (Godot 4.7 export template), where a failed lookup gives null and
// execution goes on: an unknown item type builds like a hidden item (scene_to_shader_model and get_color_code return
// {}, it is not a "TEX"), except that get_includes still collects its children's includes; a missing flip / mirror
// boolean is false. Every other script error (a parameter value of the wrong type, an enum index out of range, a color
// item under an Intersection, a 3D item under a 2D one...) makes Material Maker produce a broken or unpredictable
// shader, and NULL is returned.

#include "mm_internal.h"
#include <math.h>
#include <string.h>

// Item types

typedef enum { PT_FLOAT, PT_BOOL, PT_ENUM, PT_COLOR, PT_POLYGON } ptype_t;

typedef struct {
	const char        *name;
	ptype_t            type;
	double             def[4];
	const char *const *values; // Enum values, NULL terminated
} pdef_t;

typedef enum { M_UNION2D, M_DIFF2D, M_UNION3D, M_DIFF3D, M_EXTRUSION, M_REVOLUTION, M_PARAMS, M_UNIFORM } model_kind_t;
typedef enum { CH_NONE, CH_UNION2D, CH_INTER2D, CH_SMOOTH2D, CH_MORPH2D, CH_UNION3D, CH_INTER3D, CH_SMOOTH3D, CH_MORPH3D } children_kind_t;
typedef enum { UV_NONE, UV_FLIP2D, UV_MIRROR2D, UV_ELONGATE2D, UV_BEND2D, UV_FLIP3D, UV_MIRROR3D, UV_ELONGATE3D, UV_BEND3D, UV_TWIST } mod_uv_kind_t;
typedef enum { MOD_NONE, MOD_ANNULAR, MOD_ROUND, MOD_DISTORT } mod_kind_t;
typedef enum {
	CL_UNION2D,
	CL_UNION3D,
	CL_SMOOTH,
	CL_EXTRUSION,
	CL_REVOLUTION,
	CL_CHANNEL,
	CL_UNIFORM,
	CL_FBM,
	CL_PATTERN,
	CL_UNIFORM_GS,
	CL_BLEND,
	CL_BRIGHTNESS_CONTRAST,
	CL_DEFORM,
	CL_STEP,
	CL_GRADIENT,
} color_kind_t;

typedef struct {
	const char     *type;
	const char     *category;
	const pdef_t   *params;
	int             nparams;
	const char     *includes[4];
	model_kind_t    model;
	children_kind_t children;
	mod_uv_kind_t   mod_uv;
	mod_kind_t      mod;
	color_kind_t    color;
	const char     *shape;   // shape_code, %s is the uv. NULL: none ("" in the GDScript)
	const char     *op_sign; // Smooth union / intersection
	const char     *channel; // Color items
	bool            polygon; // Polygon's shape_code_pre and shape_code
} item_t;

#define F(n, d)        {n, PT_FLOAT, {d}, NULL}
#define B(n, d)        {n, PT_BOOL, {d}, NULL}
#define E(n, v)        {n, PT_ENUM, {0}, v}
#define C(n, r, g, b, a) {n, PT_COLOR, {r, g, b, a}, NULL}
#define P2D            F("position_x", 0.0), F("position_y", 0.0), F("angle", 0.0), F("scale", 1.0)
#define P3D_POS_ANGLE  F("position_x", 0.0), F("position_y", 0.0), F("position_z", 0.0), F("angle_x", 0.0), F("angle_y", 0.0), F("angle_z", 0.0)
#define P3D            P3D_POS_ANGLE, F("scale", 1.0)
#define N(a)           a, (int)(sizeof(a) / sizeof(a[0]))

static const char *const axis_cylinder[] = {"xyz", "yzx", "zxy", NULL};
static const char *const axis_torus[]    = {"zxy", "xyz", "yzx", NULL};
static const char *const axis_twist[]    = {"yz", "zx", "xy", NULL};
static const char *const noises[]        = {"value", "perlin", "cellular", NULL};
static const char *const combiners[]     = {"mul", "add", "max", "min", "xor", "pow", NULL};
static const char *const waves[]         = {"sine", "triangle", "square", "sawtooth", "constant", "bounce", NULL};

static const pdef_t p_2d[]        = {P2D};
static const pdef_t p_arc[]       = {F("position_x", 0.0), F("position_y", 0.0), F("a1", 50.0),    F("a2", -90.0),
                                     F("r1", 0.4),         F("r2", 0.1),         F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_arrow[]     = {F("position_x", 0.0), F("position_y", 0.0), F("ax", -0.3), F("ay", -0.3),    F("bx", 0.3),    F("by", 0.3),
                                     F("k", 0.55),         F("wh", 0.25),        F("wt", 0.1),  F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_box[]       = {P2D, F("size_x", 0.5), F("size_y", 0.5)};
static const pdef_t p_circle[]    = {P2D, F("radius", 0.5)};
static const pdef_t p_cross[]     = {F("position_x", 0.0), F("position_y", 0.0), F("r", 0.15),     F("bx", 0.5),
                                     F("by", 0.15),        F("angle", 0.0),      F("scale", 1.0)};
static const pdef_t p_heart[]     = {F("position_x", 0.0), F("position_y", 0.05), F("r", 0.45), F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_hexagram[]  = {F("position_x", 0.0), F("position_y", 0.0), F("r", 0.25), F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_line[]      = {P2D, F("ax", -0.25), F("ay", -0.25), F("bx", 0.25), F("by", 0.25), F("r", 0.25)};
static const pdef_t p_moon[]      = {F("position_x", 0.0), F("position_y", 0.0), F("ra", 0.5),     F("rb", 0.5),
                                     F("angle", 0.0),      F("d", 0.4),          F("scale", 1.0)};
static const pdef_t p_ngon[]      = {P2D, F("edgecount", 5.0), F("radius", 0.5)};
static const pdef_t p_pie[]       = {F("position_x", 0.0), F("position_y", 0.0), F("r", 0.5), F("aa", -45.0), F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_polygon[]   = {P2D, F("radius", 0.5), {"polygon", PT_POLYGON, {0}, NULL}};
static const pdef_t p_stairs[]    = {P2D, F("size_x", 0.5), F("size_y", 0.5), F("steps", 3.0)};
static const pdef_t p_star[]      = {P2D, F("branchcount", 5.0), F("radius", 0.5), F("pointiness", 0.5)};
static const pdef_t p_vesica[]    = {F("position_x", 0.0), F("position_y", 0.0), F("r", 0.6), F("d", 0.4), F("angle", 0.0), F("scale", 1.0)};
static const pdef_t p_smooth2d[]  = {P2D, F("k", 1.0), F("color_k", 0.5)};
static const pdef_t p_flip2d[]    = {P2D, B("h_flip", 1), B("v_flip", 1)};
static const pdef_t p_mirror2d[]  = {P2D, B("h_mirror", 1), B("v_mirror", 1)};
static const pdef_t p_elongate2d[] = {P2D, F("size_x", 0.1), F("size_y", 0.1)};
static const pdef_t p_amount2d[]  = {P2D, F("amount", 0.5)};
static const pdef_t p_width2d[]   = {P2D, F("width", 0.1)};
static const pdef_t p_k2d[]       = {P2D, F("k", 0.5)};
static const pdef_t p_color[]     = {B("local", 0)};

static const pdef_t p_3d[]         = {P3D};
static const pdef_t p_sphere[]     = {P3D, F("radius", 0.5)};
static const pdef_t p_box3d[]      = {P3D, F("size_x", 0.5), F("size_y", 0.5), F("size_z", 0.5)};
static const pdef_t p_cylinder[]   = {P3D_POS_ANGLE, E("axis", axis_cylinder), F("scale", 1.0), F("height", 0.5), F("radius", 0.5)};
static const pdef_t p_torus[]      = {P3D_POS_ANGLE, E("axis", axis_torus), F("scale", 1.0), F("R", 0.5), F("r", 0.1)};
static const pdef_t p_extrusion[]  = {P3D, F("length", 0.5)};
static const pdef_t p_revolution[] = {P3D, F("offset", 0.5)};
static const pdef_t p_smooth3d[]   = {P3D, F("k", 1.0), F("color_k", 0.5)};
static const pdef_t p_flip3d[]     = {P3D, B("x_flip", 1), B("y_flip", 1), B("z_flip", 1)};
static const pdef_t p_mirror3d[]   = {P3D, B("x_mirror", 1), B("y_mirror", 1), B("z_mirror", 1)};
static const pdef_t p_elongate3d[] = {P3D, F("size_x", 0.1), F("size_y", 0.1), F("size_z", 0.1)};
static const pdef_t p_width3d[]    = {P3D, F("width", 0.1)};
static const pdef_t p_bend3d[]     = {P3D, F("amount", 0.5)};
static const pdef_t p_k3d[]        = {P3D, F("k", 0.5)};
static const pdef_t p_twist[]      = {P3D, E("axis", axis_twist), F("angle", 0.0)};
static const pdef_t p_distort[]    = {P3D, F("amount", 0.1)};

static const pdef_t p_uniform[]  = {C("color", 1.0, 1.0, 1.0, 1.0)};
static const pdef_t p_uniform_gs[] = {F("value", 1.0)};
static const pdef_t p_pattern[]  = {E("mix", combiners), E("x_wave", waves),   F("x_scale", 4.0), E("y_wave", waves),
                                    F("y_scale", 4.0),   E("z_wave", waves),   F("z_scale", 4.0)};
static const pdef_t p_fbm[]      = {E("noise", noises), F("scale_x", 4.0), F("scale_y", 4.0),     F("scale_z", 4.0),
                                    F("folds", 0.0),    F("iterations", 3.0), F("persistence", 0.5), F("offset", 0.0)};
static const pdef_t p_bc[]       = {F("brightness", 0.0), F("contrast", 1.0), B("clamp", 1)};
static const pdef_t p_step[]     = {F("value", 0.5), F("width", 0.1), B("invert", 0)};
static const pdef_t p_gradient[] = {C("color1", 0.0, 0.0, 0.0, 1.0), C("color2", 1.0, 1.0, 1.0, 1.0)};
static const pdef_t p_deform[]   = {F("amount", 0.0)};

// Shapes and alterations derive from sdf2d/boolean/union.gd or sdf3d/boolean/union.gd
#define SHAPE2D(t, p, i1, i2, code) {t, "SDF2D", N(p), {i1, i2}, M_UNION2D, CH_UNION2D, UV_NONE, MOD_NONE, CL_UNION2D, code}
#define ALTER2D(t, p, i2, uv, mod)  {t, "SDF2D", N(p), {"rotate", i2}, M_UNION2D, CH_UNION2D, uv, mod, CL_UNION2D}
#define SHAPE3D(t, p, i2, code)     {t, "SDF3D", N(p), {"rotate", i2}, M_UNION3D, CH_UNION3D, UV_NONE, MOD_NONE, CL_UNION3D, code}
#define ALTER3D(t, p, i2, uv, mod)  {t, "SDF3D", N(p), {"rotate", i2}, M_UNION3D, CH_UNION3D, uv, mod, CL_UNION3D}
#define COLOR(t, cat, ch)           {t, cat, N(p_color), {NULL}, M_PARAMS, CH_NONE, UV_NONE, MOD_NONE, CL_CHANNEL, NULL, NULL, ch}
#define TEX(t, p, i1, i2, model, cl) {t, "TEX", N(p), {i1, i2}, model, CH_NONE, UV_NONE, MOD_NONE, cl}

static const item_t items[] = {
	SHAPE2D("Arc", p_arc, "sdarc", "rotate", "sdArc( %s, mod($a1, 360.0)*0.01745329251, mod($a2, 360.0)*0.01745329251, $r1, $r2)"),
	SHAPE2D("Arrow", p_arrow, "sdarrow", "rotate", "sdArrow(%s,vec2($ax,$ay),vec2($bx,$by),$wt,$wh,$k/$wh*distance(vec2($ax,$ay),vec2($bx,$by)))"),
	SHAPE2D("Box", p_box, "rotate", "sdbox", "sd_box(%s, vec2($size_x, $size_y))"),
	SHAPE2D("Circle", p_circle, "rotate", NULL, "length(%s)-$radius"),
	SHAPE2D("Cross", p_cross, "sdcross", "rotate", "sdCross(%s, vec2($bx, $by) + $r, $r)"),
	SHAPE2D("Ellipse", p_box, "rotate", "sdellipse", "sd_ellipse(%s, vec2($size_x, $size_y))"),
	SHAPE2D("Heart", p_heart, "sdheart", "rotate", "sdHeart((%s) / ($r *1.75)) * ($r *1.75)"),
	SHAPE2D("Hexagram", p_hexagram, "sdhexagram", "rotate", "sdHexagram(%s, $r)"),
	SHAPE2D("IsoTriangle", p_box, "rotate", "sdisoscelestriangle", "sd_isosceles_triangle(%s, vec2($size_x, $size_y))"),
	SHAPE2D("Line", p_line, "rotate", "sdline2", "sdLine(%s, vec2($ax, $ay), vec2($bx, $by)).x-$r"),
	SHAPE2D("Moon", p_moon, "sdmoon", "rotate", "sdMoon(%s, $d, $ra, $rb)"),
	{"NGon", "SDF2D", N(p_ngon), {"rotate", "sdcirclerepeat", "sdngon"}, M_UNION2D, CH_UNION2D, UV_NONE, MOD_NONE, CL_UNION2D,
     "sdNgon(%s, $radius, $edgecount)"},
	SHAPE2D("Pie", p_pie, "sdpie", "rotate",
	        "sdPie(%s, vec2(sin(radians(abs(mod(450.0 + $aa , 360.0) - 180.0))), cos(radians(abs(mod(450.0 + $aa , 360.0) - 180.0)))), $r)"),
	{"Polygon", "SDF2D", N(p_polygon), {"rotate"}, M_UNION2D, CH_UNION2D, UV_NONE, MOD_NONE, CL_UNION2D, NULL, NULL, NULL, true},
	SHAPE2D("Rhombus", p_box, "rotate", "sdrhombus", "sdRhombus(%s, vec2($size_x, $size_y))"),
	SHAPE2D("Stairs", p_stairs, "rotate", "sdstairs", "sdStairs(vec2(1.0, -1.0)*%s+vec2($size_x, $size_y), 2.0*vec2($size_x, $size_y)/$steps, $steps)"),
	SHAPE2D("Star", p_star, "rotate", "sdstar", "sdStar(%s, $radius, int($branchcount),  2.0+($branchcount-2.0)*(1.0-$pointiness))"),
	SHAPE2D("Tunnel", p_box, "rotate", "sdtunnel", "sdTunnel(%s, vec2($size_x, $size_y))"),
	SHAPE2D("Vesica", p_vesica, "sdversica", "rotate", "sdVesica(%s, $r, $r - $d)"),
	SHAPE2D("Union", p_2d, "rotate", NULL, NULL),
	{"Intersection", "SDF2D", N(p_2d), {"rotate"}, M_UNION2D, CH_INTER2D, UV_NONE, MOD_NONE, CL_UNION2D},
	{"Difference", "SDF2D", N(p_2d), {"rotate"}, M_DIFF2D, CH_NONE, UV_NONE, MOD_NONE, CL_UNION2D},
	{"SmoothUnion", "SDF2D", N(p_smooth2d), {"rotate"}, M_UNION2D, CH_SMOOTH2D, UV_NONE, MOD_NONE, CL_SMOOTH, NULL, "-"},
	{"SmoothIntersection", "SDF2D", N(p_smooth2d), {"rotate"}, M_UNION2D, CH_SMOOTH2D, UV_NONE, MOD_NONE, CL_SMOOTH, NULL, ""},
	ALTER2D("Flip", p_flip2d, NULL, UV_FLIP2D, MOD_NONE),
	ALTER2D("Mirror", p_mirror2d, NULL, UV_MIRROR2D, MOD_NONE),
	ALTER2D("Elongate", p_elongate2d, NULL, UV_ELONGATE2D, MOD_NONE),
	ALTER2D("Bend", p_amount2d, "sdbend", UV_BEND2D, MOD_NONE),
	ALTER2D("Annular", p_width2d, NULL, UV_NONE, MOD_ANNULAR),
	{"Morph", "SDF2D", N(p_k2d), {"rotate"}, M_UNION2D, CH_MORPH2D, UV_NONE, MOD_NONE, CL_UNION2D},
	COLOR("Albedo2D", "SDF2D_COLOR", "albedo"),
	COLOR("Metallic2D", "SDF2D_COLOR", "metallic"),
	COLOR("Roughness2D", "SDF2D_COLOR", "roughness"),
	COLOR("Emission2D", "SDF2D_COLOR", "emission"),

	SHAPE3D("Sphere", p_sphere, NULL, "length(%s)-$radius"),
	SHAPE3D("Box3D", p_box3d, "sdf3d_box", "box3d(%s, vec3($size_x, $size_y, $size_z))"),
	SHAPE3D("Cylinder", p_cylinder, "sdf3d_cylinder", "sdCylinder(%s.$axis, $height, $radius)"),
	SHAPE3D("Capsule", p_cylinder, "sdf3d_capsule2", "sdCapsule(%s.$axis, $height, $radius)"),
	SHAPE3D("Cone", p_cylinder, "sdf3d_cone2", "sdCone(vec3(0.0, $height, 0.0)-%s.$axis, vec2($radius, 2.0*$height))"),
	SHAPE3D("Torus", p_torus, "sdf3d_torus", "sdTorus(%s.$axis, vec2($R, $r))"),
	SHAPE3D("Pyramid", p_cylinder, "sdf3d_pyramid", "sdPyramid(0.5*%s.$axis/$radius, $height/$radius)*$radius*2.0"),
	SHAPE3D("Ellipsoid", p_box3d, "sdf3d_ellipsoid", "sdEllipsoid(%s, vec3($size_x, $size_y, $size_z))"),
	SHAPE3D("Octahedron", p_sphere, "sdf3d_octahedron", "sdOctahedron(%s, $radius)"),
	{"Extrusion", "SDF3D", N(p_extrusion), {"sdf3d_rotate"}, M_EXTRUSION, CH_UNION2D, UV_NONE, MOD_NONE, CL_EXTRUSION, "1000000.0"},
	{"Revolution", "SDF3D", N(p_revolution), {"sdf3d_rotate"}, M_REVOLUTION, CH_UNION2D, UV_NONE, MOD_NONE, CL_REVOLUTION, "1000000.0"},
	SHAPE3D("Union3D", p_3d, NULL, NULL),
	{"Intersection3D", "SDF3D", N(p_3d), {"rotate"}, M_UNION3D, CH_INTER3D, UV_NONE, MOD_NONE, CL_UNION3D},
	{"Difference3D", "SDF3D", N(p_3d), {"rotate"}, M_DIFF3D, CH_NONE, UV_NONE, MOD_NONE, CL_UNION3D},
	{"SmoothUnion3D", "SDF3D", N(p_smooth3d), {"rotate"}, M_UNION3D, CH_SMOOTH3D, UV_NONE, MOD_NONE, CL_SMOOTH, NULL, "-"},
	{"SmoothIntersection3D", "SDF3D", N(p_smooth3d), {"rotate"}, M_UNION3D, CH_SMOOTH3D, UV_NONE, MOD_NONE, CL_SMOOTH, NULL, ""},
	ALTER3D("Flip3D", p_flip3d, NULL, UV_FLIP3D, MOD_NONE),
	ALTER3D("Mirror3D", p_mirror3d, NULL, UV_MIRROR3D, MOD_NONE),
	ALTER3D("Elongate3D", p_elongate3d, NULL, UV_ELONGATE3D, MOD_NONE),
	ALTER3D("Round3D", p_width3d, NULL, UV_NONE, MOD_ROUND),
	ALTER3D("Bend3D", p_bend3d, "sdf3d_bend", UV_BEND3D, MOD_NONE),
	ALTER3D("Annular3D", p_width3d, NULL, UV_NONE, MOD_ANNULAR),
	{"Morph3D", "SDF3D", N(p_k3d), {"rotate"}, M_UNION3D, CH_MORPH3D, UV_NONE, MOD_NONE, CL_UNION3D},
	ALTER3D("Twist", p_twist, NULL, UV_TWIST, MOD_NONE),
	ALTER3D("Distort3D", p_distort, NULL, UV_NONE, MOD_DISTORT),
	COLOR("Albedo3D", "SDF3D_COLOR", "albedo"),
	COLOR("Metallic3D", "SDF3D_COLOR", "metallic"),
	COLOR("Roughness3D", "SDF3D_COLOR", "roughness"),
	COLOR("Emission3D", "SDF3D_COLOR", "emission"),

	TEX("Uniform", p_uniform, NULL, NULL, M_UNIFORM, CL_UNIFORM),
	TEX("GreyscaleUniform", p_uniform_gs, NULL, NULL, M_PARAMS, CL_UNIFORM_GS),
	TEX("Pattern", p_pattern, "pattern", "tex3d_pattern", M_PARAMS, CL_PATTERN),
	TEX("FBM", p_fbm, "fbm2", "tex3d_fbm", M_PARAMS, CL_FBM),
	TEX("BrightnessContrast", p_bc, NULL, NULL, M_PARAMS, CL_BRIGHTNESS_CONTRAST),
	TEX("Step", p_step, NULL, NULL, M_PARAMS, CL_STEP),
	TEX("SimpleGradient", p_gradient, NULL, NULL, M_PARAMS, CL_GRADIENT),
	{"Blend", "TEX", NULL, 0, {NULL}, M_PARAMS, CH_NONE, UV_NONE, MOD_NONE, CL_BLEND},
	TEX("Deform", p_deform, NULL, NULL, M_PARAMS, CL_DEFORM),
};

// Builder state

static bool       sdf_error;
static mm_json_t *sdf_top; // The scene's top-level items: the only ones deserialized

static void fail(void) {
	sdf_error = true;
}

static const item_t *find_item(mm_json_t *scene) {
	const char *type = mm_json_str(mm_json_get(scene, "type"), NULL);
	if (type != NULL) {
		for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
			if (strcmp(items[i].type, type) == 0) {
				return &items[i];
			}
		}
	}
	return NULL;
}

// item_types[item_ids[s.type]].item_category == category (null for an unknown type)
static bool is_category(mm_json_t *scene, const char *category) {
	const item_t *it = find_item(scene);
	return it != NULL && strcmp(it->category, category) == 0;
}

static bool hidden(mm_json_t *scene) {
	return mm_json_bool(mm_json_get(scene, "hidden"), false);
}

static int scene_index(mm_json_t *scene) {
	return (int)mm_json_num(mm_json_get(scene, "index"), 0.0);
}

static mm_json_t *children_of(mm_json_t *scene) {
	return mm_json_get(scene, "children");
}

static int children_count(mm_json_t *scene) {
	mm_json_t *c = children_of(scene);
	return c != NULL && c->type == MM_JSON_ARRAY ? c->count : 0;
}

static mm_json_t *param_value(mm_json_t *scene, const char *name) {
	return mm_json_get(mm_json_get(scene, "parameters"), name);
}

static bool is_top(mm_json_t *scene) {
	for (int i = 0; sdf_top != NULL && i < sdf_top->count; ++i) {
		if (sdf_top->items[i] == scene) {
			return true;
		}
	}
	return false;
}

// A value as a GDScript condition
static bool truthy(mm_json_t *v) {
	switch (v != NULL ? v->type : MM_JSON_NULL) {
	case MM_JSON_BOOL:
		return v->boolean;
	case MM_JSON_NUMBER:
		return v->number != 0.0;
	case MM_JSON_STRING:
		return v->string[0] != 0;
	case MM_JSON_ARRAY:
	case MM_JSON_OBJECT:
		return v->count > 0;
	default:
		return false;
	}
}

static bool is_number(mm_json_t *v) {
	return v != NULL && v->type == MM_JSON_NUMBER;
}

// scene.parameters.x as a condition (null when missing)
static bool param_true(mm_json_t *scene, const char *name) {
	return truthy(param_value(scene, name));
}

// sdf_builder.replace_parameter_values
static char *replace_parameter_values(const item_t *it, mm_json_t *scene, char *s) {
	mm_json_t *exprs = mm_json_get(scene, "parmexprs");
	bool       top   = is_top(scene);
	for (int i = 0; i < it->nparams; ++i) {
		const pdef_t *p   = &it->params[i];
		mm_json_t    *v   = param_value(scene, p->name);
		char         *key = mm_printf("$%s", p->name);
		char         *value;
		switch (p->type) {
		case PT_BOOL:
			value = (v != NULL ? truthy(v) : p->def[0] != 0.0) ? "true" : "false";
			break;
		case PT_ENUM: {
			int n = 0;
			while (p->values[n] != NULL) {
				n++;
			}
			if (v != NULL && !is_number(v)) {
				fail();
				return s;
			}
			int index = v != NULL ? (int)mm_json_num(v, 0.0) : (int)p->def[0];
			if (index < 0 || index >= n) {
				fail();
				return s;
			}
			value = (char *)p->values[index];
			break;
		}
		case PT_FLOAT: {
			mm_json_t *e = mm_json_get(exprs, p->name);
			if (e != NULL) {
				value = (char *)mm_json_str(e, "");
			}
			else if (v != NULL && !top && v->type == MM_JSON_ARRAY && v->count == 1 && is_number(v->items[0])) {
				value = mm_printf("%.9f", v->items[0]->number); // "%.09f" % [x] formats x
			}
			else if (v != NULL && !is_number(v)) {
				fail();
				return s;
			}
			else {
				value = mm_printf("%.9f", v != NULL ? mm_json_num(v, 0.0) : p->def[0]);
			}
			break;
		}
		case PT_COLOR: {
			double c[4] = {p->def[0], p->def[1], p->def[2], p->def[3]};
			if (v != NULL) {
				const char *k[4] = {"r", "g", "b", "a"};
				for (int j = 0; j < 4; ++j) {
					if (!is_number(mm_json_get(v, k[j]))) {
						fail();
						return s;
					}
				}
				for (int j = 0; j < 4; ++j) {
					c[j] = mm_json_num(mm_json_get(v, k[j]), 0.0);
					if (top) { // A Color
						c[j] = (float)c[j];
					}
				}
			}
			value = mm_printf("vec4(%.9f, %.9f, %.9f, %.9f)", c[0], c[1], c[2], c[3]);
			break;
		}
		case PT_POLYGON: {
			static const double def[] = {0.2, 0.2, 0.4, 0.7, 0.7, 0.4};
			mm_buf_t            b     = {0};
			mm_buf_add(&b, "{");
			mm_json_t *points  = mm_json_get(v, "points");
			if (v != NULL && (points == NULL || points->type != MM_JSON_ARRAY)) {
				fail();
				return s;
			}
			bool       vector2 = top && strcmp(mm_json_str(mm_json_get(v, "type"), ""), "Polygon") == 0; // An MMPolygon
			int        n       = v != NULL ? (points != NULL ? points->count : 0) : 3;
			for (int j = 0; j < n; ++j) {
				double x, y;
				if (v != NULL) {
					x = mm_json_num(mm_json_get(points->items[j], "x"), 0.0);
					y = mm_json_num(mm_json_get(points->items[j], "y"), 0.0);
					if (vector2) {
						x = (float)x;
						y = (float)y;
					}
				}
				else {
					x = def[j * 2];
					y = def[j * 2 + 1];
				}
				mm_buf_printf(&b, "%svec2(%.9f,%.9f)", j > 0 ? "," : "", x, y);
			}
			mm_buf_add(&b, "}");
			value = mm_buf_get(&b);
			break;
		}
		}
		if (strstr(s, key) != NULL) {
			s = mm_replace(s, key, value);
		}
	}
	return s;
}

// sdf_builder.check_non_zero_param (false when the value is not a number: an error)
static bool non_zero(mm_json_t *scene, const char *name, double zero) {
	if (mm_json_get(mm_json_get(scene, "parmexprs"), name) != NULL) {
		return true;
	}
	mm_json_t *v = param_value(scene, name);
	return is_number(v) && fabs(v->number - zero) > 0.0001;
}

// sdf_builder.generate_rotate_3d
static void rotate_3d(mm_buf_t *b, const char *var, mm_json_t *scene) {
	if (non_zero(scene, "angle_y", 0.0)) {
		mm_buf_printf(b, "%s.zx = rotate(%s.zx, radians($angle_y));\n", var, var);
	}
	if (non_zero(scene, "angle_x", 0.0)) {
		mm_buf_printf(b, "%s.yz = rotate(%s.yz, radians($angle_x));\n", var, var);
	}
	if (non_zero(scene, "angle_z", 0.0)) {
		mm_buf_printf(b, "%s.xy = rotate(%s.xy, radians($angle_z));\n", var, var);
	}
}

// Shader models

typedef struct {
	bool        empty;
	const char *code;    // NULL: no "code"
	const char *out_key; // NULL: no "outputs", else "sdf2d" or "sdf3d"
	const char *out;
} model_t;

static model_t scene_to_shader_model(mm_json_t *scene, const char *uv);

// outputs[0][key] of a child model
static const char *output(model_t *m, const char *key) {
	if (m->out_key == NULL || strcmp(m->out_key, key) != 0) {
		fail();
		return "";
	}
	return m->out;
}

static char *shape_code(const item_t *it, mm_json_t *scene, const char *uv) {
	if (it->polygon) {
		return mm_replace("<prefix>s*sqrt(<prefix>d)", "<prefix>", mm_printf("$(name_uv)_n%d_", scene_index(scene)));
	}
	return it->shape != NULL ? mm_printf(it->shape, uv) : "";
}

static char *shape_code_pre(const item_t *it, mm_json_t *scene, const char *uv) {
	if (!it->polygon) {
		return "";
	}
	mm_buf_t b = {0};
	mm_buf_printf(&b, "vec2 <prefix>uv = %s+vec2(0.5);", uv);
	mm_buf_add(&b, "vec2 <prefix>v[] = $polygon;");
	mm_buf_add(&b, "int <prefix>l = <prefix>v.length();");
	mm_buf_add(&b, "float <prefix>d = dot(<prefix>uv-<prefix>v[0], <prefix>uv-<prefix>v[0]);");
	mm_buf_add(&b, "float <prefix>s = 1.0;");
	mm_buf_add(&b, "int <prefix>j = <prefix>l-1;");
	mm_buf_add(&b, "for (int <prefix>i=0; <prefix>i<<prefix>l; <prefix>i++) {");
	mm_buf_add(&b, "    vec2 <prefix>e = <prefix>v[<prefix>j] - <prefix>v[<prefix>i];");
	mm_buf_add(&b, "    vec2 <prefix>w = <prefix>uv - <prefix>v[<prefix>i];");
	mm_buf_add(&b, "    vec2 <prefix>b = <prefix>w - <prefix>e*clamp(dot(<prefix>w,<prefix>e)/dot(<prefix>e,<prefix>e), 0.0, 1.0 );");
	mm_buf_add(&b, "    <prefix>d = min(<prefix>d, dot(<prefix>b, <prefix>b));");
	mm_buf_add(&b, "    bvec3 <prefix>c = bvec3(<prefix>uv.y>=<prefix>v[<prefix>i].y,<prefix>uv.y<<prefix>v[<prefix>j].y,<prefix>e.x*<prefix>w.y><prefix>e.y*<prefix>w.x);");
	mm_buf_add(&b, "    if (all(<prefix>c) || all(not(<prefix>c))) <prefix>s*=-1.0;");
	mm_buf_add(&b, "    <prefix>j = <prefix>i;");
	mm_buf_add(&b, "}");
	return mm_replace(mm_buf_get(&b), "<prefix>", mm_printf("$(name_uv)_n%d_", scene_index(scene)));
}

static void mod_uv_code(const item_t *it, mm_json_t *scene, mm_buf_t *b, const char *o) {
	static const char *axes[] = {"x", "y", "z"};
	switch (it->mod_uv) {
	case UV_NONE:
		break;
	case UV_FLIP2D:
	case UV_MIRROR2D:
	case UV_FLIP3D:
	case UV_MIRROR3D: {
		bool        flip = it->mod_uv == UV_FLIP2D || it->mod_uv == UV_FLIP3D;
		bool        is3d = it->mod_uv == UV_FLIP3D || it->mod_uv == UV_MIRROR3D;
		const char *names2d[] = {"h", "v"};
		for (int i = 0; i < (is3d ? 3 : 2); ++i) {
			char *name = mm_printf("%s_%s", is3d ? axes[i] : names2d[i], flip ? "flip" : "mirror");
			if (param_true(scene, name)) {
				if (flip) {
					mm_buf_printf(b, "%s_p.%s = -%s_p.%s;\n", o, axes[i], o, axes[i]);
				}
				else {
					mm_buf_printf(b, "%s_p.%s = abs(%s_p.%s);\n", o, axes[i], o, axes[i]);
				}
			}
		}
		break;
	}
	case UV_ELONGATE2D:
		mm_buf_printf(b, "vec2 %s_s = vec2($size_x, $size_y);\n%s_p = %s_p - clamp(%s_p, -%s_s, %s_s);\n", o, o, o, o, o, o);
		break;
	case UV_ELONGATE3D:
		mm_buf_printf(b, "vec3 %s_s = vec3($size_x, $size_y, $size_z);\n%s_p = %s_p - clamp(%s_p, -%s_s, %s_s);\n", o, o, o, o, o, o);
		break;
	case UV_BEND2D:
		mm_buf_printf(b, "%s_p = sdBend(%s_p, $amount);\n", o, o);
		break;
	case UV_BEND3D:
		mm_buf_printf(b, "%s_p = Bend3d(%s_p, $amount);\n", o, o);
		break;
	case UV_TWIST: {
		mm_json_t *axis = param_value(scene, "axis");
		switch (axis != NULL ? (int)mm_json_num(axis, 0.0) : 0) {
		case 1:
			mm_buf_printf(b, "%s_p.zx=rotate(%s_p.zx, %s_p.y*$angle*0.01745329251);\n", o, o, o);
			break;
		case 2:
			mm_buf_printf(b, "%s_p.xy=rotate(%s_p.xy, %s_p.z*$angle*0.01745329251);\n", o, o, o);
			break;
		default:
			mm_buf_printf(b, "%s_p.yz=rotate(%s_p.yz, %s_p.x*$angle*0.01745329251);\n", o, o, o);
			break;
		}
		break;
	}
	}
}

typedef struct {
	const char *uv;
	const char *channel;
	const char *target;
	const char *type;
	const char *glsl_type;
	const char *local_uv;
	const char *geometry;
} ctxt_t;

typedef struct {
	bool        empty;
	const char *color;    // NULL: no "color"
	const char *distance; // NULL: no "distance"
} color_t;

static color_t get_color_code(mm_json_t *scene, ctxt_t *ctxt);

static void mod_code(const item_t *it, mm_json_t *scene, mm_buf_t *b, const char *o) {
	switch (it->mod) {
	case MOD_NONE:
		break;
	case MOD_ANNULAR:
		mm_buf_printf(b, "%s = abs(%s)-0.5*$width;\n", o, o);
		break;
	case MOD_ROUND:
		mm_buf_printf(b, "%s -= $width;\n", o);
		break;
	case MOD_DISTORT:
		for (int i = 0; i < children_count(scene); ++i) {
			mm_json_t *s = children_of(scene)->items[i];
			if (!is_category(s, "TEX")) {
				continue;
			}
			ctxt_t  ctxt        = {.uv = mm_printf("%s_p", o), .geometry = "sdf3d", .type = "float", .glsl_type = "float"};
			color_t distort_map = get_color_code(s, &ctxt);
			if (distort_map.color != NULL) {
				mm_buf_printf(b, "%s += $amount*(%s-0.5);\n", o, distort_map.color);
				return;
			}
		}
		break;
	}
}

// shape_and_children_code
static void children_code(const item_t *it, mm_json_t *scene, mm_buf_t *b, const char *uv) {
	char       *o        = mm_printf("$(name_uv)_n%d", scene_index(scene));
	char       *o_p      = mm_printf("%s_p", o);
	int         index    = scene_index(scene);
	int         n        = children_count(scene);
	bool        is3d     = it->children == CH_UNION3D || it->children == CH_INTER3D || it->children == CH_SMOOTH3D || it->children == CH_MORPH3D;
	const char *key      = is3d ? "sdf3d" : "sdf2d";
	bool        assigned = false;
	bool        first    = true;
	int         count    = 0;
	switch (it->children) {
	case CH_NONE:
		break;
	case CH_UNION2D:
	case CH_UNION3D: {
		mm_buf_printf(b, "float %s;\n", o);
		char *init = shape_code(it, scene, uv);
		if (init[0] != 0) {
			if (!is3d) {
				mm_buf_add(b, shape_code_pre(it, scene, uv));
			}
			mm_buf_printf(b, "%s = %s;\n", o, init);
			assigned = true;
		}
		for (int i = 0; i < n; ++i) {
			model_t m = scene_to_shader_model(children_of(scene)->items[i], is3d ? o_p : uv);
			if (m.out_key != NULL) {
				if (m.code != NULL) {
					mm_buf_add(b, m.code);
				}
				const char *out = output(&m, key);
				if (sdf_error) {
					return;
				}
				if (assigned) {
					mm_buf_printf(b, "%s = min(%s, %s);\n", o, o, out);
				}
				else {
					mm_buf_printf(b, "%s = %s;\n", o, out);
					assigned = true;
				}
			}
		}
		if (is3d && !assigned) {
			mm_buf_printf(b, "%s = 1000000.0;\n", o);
		}
		break;
	}
	case CH_INTER2D:
	case CH_INTER3D:
		mm_buf_printf(b, "float %s = 42.0;", o);
		for (int i = 0; i < n; ++i) {
			model_t m = scene_to_shader_model(children_of(scene)->items[i], o_p);
			if (is3d ? m.code != NULL : !m.empty) {
				if (m.code == NULL) {
					fail();
					return;
				}
				mm_buf_add(b, m.code);
				const char *out = output(&m, key);
				if (sdf_error) {
					return;
				}
				if (first) {
					mm_buf_printf(b, "%s = %s;\n", o, out);
					first = false;
				}
				else {
					mm_buf_printf(b, "%s = max(%s, %s);\n", o, o, out);
				}
			}
		}
		break;
	case CH_SMOOTH2D:
	case CH_SMOOTH3D:
		mm_buf_printf(b, "float %s = 0.0;", o);
		mm_buf_printf(b, "float $(name_uv)_n%d_kk = 10.0/$k;", index);
		for (int i = 0; i < n; ++i) {
			model_t m = scene_to_shader_model(children_of(scene)->items[i], o_p);
			if (m.code != NULL) {
				mm_buf_add(b, m.code);
				const char *out = output(&m, key);
				if (sdf_error) {
					return;
				}
				mm_buf_printf(b, "%s += exp2(%s$(name_uv)_n%d_kk*%s);\n", o, it->op_sign, index, out);
			}
		}
		mm_buf_printf(b, "%s = %slog2(%s)/$(name_uv)_n%d_kk;\n", o, it->op_sign, o, index);
		break;
	case CH_MORPH2D:
	case CH_MORPH3D:
		mm_buf_printf(b, "float %s = 42.0;", o);
		for (int i = 0; i < n; ++i) {
			model_t m = scene_to_shader_model(children_of(scene)->items[i], o_p);
			if (m.code != NULL) {
				mm_buf_add(b, m.code);
				if (count == 0) {
					const char *out = output(&m, key);
					if (sdf_error) {
						return;
					}
					mm_buf_printf(b, "%s = %s;\n", o, out);
				}
				else if (count == 1) {
					mm_buf_printf(b, "%s *= $k;\n", o);
					const char *out = output(&m, key);
					if (sdf_error) {
						return;
					}
					mm_buf_printf(b, "%s += %s*(1.0-$k);\n", o, out);
				}
				count++;
			}
		}
		break;
	}
}

// The transform prefix of the 3D items (sdf3d/boolean/union.gd)
static void transform_3d(mm_buf_t *b, mm_json_t *scene, const char *o, const char *uv) {
	mm_buf_printf(b, "vec3 %s_p = %s;\n", o, uv);
	if (non_zero(scene, "position_x", 0.0) || non_zero(scene, "position_y", 0.0) || non_zero(scene, "position_z", 0.0)) {
		mm_buf_printf(b, "%s_p -= vec3($position_x, $position_y, $position_z);\n", o);
	}
	rotate_3d(b, mm_printf("%s_p", o), scene);
	if (non_zero(scene, "scale", 1.0)) {
		mm_buf_printf(b, "%s_p /= $scale;\n", o);
	}
}

// The item's scene_to_shader_model
static model_t item_shader_model(const item_t *it, mm_json_t *scene, const char *uv) {
	char    *o   = mm_printf("$(name_uv)_n%d", scene_index(scene));
	char    *o_p = mm_printf("%s_p", o);
	mm_buf_t b   = {0};
	model_t  m   = {.empty = false, .code = NULL, .out_key = NULL, .out = o};
	int      n   = children_count(scene);
	switch (it->model) {
	case M_UNION2D:
		mm_buf_printf(&b, "vec2 %s_p = %s - vec2($position_x, $position_y);\n", o, uv);
		mm_buf_printf(&b, "%s_p = rotate(%s_p, radians($angle))/$scale;\n", o, o);
		mod_uv_code(it, scene, &b, o);
		children_code(it, scene, &b, o_p);
		mod_code(it, scene, &b, o);
		mm_buf_printf(&b, "%s *= $scale;\n", o);
		m.out_key = "sdf2d";
		break;
	case M_DIFF2D: {
		bool first = true;
		mm_buf_printf(&b, "vec2 %s_p = %s - vec2($position_x, $position_y);\n", o, uv);
		mm_buf_printf(&b, "%s_p = rotate(%s_p, radians($angle))/$scale;\n", o, o);
		mm_buf_printf(&b, "float %s = 0.0;", o);
		for (int i = 0; i < n; ++i) {
			model_t c = scene_to_shader_model(children_of(scene)->items[i], o_p);
			if (c.code != NULL) {
				mm_buf_add(&b, c.code);
				if (first) {
					mm_buf_printf(&b, "%s = %s;\n", o, output(&c, "sdf2d"));
					first = false;
				}
				else {
					mm_buf_printf(&b, "%s = max(%s, -(%s));\n", o, o, output(&c, "sdf2d"));
				}
			}
		}
		mm_buf_printf(&b, "%s *= $scale;\n", o);
		m.out_key = "sdf2d";
		break;
	}
	case M_UNION3D:
		transform_3d(&b, scene, o, uv);
		mod_uv_code(it, scene, &b, o);
		children_code(it, scene, &b, o_p);
		mod_code(it, scene, &b, o);
		if (non_zero(scene, "scale", 1.0)) {
			mm_buf_printf(&b, "%s *= $scale;\n", o);
		}
		m.out_key = "sdf3d";
		break;
	case M_DIFF3D: {
		bool first = true;
		transform_3d(&b, scene, o, uv);
		mm_buf_printf(&b, "float %s = 0.0;", o);
		for (int i = 0; i < n; ++i) {
			model_t c = scene_to_shader_model(children_of(scene)->items[i], o_p);
			if (!c.empty) {
				if (c.code != NULL) {
					mm_buf_add(&b, c.code);
				}
				if (c.out_key != NULL) {
					if (first) {
						mm_buf_printf(&b, "%s = %s;\n", o, output(&c, "sdf3d"));
						first = false;
					}
					else {
						mm_buf_printf(&b, "%s = max(%s, -(%s));\n", o, o, output(&c, "sdf3d"));
					}
				}
			}
		}
		if (non_zero(scene, "scale", 1.0)) {
			mm_buf_printf(&b, "%s *= $scale;\n", o);
		}
		m.out_key = "sdf3d";
		break;
	}
	case M_EXTRUSION:
		transform_3d(&b, scene, o, uv);
		children_code(it, scene, &b, mm_printf("%s_p.xy", o));
		mm_buf_printf(&b, "vec2 %s_w = vec2(%s, abs(%s_p.z) - $length);\n", o, o, o);
		mm_buf_printf(&b, "%s = min(max(%s_w.x, %s_w.y),0.0) + length(max(%s_w,0.0));\n", o, o, o, o);
		if (non_zero(scene, "scale", 1.0)) {
			mm_buf_printf(&b, "%s *= $scale;\n", o);
		}
		m.out_key = "sdf3d";
		break;
	case M_REVOLUTION:
		transform_3d(&b, scene, o, uv);
		mm_buf_printf(&b, "vec2 %s_q = vec2(length(%s_p.xz)-$offset, %s_p.y);\n", o, o, o);
		children_code(it, scene, &b, mm_printf("%s_q", o));
		if (non_zero(scene, "scale", 1.0)) {
			mm_buf_printf(&b, "%s *= $scale;\n", o);
		}
		m.out_key = "sdf3d";
		break;
	case M_PARAMS: // Only collects the children's parameters (none without the editor)
		for (int i = 0; i < n; ++i) {
			scene_to_shader_model(children_of(scene)->items[i], uv);
		}
		return m;
	case M_UNIFORM:
		m.code    = "";
		m.out_key = "sdf2d";
		return m;
	}
	m.code = mm_buf_get(&b);
	return m;
}

// sdf_builder.scene_to_shader_model (without the includes, only needed for the top-level items)
static model_t scene_to_shader_model(mm_json_t *scene, const char *uv) {
	model_t empty = {.empty = true};
	if (hidden(scene)) {
		return empty;
	}
	const item_t *it = find_item(scene);
	if (it == NULL) { // Unknown: {}
		return empty;
	}
	model_t m = item_shader_model(it, scene, uv);
	if (m.code != NULL) {
		m.code = replace_parameter_values(it, scene, (char *)m.code);
	}
	return m;
}

// sdf_builder.get_includes
static void get_includes(mm_json_t *scene, mm_json_t *includes) {
	if (hidden(scene)) {
		return;
	}
	const item_t *it = find_item(scene);
	for (int i = 0; it != NULL && i < 4 && it->includes[i] != NULL; ++i) { // Unknown: only the children's
		bool found = false;
		for (int j = 0; j < includes->count; ++j) {
			found = found || strcmp(includes->items[j]->string, it->includes[i]) == 0;
		}
		if (!found) {
			mm_json_t *s = mm_json_new(MM_JSON_STRING);
			s->string    = (char *)it->includes[i];
			mm_json_push(includes, s);
		}
	}
	for (int i = 0; i < children_count(scene); ++i) {
		get_includes(children_of(scene)->items[i], includes);
	}
}

// Colors

static const char *color_of(color_t c) { // .color of a child's color code
	if (c.color == NULL) {
		fail();
		return "";
	}
	return c.color;
}

static color_t color_union2d(mm_json_t *scene, ctxt_t *ctxt) {
	mm_buf_t    b          = {0};
	ctxt_t      ctxt2      = *ctxt;
	const char *else_color = "";
	bool        first      = true;
	int         n          = children_count(scene);
	ctxt2.local_uv         = mm_printf("$(name_uv)_n%d_p", scene_index(scene));
	char *distance         = mm_printf("_n%d", scene_index(scene));
	for (int i = 0; i < n; ++i) {
		mm_json_t *s = children_of(scene)->items[n - i - 1];
		if (is_category(s, "TEX")) {
			continue;
		}
		color_t c = get_color_code(s, &ctxt2);
		if (c.color == NULL) {
			continue;
		}
		if (c.distance == NULL) {
			if (else_color[0] == 0) {
				else_color = c.color;
			}
			continue;
		}
		if (first) {
			first = false;
		}
		else {
			mm_buf_add(&b, " else ");
		}
		mm_buf_printf(&b, "if (%s < 0.0) {\n%s\n}\n", c.distance, c.color);
	}
	if (else_color[0] != 0) {
		if (first) {
			mm_buf_printf(&b, "%s\n", else_color);
		}
		else {
			mm_buf_printf(&b, " else {\n%s\n}", else_color);
		}
	}
	else if (first) {
		return (color_t){.distance = distance};
	}
	return (color_t){.color = mm_buf_get(&b), .distance = distance};
}

static color_t color_union3d(mm_json_t *scene, ctxt_t *ctxt) {
	mm_buf_t b              = {0};
	ctxt_t   ctxt2          = *ctxt;
	int      index          = scene_index(scene);
	int      n              = children_count(scene);
	color_t *colored        = mm_alloc(sizeof(color_t) * (n + 1));
	int      ncolored       = 0;
	int      color_count    = 0;
	int      distance_count = 0;
	ctxt2.local_uv          = mm_printf("$(name_uv)_n%d_p", index);
	char *distance          = mm_printf("_n%d", index);
	for (int i = 0; i < n; ++i) {
		mm_json_t *s = children_of(scene)->items[i];
		if (is_category(s, "TEX")) {
			continue;
		}
		color_t c           = get_color_code(s, &ctxt2);
		colored[ncolored++] = c;
		if (c.color != NULL) {
			color_count++;
			if (c.distance != NULL) {
				distance_count++;
			}
		}
	}
	if (color_count == 0) {
		return (color_t){.distance = distance};
	}
	if (distance_count == 0) {
		for (int i = 0; i < ncolored; ++i) {
			if (colored[i].color != NULL) {
				return (color_t){.color = colored[i].color, .distance = distance};
			}
		}
	}
	bool first = true;
	for (int i = 0; i < ncolored; ++i) {
		if (colored[i].distance == NULL) {
			continue;
		}
		if (first) {
			first = false;
			mm_buf_printf(&b, "int $(name_uv)_n%d_id = %d;\nfloat $(name_uv)_n%d_best_d = %s;\n", index, i, index, colored[i].distance);
		}
		else {
			mm_buf_printf(&b, "if ($(name_uv)_n%d_best_d > %s) { $(name_uv)_n%d_best_d = %s; $(name_uv)_n%d_id = %d; }\n", index,
			              colored[i].distance, index, colored[i].distance, index, i);
		}
	}
	first                  = true;
	const char *else_color = "";
	for (int i = 0; i < ncolored; ++i) {
		if (colored[i].color == NULL) {
			continue;
		}
		if (colored[i].distance == NULL) {
			if (else_color[0] == 0) {
				else_color = colored[i].color;
			}
			continue;
		}
		if (first) {
			first = false;
		}
		else {
			mm_buf_add(&b, " else ");
		}
		mm_buf_printf(&b, "if ($(name_uv)_n%d_id == %d) {\n%s\n}\n", index, i, colored[i].color);
	}
	if (else_color[0] != 0) {
		mm_buf_printf(&b, " else {\n%s\n}", else_color);
	}
	return (color_t){.color = mm_buf_get(&b), .distance = distance};
}

// base.gd get_color_code_smooth_union
static color_t color_smooth(mm_json_t *scene, ctxt_t *ctxt) {
	mm_buf_t b     = {0};
	int      index = scene_index(scene);
	ctxt_t   ctxt2 = *ctxt;
	ctxt2.target   = mm_printf("tmp_%d", index);
	char *distance = mm_printf("_n%d", index);
	for (int i = 0; i < children_count(scene); ++i) {
		color_t c = get_color_code(children_of(scene)->items[i], &ctxt2);
		if (c.distance != NULL) {
			if (c.color != NULL) {
				mm_buf_add(&b, c.color);
			}
			else {
				mm_buf_printf(&b, "\ntmp_%d = current_color_%d;", index, index);
			}
			mm_buf_printf(&b, "\ncoef_%d = 1.0/pow(1.0+max(%s, 0.0), 10000.0*pow(0.8-0.3*clamp($color_k, 0.0, 1.0), 10.0));", index, c.distance);
			mm_buf_printf(&b, "\nsum_%d += tmp_%d*coef_%d;", index, index, index);
			mm_buf_printf(&b, "\ncoefsum_%d += coef_%d;", index, index);
		}
		else if (c.color != NULL) {
			mm_buf_add(&b, c.color);
			mm_buf_printf(&b, "\ncurrent_color_%d = tmp_%d;", index, index);
		}
	}
	if (b.len == 0) {
		return (color_t){.distance = distance};
	}
	if (ctxt->glsl_type == NULL || ctxt->channel == NULL || ctxt->target == NULL) {
		fail();
		return (color_t){.empty = true};
	}
	mm_buf_t r = {0};
	mm_buf_printf(&r, "%s sum_%d = %s(0.0);\n", ctxt->glsl_type, index, ctxt->glsl_type);
	mm_buf_printf(&r, "%s tmp_%d;\n", ctxt->glsl_type, index);
	mm_buf_printf(&r, "%s current_color_%d = %s;\n", ctxt->glsl_type, index, ctxt->channel);
	mm_buf_printf(&r, "float coef_%d;\n", index);
	mm_buf_printf(&r, "float coefsum_%d = 0.0;\n", index);
	mm_buf_add(&r, mm_buf_get(&b));
	mm_buf_printf(&r, "\n%s = sum_%d/coefsum_%d;", ctxt->target, index, index);
	return (color_t){.color = mm_buf_get(&r), .distance = distance};
}

static char *wrap_gs(const char *tex, const char *type) { // rgba / color from a greyscale value
	if (strcmp(type, "rgba") == 0) {
		return mm_printf("vec4(vec3(%s), 1.0)", tex);
	}
	if (strcmp(type, "color") == 0) {
		return mm_printf("vec3(%s)", tex);
	}
	return (char *)tex;
}

static char *wrap_rgba(const char *tex, const char *type) { // color / float from a vec4
	if (strcmp(type, "color") == 0) {
		return mm_printf("(%s).xyz", tex);
	}
	if (strcmp(type, "float") == 0) {
		return mm_printf("dot((%s).xyz, vec3(1.0))/3.0", tex);
	}
	return (char *)tex;
}

// The item's get_color_code
static color_t item_color_code(const item_t *it, mm_json_t *scene, ctxt_t *ctxt) {
	int         n        = children_count(scene);
	mm_json_t  *children = children_of(scene);
	bool        sdf3d    = ctxt->geometry != NULL && strcmp(ctxt->geometry, "sdf3d") == 0;
	const char *type     = ctxt->type != NULL ? ctxt->type : "";
	switch (it->color) {
	case CL_UNION2D:
		return color_union2d(scene, ctxt);
	case CL_UNION3D:
		return color_union3d(scene, ctxt);
	case CL_SMOOTH:
		return color_smooth(scene, ctxt);
	case CL_EXTRUSION:
	case CL_REVOLUTION: {
		ctxt_t ctxt2   = *ctxt;
		ctxt2.uv       = mm_printf("$(name_uv)_n%d_%s", scene_index(scene), it->color == CL_EXTRUSION ? "p.xy" : "q");
		ctxt2.local_uv = ctxt2.uv;
		color_t c      = color_union2d(scene, &ctxt2);
		return (color_t){.color = c.color, .distance = mm_printf("_n%d", scene_index(scene))};
	}
	case CL_CHANNEL: {
		if (ctxt->channel == NULL) {
			fail();
			return (color_t){.empty = true};
		}
		if (strcmp(it->channel, ctxt->channel) != 0) {
			return (color_t){.empty = true};
		}
		ctxt_t ctxt2   = *ctxt;
		ctxt2.geometry = "sdf3d";
		if (truthy(param_value(scene, "local"))) {
			if (ctxt->local_uv == NULL) {
				fail();
				return (color_t){.empty = true};
			}
			ctxt2.uv = ctxt->local_uv;
		}
		for (int i = 0; i < n; ++i) {
			color_t c = get_color_code(children->items[i], &ctxt2);
			if (!c.empty) {
				if (ctxt->target == NULL) {
					fail();
					return (color_t){.empty = true};
				}
				return (color_t){.color = mm_printf("%s = %s;", ctxt->target, color_of(c))};
			}
		}
		return (color_t){.empty = true};
	}
	case CL_UNIFORM:
		if (strcmp(type, "color") == 0) {
			return (color_t){.color = "($color).xyz"};
		}
		if (strcmp(type, "float") == 0) {
			return (color_t){.color = "dot(($color).xyz, vec3(1.0))/3.0"};
		}
		return (color_t){.color = "$color"};
	case CL_FBM:
	case CL_PATTERN:
	case CL_UNIFORM_GS: {
		char *tex;
		if (it->color == CL_UNIFORM_GS) {
			tex = "$value";
		}
		else if (it->color == CL_FBM) {
			tex = sdf3d ? mm_printf("fbm3d_$noise(%s, vec3($scale_x, $scale_y, $scale_z), int($iterations), $persistence, 0.0)", ctxt->uv)
			            : mm_printf("fbm_2d_$noise(%s, vec2($scale_x, $scale_y), int($folds), int($iterations), $persistence, $offset, 0.0)", ctxt->uv);
		}
		else {
			const char *uv = ctxt->uv;
			tex = sdf3d ? mm_printf("mix3d_$mix(wave_$x_wave($x_scale*%s.x), wave_$y_wave($y_scale*%s.y), wave_$z_wave($z_scale*%s.z))", uv, uv, uv)
			            : mm_printf("mix_$mix(wave_$x_wave($x_scale*%s.x), wave_$y_wave($y_scale*%s.y))", uv, uv);
		}
		return (color_t){.color = wrap_gs(tex, type)};
	}
	case CL_BLEND: {
		const char *tex   = "vec4(1.0)";
		ctxt_t      ctxt2 = *ctxt;
		ctxt2.type        = "rgba";
		if (n > 0) {
			tex = color_of(get_color_code(children->items[0], &ctxt2));
			for (int i = 1; i < n; i += 2) {
				ctxt2.type       = "rgba";
				const char *tex2 = color_of(get_color_code(children->items[i], &ctxt2));
				const char *mask = "0.5";
				if (n > i + 1) {
					ctxt2.type = "float";
					mask       = color_of(get_color_code(children->items[i + 1], &ctxt2));
				}
				tex = mm_printf("mix(%s, %s, %s)", tex, tex2, mask);
			}
		}
		return (color_t){.color = wrap_rgba(tex, type)};
	}
	case CL_BRIGHTNESS_CONTRAST: {
		const char *child = strcmp(type, "rgba") == 0 ? "vec4(vec3(0.5), 1.0)" : strcmp(type, "color") == 0 ? "vec3(0.5)" : "0.5";
		if (n > 0) {
			color_t c = get_color_code(children->items[0], ctxt);
			if (c.color != NULL) {
				child = c.color;
			}
		}
		const char *t = strcmp(type, "rgba") == 0 ? "vec4" : strcmp(type, "color") == 0 ? "vec3" : "";
		char       *tex;
		if (strcmp(type, "rgba") == 0) {
			tex = mm_printf("%s*vec4(vec3($contrast), 1.0)+vec4(vec3($brightness+0.5-$contrast*0.5), 0.0)", child);
		}
		else {
			tex = mm_printf("%s*$contrast+%s($brightness+0.5-$contrast*0.5)", child, t);
		}
		mm_json_t *clamp = param_value(scene, "clamp");
		if (clamp == NULL || truthy(clamp)) {
			tex = mm_printf("clamp(%s, %s(0.0), %s(1.0))", tex, t, t);
		}
		return (color_t){.color = tex};
	}
	case CL_DEFORM: {
		const char *tex = "";
		if (n == 0) {
			tex = wrap_gs("1.0", type);
		}
		else {
			const char *deform = "vec3(0.0)";
			if (n > 1) {
				ctxt_t ctxt2 = *ctxt;
				ctxt2.type   = "color";
				color_t c    = get_color_code(children->items[1], &ctxt2);
				if (c.color != NULL) {
					deform = c.color;
				}
			}
			ctxt_t ctxt2 = *ctxt;
			if (sdf3d) {
				ctxt2.uv = mm_printf("(%s+$amount*(%s.xyz-vec3(0.5)))", ctxt->uv, deform);
			}
			else {
				ctxt2.uv = mm_printf("(%s+$amount*(%s.xy-vec2(0.5)))", ctxt->uv, deform);
			}
			color_t c = get_color_code(children->items[0], &ctxt2);
			if (c.color != NULL) {
				tex = c.color;
			}
		}
		return (color_t){.color = tex};
	}
	case CL_STEP: {
		const char *child = "0.0";
		if (n > 0) {
			ctxt_t ctxt2 = *ctxt;
			ctxt2.type   = "float";
			child        = color_of(get_color_code(children->items[0], &ctxt2));
		}
		char *tex = mm_printf("clamp((%s-$value)/max(0.0001, $width)+0.5, 0.0, 1.0)", child);
		if (truthy(param_value(scene, "invert"))) {
			tex = mm_printf("(1.0-%s)", tex);
		}
		return (color_t){.color = wrap_gs(tex, type)};
	}
	case CL_GRADIENT: {
		const char *tex;
		ctxt_t      ctxt2 = *ctxt;
		ctxt2.type        = "float";
		if (n == 0) {
			tex = "$color1";
		}
		else {
			tex = mm_printf("mix($color1, $color2, %s)", color_of(get_color_code(children->items[0], &ctxt2)));
		}
		return (color_t){.color = wrap_rgba(tex, type)};
	}
	}
	return (color_t){.empty = true};
}

// sdf_builder.get_color_code
static color_t get_color_code(mm_json_t *scene, ctxt_t *ctxt) {
	if (hidden(scene)) {
		return (color_t){.empty = true};
	}
	const item_t *it = find_item(scene);
	if (it == NULL) { // Unknown: {}
		return (color_t){.empty = true};
	}
	color_t c = item_color_code(it, scene, ctxt);
	if (c.color != NULL) {
		c.color = replace_parameter_values(it, scene, (char *)c.color);
	}
	return c;
}

// gen_sdf.gd set_sdf_scene

static mm_json_t *jstr(const char *s) {
	mm_json_t *v = mm_json_new(MM_JSON_STRING);
	v->string    = mm_strdup(s);
	return v;
}

static mm_json_t *joutput(const char *key, const char *value, const char *type, const char *shortdesc) {
	mm_json_t *o = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(o, key, jstr(value));
	mm_json_set(o, "type", jstr(type));
	if (shortdesc != NULL) {
		mm_json_set(o, "shortdesc", jstr(shortdesc));
	}
	return o;
}

// sdf_scene: the node's "sdf_scene" array, or the saved node itself (an object with "sdf_scene" and "node_parameters",
// the parameters the node defines, which the scene's parmexprs may use).
mm_json_t *mm_sdf_shader_model(mm_json_t *sdf_scene) {
	mm_json_t *node_parameters = NULL;
	if (sdf_scene != NULL && sdf_scene->type == MM_JSON_OBJECT) {
		node_parameters = mm_json_get(sdf_scene, "node_parameters");
		sdf_scene       = mm_json_get(sdf_scene, "sdf_scene");
	}
	if (sdf_scene == NULL || sdf_scene->type != MM_JSON_ARRAY) {
		return NULL;
	}
	sdf_error = false;
	sdf_top   = sdf_scene;

	const char *scene_type = ""; // get_scene_type, "" for an unknown type
	if (sdf_scene->count > 0) {
		const item_t *it = find_item(sdf_scene->items[0]);
		if (it != NULL) {
			scene_type = it->category;
		}
	}
	bool        is3d  = strcmp(scene_type, "SDF3D") == 0;
	bool        is2d  = strcmp(scene_type, "SDF2D") == 0;
	const char *uv    = is3d ? "$uv" : "$uv-vec2(0.5)";
	const char *field = is2d ? "sdf2d" : "sdf3d";

	mm_json_t *includes = mm_json_new(MM_JSON_ARRAY);
	mm_buf_t   d        = {0};
	mm_buf_t   c        = {0};
	mm_buf_add(&d, is3d ? "float $(name)_d(vec3 uv" : "float $(name)_d(vec2 uv");
	mm_buf_add(&c, is3d ? "float $(name)_c(vec3 uv" : "float $(name)_c(vec2 uv");
	mm_buf_add(&d, ") {\n");
	mm_buf_add(&c, ", out vec4 albedo, out float metallic, out float roughness, out vec3 emission) {\n");
	mm_buf_printf(&c, "albedo = %s;\n", is3d ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec4(0.0, 0.0, 0.0, 1.0)");
	mm_buf_add(&c, "metallic = 0.0;\n");
	mm_buf_add(&c, "roughness = 1.0;\n");
	mm_buf_add(&c, "emission = vec3(0.0);\n");

	mm_json_t *parameters = mm_json_new(MM_JSON_ARRAY);
	for (int i = 0; node_parameters != NULL && i < node_parameters->count; ++i) {
		mm_json_t *p = mm_json_copy(node_parameters->items[i]);
		if (p == NULL || p->type != MM_JSON_OBJECT) {
			continue;
		}
		mm_json_t *shortdesc = mm_json_get(p, "shortdesc");
		mm_json_set(p, "label", shortdesc != NULL ? mm_json_copy(shortdesc) : jstr(""));
		mm_json_push(parameters, p);
	}

	bool first = true;
	for (int i = 0; i < sdf_scene->count; ++i) {
		mm_json_t *item = sdf_scene->items[i];
		if (hidden(item)) {
			continue;
		}
		model_t m = scene_to_shader_model(item, "uv");
		if (m.empty) {
			continue;
		}
		mm_json_t *item_includes = mm_json_new(MM_JSON_ARRAY);
		get_includes(item, item_includes);
		for (int j = 0; j < item_includes->count; ++j) {
			mm_json_push(includes, item_includes->items[j]);
		}
		if (m.code != NULL) {
			char *code = mm_replace(m.code, "$(name_uv)", "");
			mm_buf_add(&d, code);
			mm_buf_add(&c, code);
		}
		if (m.out_key != NULL && strcmp(m.out_key, field) == 0) {
			char *output_name = mm_replace(m.out, "$(name_uv)", "");
			if (first) {
				mm_buf_printf(&d, "float return_value = %s;\n", output_name);
				mm_buf_printf(&c, "float return_value = %s;\n", output_name);
				first = false;
			}
			else {
				mm_buf_printf(&d, "return_value = min(return_value, %s);\n", output_name);
				mm_buf_printf(&c, "return_value = min(return_value, %s);\n", output_name);
			}
		}
	}

	mm_json_t fake_scene = {0};
	fake_scene.type      = MM_JSON_OBJECT;
	mm_json_set(&fake_scene, "index", mm_json_new(MM_JSON_NUMBER));
	mm_json_set(&fake_scene, "type", jstr(is2d ? "Union" : "Union3D"));
	mm_json_set(&fake_scene, "children", sdf_scene);
	mm_json_set(&fake_scene, "parameters", mm_json_new(MM_JSON_OBJECT));
	static const struct {
		const char *channel, *type, *glsl_type, *title;
	} channels[] = {
		{"albedo", "rgba", "vec4", "Albedo"},
		{"metallic", "float", "float", "Metallic"},
		{"roughness", "float", "float", "Roughness"},
		{"emission", "color", "vec3", "Emission"},
	};
	for (int i = 0; i < 4; ++i) {
		ctxt_t  ctxt = {.uv = "uv", .channel = channels[i].channel, .target = channels[i].channel, .type = channels[i].type, .glsl_type = channels[i].glsl_type};
		color_t cc   = get_color_code(&fake_scene, &ctxt);
		if (cc.color != NULL) {
			mm_buf_printf(&c, "\n// %s\n", channels[i].title);
			mm_buf_printf(&c, "{\n%s}\n", cc.color);
		}
	}
	mm_buf_add(&c, "\n");
	if (first) {
		mm_buf_add(&d, "return 1.0;");
		mm_buf_add(&c, "return 1.0;");
	}
	else {
		mm_buf_add(&d, "return return_value;");
		mm_buf_add(&c, "return return_value;");
	}
	mm_buf_add(&d, "}\n");
	mm_buf_add(&c, "}\n");
	sdf_top = NULL;
	if (sdf_error) {
		return NULL;
	}

	mm_json_t *model = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(model, "includes", includes);
	mm_json_set(model, "parameters", parameters);
	mm_json_set(model, "instance", jstr(mm_printf("%s%s", mm_buf_get(&d), mm_replace(mm_buf_get(&c), "$(name_uv)", ""))));
	mm_buf_t code = {0};
	mm_buf_add(&code, "vec4 $(name_uv)_albedo;\n");
	mm_buf_add(&code, "float $(name_uv)_metallic;\n");
	mm_buf_add(&code, "float $(name_uv)_roughness;\n");
	mm_buf_add(&code, "vec3 $(name_uv)_emission;\n");
	mm_json_t *outputs = mm_json_new(MM_JSON_ARRAY);
	if (is3d) {
		mm_buf_printf(&code, "$(name)_c(%s.xyz*vec3(1.0, -1.0, -1.0), $(name_uv)_albedo, $(name_uv)_metallic, $(name_uv)_roughness, $(name_uv)_emission);\n", uv);
		mm_json_push(outputs, joutput("sdf3d", mm_printf("@NOCODE $(name)_d(%s*vec3(1.0, -1.0, -1.0))", uv), "sdf3d", NULL));
		mm_json_push(outputs, joutput("tex3d", "$(name_uv)_albedo.rgb", "tex3d", "Albedo"));
		mm_json_push(outputs, joutput("tex3d_gs", "clamp($(name_uv)_metallic, 0.0, 1.0)", "tex3d_gs", "Metallic"));
		mm_json_push(outputs, joutput("tex3d_gs", "clamp($(name_uv)_roughness, 0.0, 1.0)", "tex3d_gs", "Roughness"));
		mm_json_push(outputs, joutput("tex3d", "$(name_uv)_emission", "tex3d", "Emission"));
	}
	else {
		mm_buf_printf(&code, "$(name)_c(%s, $(name_uv)_albedo, $(name_uv)_metallic, $(name_uv)_roughness, $(name_uv)_emission);\n", uv);
		mm_json_push(outputs, joutput("sdf2d", mm_printf("@NOCODE $(name)_d(%s)", uv), "sdf2d", "SDF"));
		mm_json_push(outputs, joutput("rgba", "$(name_uv)_albedo", "rgba", "Albedo"));
		mm_json_push(outputs, joutput("f", "$(name_uv)_metallic", "f", "Metallic"));
		mm_json_push(outputs, joutput("f", "$(name_uv)_roughness", "f", "Roughness"));
		mm_json_push(outputs, joutput("rgb", "$(name_uv)_emission", "rgb", "Emission"));
	}
	mm_json_set(model, "code", jstr(mm_buf_get(&code)));
	mm_json_set(model, "outputs", outputs);
	return model;
}
