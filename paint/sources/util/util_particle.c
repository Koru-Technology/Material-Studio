
#include "../global.h"

void util_particle_init_mesh() {
	if (g_context->paint_body != NULL) {
		return;
	}
	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}
	if (g_context->merged_object == NULL) {
		return; // Nothing to paint
	}

	g_context->paint_body = physics_body_create(g_context->merged_object->base, PHYSICS_SHAPE_MESH, 0.0);
}

// One particle from the emitter, k of count: shapes spread the particles over the brush radius on the surface
static bool util_particle_emit(i32 k, i32 count) {
	i32 slot = -1;
	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer == NULL) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		return false;
	}

	f32 nx = g_context->norx_picked;
	f32 ny = g_context->nory_picked;
	f32 nz = g_context->norz_picked;

	// Tangent frame of the surface
	vec4_t n  = (vec4_t){nx, ny, nz, 0.0};
	vec4_t up = fabsf(nz) < 0.9f ? (vec4_t){0.0, 0.0, 1.0, 0.0} : (vec4_t){1.0, 0.0, 0.0, 0.0};
	vec4_t t  = vec4_norm(vec4_cross(up, n));
	vec4_t b  = vec4_cross(n, t);
	f32    r  = g_context->brush_radius * 0.5f;
	f32    ox = 0.0f;
	f32    oy = 0.0f;
	if (g_context->particle_shape == 1) { // Disk
		f32 a = math_random() * math_pi() * 2.0f;
		f32 d = sqrtf(math_random()) * r;
		ox    = cosf(a) * d;
		oy    = sinf(a) * d;
	}
	else if (g_context->particle_shape == 2) { // Ring
		f32 a = (k + math_random() * 0.5f) / (f32)count * math_pi() * 2.0f;
		ox    = cosf(a) * r;
		oy    = sinf(a) * r;
	}
	else if (g_context->particle_shape == 3) { // Line across the screen
		vec4_t right = camera_object_right_world(scene_camera);
		f32    f     = count > 1 ? (k / (f32)(count - 1)) * 2.0f - 1.0f : math_random() * 2.0f - 1.0f;
		ox           = vec4_dot(right, t) * f * r;
		oy           = vec4_dot(right, b) * f * r;
	}

	object_t      *o  = scene_spawn_object(".Sphere", NULL, true);
	mesh_object_t *mo = o->ext;
	mo->base->name    = ".Bullet";
	mo->base->visible = true;
	mo->material      = make_particle_get_bullet_material();

	f32 sphere_radius = g_context->brush_radius * 0.1f;
	f32 spawn_h       = -(sphere_radius + g_context->particle_spawn_distance) + 0.01;
	f32 px            = g_context->posx_picked + t.x * ox + b.x * oy;
	f32 py            = g_context->posy_picked + t.y * ox + b.y * oy;
	f32 pz            = g_context->posz_picked + t.z * ox + b.z * oy;
	mo->base->transform->loc   = (vec4_t){px + nx * spawn_h, py + ny * spawn_h, pz + nz * spawn_h, 1.0};
	mo->base->transform->scale = (vec4_t){g_context->brush_radius * 0.2, g_context->brush_radius * 0.2, g_context->brush_radius * 0.2, 1.0};
	transform_build_matrix(mo->base->transform);

	physics_body_t *body = physics_body_create(mo->base, PHYSICS_SHAPE_SPHERE, g_context->particle_mass);

	// Random direction
	f32 rx   = math_random() * 2.0f - 1.0f;
	f32 ry   = math_random() * 2.0f - 1.0f;
	f32 rz   = math_random() * 2.0f - 1.0f;
	f32 rlen = sqrtf(rx * rx + ry * ry + rz * rz);
	if (rlen < 0.0001f)
		rlen = 0.0001f;
	rx /= rlen;
	ry /= rlen;
	rz /= rlen;
	if (rx * nx + ry * ny + rz * nz < 0.0f) {
		rx = -rx;
		ry = -ry;
		rz = -rz;
	}
	f32 rr      = g_context->particle_random;
	f32 dx      = nx + (rx - nx) * rr;
	f32 dy      = ny + (ry - ny) * rr;
	f32 dz      = nz + (rz - nz) * rr;
	f32 impulse = g_context->particle_spawn_distance * 30.0f;
	physics_body_apply_impulse(body->_body, (vec4_t){dx * impulse, dy * impulse, dz * impulse, 0.0});

	g_context->particles[slot].body   = body;
	g_context->particles[slot].bullet = mo->base;
	g_context->particles[slot].timer  = tween_timer(g_context->particle_lifetime, NULL, NULL);
	return true;
}

// Particle presets of the tool options
typedef struct util_particle_preset {
	char *name;
	f32   lifetime, distance, mass, random, friction, bounce, gravity_z;
	i32   shape, count;
	f32   rate;
} util_particle_preset_t;

// clang-format off
static util_particle_preset_t util_particle_presets[] = {
	{"Drip",     6.0, 0.0,  1.0, 0.0, 0.2, 0.0, -9.81, 0, 1, 4.0},
	{"Splatter", 1.5, 0.6,  0.5, 0.9, 0.5, 0.0, -2.0,  1, 6, 3.0},
	{"Rain",     4.0, 0.0,  1.0, 0.05,0.0, 0.0, -9.81, 1, 4, 8.0},
	{"Leak",     8.0, 0.05, 2.0, 0.1, 0.05,0.0, -9.81, 3, 3, 2.0},
	{"Dust",     2.0, 0.3,  0.1, 1.0, 0.8, 0.3, -0.5,  1, 8, 6.0},
};
// clang-format on

i32 util_particle_preset_count() {
	return sizeof(util_particle_presets) / sizeof(util_particle_presets[0]);
}

char *util_particle_preset_name(i32 i) {
	return tr(util_particle_presets[i].name);
}

void util_particle_apply_preset(i32 i) {
	util_particle_preset_t *p          = &util_particle_presets[i];
	g_context->particle_lifetime       = p->lifetime;
	g_context->particle_spawn_distance = p->distance;
	g_context->particle_mass           = p->mass;
	g_context->particle_random         = p->random;
	g_context->particle_friction       = p->friction;
	g_context->particle_bounciness     = p->bounce;
	g_context->particle_gravity_x      = 0.0;
	g_context->particle_gravity_y      = 0.0;
	g_context->particle_gravity_z      = p->gravity_z;
	g_context->particle_shape          = p->shape;
	g_context->particle_count          = p->count;
	g_context->particle_rate           = p->rate;
	physics_set_friction(g_context->particle_friction);
	physics_set_bounciness(g_context->particle_bounciness);
	physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
}

void util_particle_update() {
	if (g_context->tool != TOOL_TYPE_PARTICLE) {
		return;
	}

	util_particle_init_mesh();
	make_particle_get_bullet_material();
	physics_world_update();

	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer != NULL && g_context->particles[i].timer->delay <= 0) {
			physics_body_remove(g_context->particles[i].body);
			mesh_object_remove((mesh_object_t *)g_context->particles[i].bullet->ext);
			memset(&g_context->particles[i], 0, sizeof(g_context->particles[i]));
		}
	}

	bool any_active = false;
	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer != NULL) {
			any_active = true;
			break;
		}
	}
	if (any_active) {
		g_context->ddirty = 2;
		iron_delay_idle_sleep();
	}

	static f64 particle_last_spawn_time = 0.0;
	bool       particle_just_fired      = false;
	f32        rate                     = g_context->particle_rate > 0.1f ? g_context->particle_rate : 0.1f;
	if (mouse_down("left") && context_in_paint_area() && !g_context->paint2d &&
	    (mouse_started("left") || sys_time() - particle_last_spawn_time >= 1.0 / rate)) {
		particle_last_spawn_time = sys_time();

		if (mouse_started("left")) {
			layers_fill_paint_to_mask(); // A fill layer is painted through its mask
			history_push_undo            = true;
			g_context->brush_blend_dirty = true;
		}
		util_render_pick_pos_nor_tex();
		i32 count = g_context->particle_count > 0 ? g_context->particle_count : 1;
		for (i32 k = 0; k < count; ++k) {
			if (util_particle_emit(k, count)) {
				particle_just_fired = true;
			}
		}
	}

	if (mouse_released("left")) {
		g_context->layer_preview_dirty = true;
	}

#ifdef WITH_PHYSICS
	if (!particle_just_fired) {
		for (i32 i = 0; i < 32; ++i) {
			if (g_context->particles[i].timer == NULL) {
				continue;
			}
			physics_pair_t_array_t *pairs = physics_get_contact_pairs(g_context->particles[i].body);
			if (pairs != NULL && pairs->length > 0) {
				physics_pair_t *p                  = pairs->buffer[0];
				bool was_hit                       = g_context->particles[i].hit;
				g_context->particles[i].hit_last_x = was_hit ? g_context->particles[i].hit_x : p->pos_a_x;
				g_context->particles[i].hit_last_y = was_hit ? g_context->particles[i].hit_y : p->pos_a_y;
				g_context->particles[i].hit_last_z = was_hit ? g_context->particles[i].hit_z : p->pos_a_z;
				g_context->particles[i].hit_x      = p->pos_a_x;
				g_context->particles[i].hit_y      = p->pos_a_y;
				g_context->particles[i].hit_z      = p->pos_a_z;
				g_context->particles[i].hit_nor_x  = p->nor_x;
				g_context->particles[i].hit_nor_y  = p->nor_y;
				g_context->particles[i].hit_nor_z  = p->nor_z;
				g_context->particles[i].contact_time += sys_delta();
				g_context->particles[i].hit = true;
				g_context->pdirty           = 1;
			}
			else {
				g_context->particles[i].hit   = false;
				g_context->particles[i].hit_x = 0.0;
			}
		}
	}
#endif
}
