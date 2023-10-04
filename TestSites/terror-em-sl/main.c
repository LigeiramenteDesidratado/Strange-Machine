#include "core/smArray.h"
#include "core/smCore.h"
#include "core/smLog.h"

#include "ecs/smECS.h"

#include "ecs/smStage.h"
#include "math/smCollision.h"
#include "math/smMath.h"
#include "math/smShape.h"

#include "animation/smAnimation.h"
#include "audio/smAudio.h"
#include "effects/smShake.h"
#include "renderer/smRenderer.h"

entity_t player_ett;

struct intersect_result
rigid_body_intersects(struct scene *scene, rigid_body_component *rb)
{
	struct intersect_result best_result = {0};

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH | STATIC_BODY);

	u32 next = 0;
	while (scene_iter_next(scene, &iter))
	{
		static_body_component *static_body = scene_iter_get_component(&iter, STATIC_BODY);
		if (!static_body->enabled) { continue; }

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		struct intersect_result result;
		switch (rb->collision_shape)
		{
		case RB_SHAPE_CAPSULE: result = collision_capsule_mesh(rb->capsule, mesh, transform); break;
		case RB_SHAPE_SPHERE: result = collision_sphere_mesh(rb->sphere, mesh, transform); break;
		default: sm__unreachable();
		}

		if (result.valid)
		{
			if ((!best_result.valid) || (result.depth > best_result.depth)) { best_result = result; }
		}
	}

	return (best_result);
}

void
rigid_body_handle_capsule(
    struct scene *scene, struct ctx *ctx, entity_t entity, rigid_body_component *rb, transform_component *transform)
{
	v3 position = transform->transform_local.translation.v3;
	v3 c;
	glm_vec3_sub(rb->capsule.tip.data, rb->capsule.base.data, c.data);
	f32 height = glm_vec3_norm(c.data);
	f32 radius = rb->capsule.radius;

	b8 ground_intersect = false;

	const u32 ccd_max = 5;
	for (u32 i = 0; i < ccd_max; ++i)
	{
		v3 step;
		glm_vec3_scale(rb->velocity.data, 1.0f / ccd_max * ctx->fixed_dt, step.data);
		glm_vec3_add(position.data, step.data, position.data);

		v3 tip;
		glm_vec3_add(position.data, v3_new(0.0f, height, 0.0f).data, tip.data);
		rb->capsule = (struct capsule){.base = position, .tip = tip, radius};

		struct intersect_result result = rigid_body_intersects(scene, rb);
		if (!result.valid) { continue; }

		v3 r_norm;
		glm_vec3_copy(result.normal.data, r_norm.data);

		f32 capsule_mag = glm_vec3_norm(rb->velocity.data);

		v3 capsule_direction;
		glm_vec3_normalize_to(rb->velocity.data, capsule_direction.data);

		v3 undesired_motion;
		f32 scale = glm_vec3_dot(capsule_direction.data, r_norm.data);
		glm_vec3_scale(r_norm.data, scale, undesired_motion.data);

		v3 desired_motion;
		glm_vec3_sub(capsule_direction.data, undesired_motion.data, desired_motion.data);

		glm_vec3_scale(desired_motion.data, capsule_mag, rb->velocity.data);

		// Remove penetration (penetration epsilon added to handle infinitely small penetration)
		v3 penetration;

		glm_vec3_scale(r_norm.data, result.depth + GLM_FLT_EPSILON, penetration.data);
		glm_vec3_add(position.data, penetration.data, position.data);
	}

	if (rb->has_gravity)
	{
		v3 g_force;
		glm_vec3_scale(scene->gravity_force.data, ctx->fixed_dt, g_force.data);

		if (glm_vec3_norm(g_force.data) != 0.0f)
		{
			glm_vec3_add(rb->gravity.data, g_force.data, rb->gravity.data);

			for (u32 i = 0; i < ccd_max; ++i)
			{
				v3 step;
				glm_vec3_scale(rb->gravity.data, 1.0f / ccd_max * ctx->fixed_dt, step.data);
				glm_vec3_add(position.data, step.data, position.data);

				v3 tip;
				glm_vec3_add(position.data, v3_new(0.0f, height, 0.0f).data, tip.data);
				rb->capsule = (struct capsule){.base = position, .tip = tip, radius};

				struct intersect_result result = rigid_body_intersects(scene, rb);
				if (!result.valid) { continue; }

				// Remove penetration (penetration epsilon added to handle infinitely
				// small penetration):
				v3 penetration;
				glm_vec3_scale(result.normal.data, result.depth + 0.0001f, penetration.data);
				glm_vec3_add(position.data, penetration.data, position.data);

				// Check whether it is intersecting the ground (ground normal is
				// upwards)
				if (glm_vec3_dot(result.normal.data, v3_up().data) > 0.3f)
				{
					ground_intersect = true;
					glm_vec3_scale(rb->gravity.data, 0, rb->gravity.data);
					break;
				}
			}
		}
	}

	const f32 GROUND_FRICTION = 0.7;
	const f32 AIR_FRICTION = 0.62;
	if (ground_intersect) { glm_vec3_scale(rb->velocity.data, GROUND_FRICTION, rb->velocity.data); }
	else { glm_vec3_scale(rb->velocity.data, AIR_FRICTION, rb->velocity.data); }

	v3 sub;
	v3 original_position = transform->transform_local.translation.v3;
	glm_vec3_sub(position.data, original_position.data, sub.data);
	scene_entity_translate(scene, entity, sub);
	// transform_translate(transform, sub);
	// glm_vec3_add(
	//     transform->transform_local.position.v3.data, sub.data, transform->transform_local.position.v3.data);
	// transform_set_dirty(transform, true);
}

void
rigid_body_handle_sphere(
    struct scene *scene, struct ctx *ctx, entity_t entity, rigid_body_component *rb, transform_component *transform)
{
	v3 position = transform->transform_local.translation.v3;
	f32 radius = rb->sphere.radius;

	b8 ground_intersect = false;

	const u32 ccd_max = 5;
	for (u32 i = 0; i < ccd_max; ++i)
	{
		v3 step;
		glm_vec3_scale(rb->velocity.data, 1.0f / ccd_max * ctx->fixed_dt, step.data);
		glm_vec3_add(position.data, step.data, position.data);

		rb->sphere.center = position;

		struct intersect_result result = rigid_body_intersects(scene, rb);
		if (!result.valid) { continue; }

		v3 r_norm;
		glm_vec3_copy(result.normal.data, r_norm.data);

		f32 velocity_len = glm_vec3_norm(rb->velocity.data);

		v3 velocity_norm;
		glm_vec3_normalize_to(rb->velocity.data, velocity_norm.data);

		v3 undesired_motion;
		glm_vec3_scale(r_norm.data, glm_vec3_dot(velocity_norm.data, r_norm.data), undesired_motion.data);

		v3 desired_motion;
		glm_vec3_sub(velocity_norm.data, undesired_motion.data, desired_motion.data);

		glm_vec3_scale(desired_motion.data, velocity_len, rb->velocity.data);

		// Remove penetration (penetration epsilon added to handle infinitely small penetration)
		v3 penetration;
		glm_vec3_scale(r_norm.data, result.depth + 0.0001f, penetration.data);
		glm_vec3_add(position.data, penetration.data, position.data);
	}

	if (rb->has_gravity)
	{
		v3 g_force;
		glm_vec3_scale(scene->gravity_force.data, ctx->fixed_dt, g_force.data);

		if (glm_vec3_norm(g_force.data) != 0.0f)
		{
			glm_vec3_add(rb->gravity.data, g_force.data, rb->gravity.data);
			for (u32 i = 0; i < ccd_max; ++i)
			{
				v3 step;
				glm_vec3_scale(rb->gravity.data, 1.0f / ccd_max * ctx->fixed_dt, step.data);
				glm_vec3_add(position.data, step.data, position.data);

				rb->sphere.center = position;

				struct intersect_result result = rigid_body_intersects(scene, rb);
				if (!result.valid) { continue; }

				// Remove penetration (penetration epsilon added to handle infinitely small
				// penetration)
				v3 penetration;
				glm_vec3_scale(result.normal.data, result.depth + 0.0001f, penetration.data);
				glm_vec3_add(position.data, penetration.data, position.data);

				// Check whether it is intersecting the ground (ground normal is upwards)
				if (glm_vec3_dot(result.normal.data, v3_up().data) > 0.3f)
				{
					ground_intersect = true;
					glm_vec3_scale(rb->gravity.data, 0, rb->gravity.data);
					break;
				}
			}
		}
	}

	const f32 GROUND_FRICTION = 0.7;
	const f32 AIR_FRICTION = 0.62;
	if (ground_intersect) { glm_vec3_scale(rb->velocity.data, GROUND_FRICTION, rb->velocity.data); }
	else { glm_vec3_scale(rb->velocity.data, AIR_FRICTION, rb->velocity.data); }

	v3 sub;
	v3 original_position = transform->transform_local.translation.v3;
	glm_vec3_sub(position.data, original_position.data, sub.data);
	// transform_translate(transform, sub);

	scene_entity_translate(scene, entity, sub);
	// glm_vec3_add(transform->transform_local.position.v3.data, sub.data, transform->position.v3.data);
	// transform_set_dirty(transform, true);
}

b8
scene_rigid_body_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | RIGID_BODY);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		rigid_body_component *rb = scene_iter_get_component(&iter, RIGID_BODY);
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);

		switch (rb->collision_shape)
		{
		case RB_SHAPE_CAPSULE: rigid_body_handle_capsule(scene, ctx, entity, rb, transform); break;
		case RB_SHAPE_SPHERE: rigid_body_handle_sphere(scene, ctx, entity, rb, transform); break;
		default: sm__unreachable();
		};

		glm_vec3_clamp(rb->velocity.data, -16.0f, 16.0f);
	}

	return (true);
}

static v3
random_point_inside_aabb(struct aabb aabb)
{
	v3 result;

	result.x = f32_min_max(aabb.min.x, aabb.max.x);
	result.y = f32_min_max(aabb.min.y, aabb.max.y);
	result.z = f32_min_max(aabb.min.z, aabb.max.z);

	return (result);
}

static v3
random_point_inside_cube(trs cube)
{
	v3 result;

	f32 min_x = cube.scale.x * -0.5f;
	f32 min_y = cube.scale.y * -0.5f;
	f32 min_z = cube.scale.z * -0.5f;

	result.x = f32_min_max(min_x, -min_x);
	result.y = f32_min_max(min_y, -min_y);
	result.z = f32_min_max(min_z, -min_z);

	return (result);
}

b8
scene_particle_emitter_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | PARTICLE_EMITTER);
	while (scene_iter_next(scene, &iter))
	{
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		particle_emitter_component *pe = scene_iter_get_component(&iter, PARTICLE_EMITTER);

#if defined(PARTICLE_USE_POOL)

		for (u32 i = 0; i < pe->pool_size; ++i)
		{
			struct particle *p = &pe->particles_pool[i];
			p->energy_remaing -= ctx->fixed_dt;
			if (p->energy_remaing > 0.0f)
			{
				v3 velocity;
				glm_vec3_scale(p->velocity.data, ctx->fixed_dt, velocity.data);
				glm_vec3_add(p->position.data, velocity.data, p->position.data);
			}
		}

		u32 last_free_particle_index = 0;
		for (u32 rate = 0; (rate < pe->emission_rate) && (last_free_particle_index < pe->pool_size); ++rate)
		{
			for (u32 i = last_free_particle_index; i < pe->pool_size; ++i)
			{
				struct particle *p = &pe->particles_pool[i];
				if (p->energy_remaing > 0.0f) { continue; }

				last_free_particle_index = i;

				v3 position;
				switch (pe->shape_type)
				{
				case EMISSION_SHAPE_AABB:
					{
						struct aabb aabb;
						glm_aabb_transform(pe->box.data, transform->matrix.data, aabb.data);
						position = random_point_inside_aabb(aabb);
					}
					break;
				case EMISSION_SHAPE_CUBE:
					{
						position = random_point_inside_cube(pe->cube);
						glm_mat4_mulv3(
						    transform->matrix.data, position.data, 1.0f, position.data);
						// trs t = trs_from_m4(transform->matrix);
						// position = trs_point(t, position);
					}
					break;
				default: position = v3_zero(); break;
				}

				// glm_mat4_mulv3(transform->matrix.data, position.data, 1.0f, position.data);
				p->position = position;

				p->color_begin = color_from_hex(u32_prng() | 0x000000FF);
				p->color_end = color_from_hex(u32_prng() & 0xFFFFFF00);

				p->velocity = v3_new(0, (f32_range01() + 1) * 5, 0);

				p->energy = f32_min_max(0.2f, 0.7f);
				p->energy_remaing = p->energy;
			}
		}

#else
		struct particle *p, *next;
		for (p = pe->active_sentinel.next, next = 0; p != &pe->active_sentinel; p = next)
		{
			next = p->next;

			p->energy_remaing -= ctx->fixed_dt;
			if (p->energy_remaing > 0.0f)
			{
				v3 velocity;
				glm_vec3_scale(p->velocity.data, ctx->fixed_dt, velocity.data);
				glm_vec3_add(p->position.data, velocity.data, p->position.data);
			}
			else
			{
				dll_remove(p);
				dll_insert(&pe->free_sentinel, p);
			}
		}

		if (!pe->enable) { continue; }

		u32 rate;
		for (p = pe->free_sentinel.next, next = 0, rate = 0;
		     (p != &pe->free_sentinel && rate < pe->emission_rate); p = next, ++rate)
		{
			next = p->next;
			dll_remove(p);
			dll_insert(&pe->active_sentinel, p);

			v3 position;

			switch (pe->shape_type)
			{
			case EMISSION_SHAPE_AABB:
				{
					struct aabb aabb;
					glm_aabb_transform(pe->box.data, transform->matrix.data, aabb.data);
					position = random_point_inside_aabb(aabb);
				}
				break;
			case EMISSION_SHAPE_CUBE:
				{
					position = random_point_inside_cube(pe->cube);
					glm_mat4_mulv3(transform->matrix.data, position.data, 1.0f, position.data);
					// trs t = trs_from_m4(transform->matrix);
					// position = trs_point(t, position);
				}
				break;
			default: position = v3_zero(); break;
			}

			// glm_mat4_mulv3(transform->matrix.data, position.data, 1.0f, position.data);
			p->position = position;

			p->color_begin = color_from_hex(u32_prng() | 0x000000FF);
			p->color_end = color_from_hex(u32_prng() & 0xFFFFFF00);

			p->velocity = v3_new(0, (f32_range01() + 1) * 5, 0);

			p->energy = f32_min_max(0.2f, 0.7f);
			p->energy_remaing = p->energy;
		}
#endif
	}

	return (true);
}

b8
scene_particle_emitter_sort_update(sm__maybe_unused struct arena *arena, sm__maybe_unused struct scene *scene,
    sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	entity_t main_camera_entity = scene_get_main_camera(scene);
	transform_component *camera_transform = scene_component_get_data(scene, main_camera_entity, TRANSFORM);
	v3 camera_position = camera_transform->matrix.v3.position;

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | PARTICLE_EMITTER);

	while (scene_iter_next(scene, &iter))
	{
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		particle_emitter_component *pe = scene_iter_get_component(&iter, PARTICLE_EMITTER);

#if defined(PARTICLE_USE_POOL)
		for (u32 i = 0; i < pe->pool_size; ++i)
		{
			struct particle *p1 = &pe->particles_pool[i];
			f32 p1_distance = glm_vec3_distance2(p1->position.data, camera_position.data);
			for (u32 j = i + 1; j < pe->pool_size; ++j)
			{
				struct particle *p2 = &pe->particles_pool[j];
				if (p1->energy_remaing <= 0.0f)
				{
					*p1 = *p2;
					p2->energy_remaing = 0.0f;
					continue;
				}

				f32 p2_distance = glm_vec3_distance2(p2->position.data, camera_position.data);
				if (p2_distance < p1_distance)
				{
					struct particle p_swap = *p1;
					*p1 = *p2;
					*p2 = p_swap;
				}
			}
		}

#else
		struct particle *p = 0, *i = 0, *next = 0;
		for (p = pe->active_sentinel.next; p != &pe->active_sentinel; p = next)
		{
			next = p->next;

			// TODO: particle position in world space

			// TODO: swap particles instead of using double linked list. Its better for the cache

			// squared distance between the particle and the camera
			f32 particle_distance = glm_vec3_distance2(p->position.data, camera_position.data);
			for (i = next; i != &pe->active_sentinel; i = i->next)
			{
				if (particle_distance < glm_vec3_distance2(i->position.data, camera_position.data))
				{
					dll_remove(p);
					dll_insert(i, p);
				}
			}
		}
#endif
	}

	return (true);
}

b8
scene_mesh_calculate_aabb_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, MESH);
	while (scene_iter_next(scene, &iter))
	{
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		if (mesh->mesh_ref->flags & MESH_FLAG_DIRTY)
		{
			mesh_calculate_aabb(mesh);
			mesh->mesh_ref->flags &= ~(u32)MESH_FLAG_DIRTY;
		}
	}
	return (true);
}

static struct shake shake;

void
camera_focus_on_selected_entity(
    struct scene *scene, camera_component *cam, transform_component *camera_transform, sm__maybe_unused struct ctx *ctx)
{
	// TODO: fix me
	entity_t focus = {INVALID_HANDLE};
	if (focus.handle)
	{
		transform_component *target_transform = scene_component_get_data(scene, focus, TRANSFORM);

		v3 target_position = target_transform->matrix.v3.position;
		v3 camera_position = camera_transform->matrix.v3.position;

		cam->free.lerp_to_target_position = target_position;

		v3 target_direction;
		glm_vec3_sub(cam->free.lerp_to_target_position.data, camera_position.data, target_direction.data);
		glm_vec3_normalize(target_direction.data);

		if (scene_entity_has_components(scene, focus, MESH))
		{
			mesh_component *mesh = scene_component_get_data(scene, focus, MESH);
			struct aabb aabb;
			glm_aabb_transform(mesh->mesh_ref->aabb.data, target_transform->matrix.data, aabb.data);

			v3 extents;
			glm_vec3_sub(aabb.max.data, aabb.min.data, extents.data);
			glm_vec3_scale(extents.data, 0.5f, extents.data);

			glm_vec3_scale(target_direction.data, glm_vec3_norm(extents.data), target_direction.data);
			glm_vec3_scale(target_direction.data, 2, target_direction.data);

			glm_vec3_sub(cam->free.lerp_to_target_position.data, target_direction.data,
			    cam->free.lerp_to_target_position.data);
		}
		else
		{
			glm_vec3_sub(cam->free.lerp_to_target_position.data, target_direction.data,
			    cam->free.lerp_to_target_position.data);
		}

		v3 direction;
		glm_vec3_sub(cam->free.lerp_to_target_position.data, target_position.data, direction.data);
		glm_quat_for(direction.data, v3_up().data, cam->free.lerp_to_target_rotation.data);
		glm_quat_normalize(cam->free.lerp_to_target_rotation.data);

		f32 dist = glm_vec3_distance(cam->free.lerp_to_target_position.data, camera_position.data);
		cam->free.lerp_to_target_distance = dist;

		v4 target_rotation, camera_rotation;
		glm_quat_normalize_to(cam->free.lerp_to_target_rotation.data, target_rotation.data);

		glm_mat4_quat(camera_transform->matrix.data, camera_rotation.data);
		glm_quat_normalize(camera_rotation.data);

		f32 lerp_angle = acosf(glm_quat_dot(target_rotation.data, camera_rotation.data));
		lerp_angle = glm_deg(lerp_angle);

		cam->free.lerp_to_target_p = cam->free.lerp_to_target_distance > 0.1f;
		cam->free.lerp_to_target_r = lerp_angle > 1.0f;
	}
}

void
camera_lerp_to_entity(struct scene *scene, entity_t entity, camera_component *cam,
    transform_component *camera_transform, sm__maybe_unused struct ctx *ctx)
{
	// Set focused entity as a lerp target
	if (core_key_pressed_lock(KEY_F, 60)) { camera_focus_on_selected_entity(scene, cam, camera_transform, ctx); }

	// Lerp
	if (cam->free.lerp_to_target_p || cam->free.lerp_to_target_r)
	{
		// Lerp duration in seconds
		// 2.0 seconds + [0.0 - 2.0] seconds based on distance
		// Something is not right with the duration...
		f32 lerp_duration = 2.0f + glm_clamp(cam->free.lerp_to_target_distance * 0.01f, 0.0f, 2.0f);

		// Alpha
		cam->free.lerp_to_target_alpha += (ctx->dt) / lerp_duration;

		// Position
		if (cam->free.lerp_to_target_p)
		{
			v3 interpolated_position;
			glm_vec3_lerp(camera_transform->matrix.v3.position.data, cam->free.lerp_to_target_position.data,
			    cam->free.lerp_to_target_alpha, interpolated_position.data);

			// transform_set_position(camera_transform, interpolated_position);
			scene_entity_set_position(scene, entity, interpolated_position);
		}

		// Rotation
		if (cam->free.lerp_to_target_r)
		{
			v4 interpolated_rotation;

			v4 q;
			glm_mat4_quat(camera_transform->matrix.data, q.data);

			f32 amount = glm_clamp_zo(cam->free.lerp_to_target_alpha);
			glm_quat_lerp(
			    q.data, cam->free.lerp_to_target_rotation.data, amount, interpolated_rotation.data);

			// transform_set_rotation(camera_transform, interpolated_rotation);
			scene_entity_set_rotation(scene, entity, interpolated_rotation);
		}

		// If the lerp has completed or the user has initiated fps control, stop lerping.
		if (cam->free.lerp_to_target_alpha >= 1.0f || cam->free.is_controlled_by_keyboard_mouse)
		{
			cam->free.lerp_to_target_p = false;
			cam->free.lerp_to_target_r = false;
			cam->free.lerp_to_target_alpha = 0.0f;
			cam->free.lerp_to_target_position = v3_zero();
		}
	}
}

void
scene_camera_update_input(struct scene *scene, entity_t entity, camera_component *camera,
    transform_component *transform, sm__maybe_unused struct ctx *ctx)
{
	v2 offset = core_get_cursor_offset();
	offset.y = -offset.y;
	f32 wheel = core_get_scroll();

	const float movement_acceleration = 1.0f;
	const f32 movement_speed_max = 5.0f;
	const f32 movement_drag = 10.0f;

	if (camera->flags & CAMERA_FLAG_FREE)
	{
		// Detect if fps control should be activated
		{
			// Initiate control only when the mouse is within the viewport
			if (core_button_pressed(MOUSE_BUTTON_LEFT) && core_is_cursor_in_window())
			{
				camera->free.is_controlled_by_keyboard_mouse = true;
			}

			// Maintain control as long as the right click is pressed and initial control has been
			// given
			camera->free.is_controlled_by_keyboard_mouse =
			    core_button_pressed(MOUSE_BUTTON_LEFT) && camera->free.is_controlled_by_keyboard_mouse;
		}

		{
			// Toggle mouse cursor and adjust mouse position
			if (camera->free.is_controlled_by_keyboard_mouse && !core_is_cursor_hidden())
			{
				camera->free.mouse_last_position = core_get_window_cursor_position();
				core_hide_cursor();
			}
			else if (!camera->free.is_controlled_by_keyboard_mouse && core_is_cursor_hidden())
			{
				core_set_cursor_pos(camera->free.mouse_last_position);
				core_show_cursor();
			}
		}

		v3 movement_direction = v3_zero();
		if (camera->free.is_controlled_by_keyboard_mouse)
		{
			// Wrap around left and right screen edges (to allow for infinite scrolling)
			{
				uint32_t edge_padding = 5;
				v2 mouse_position = core_get_screen_cursor_position();
				if (mouse_position.x >= 1359 - edge_padding)
				{
					mouse_position.x = edge_padding + 1;
					core_set_cursor_pos(mouse_position);
				}
				else if (mouse_position.x <= edge_padding)
				{
					mouse_position.x = 1359 - edge_padding - 1;
					core_set_cursor_pos(mouse_position);
				}
			}

			// Get camera rotation.
			v4 q;
			glm_mat4_quat(transform->matrix.data, q.data);
			v3 angles = quat_to_euler_angles(q);

			camera->free.rotation_deg.x = glm_deg(angles.yaw);
			camera->free.rotation_deg.y = glm_deg(angles.pitch);

			f32 mouse_sensitivity = 0.2f;

			v2 mouse_delta;
			glm_vec2_scale(offset.data, -1 * mouse_sensitivity, mouse_delta.data);

			f32 mouse_smoothing = 0.5f;
			glm_vec2_lerp(camera->free.mouse_smoothed.data, mouse_delta.data,
			    glm_clamp_zo(1.0f - mouse_smoothing), camera->free.mouse_smoothed.data);

			glm_vec2_add(camera->free.rotation_deg.data, camera->free.mouse_smoothed.data,
			    camera->free.rotation_deg.data);

			// clamp rotation along the x-axis (but not exactly at 90 degrees, this is to avoid a
			// gimbal lock).
			camera->free.rotation_deg.y = glm_clamp(camera->free.rotation_deg.y, -75.0f, 75.0f);

			v4 xq, yq;
			glm_quatv(xq.data, glm_rad(camera->free.rotation_deg.x), v3_up().data);
			glm_quatv(yq.data, glm_rad(camera->free.rotation_deg.y), v3_right().data);

			v4 rotation;
			glm_quat_mul(xq.data, yq.data, rotation.data);

			// Rotate
			// transform_set_rotation_local(transform, rotation);
			scene_entity_set_rotation_local(scene, entity, rotation);

			// Compute direction
			if (core_key_pressed(KEY_W))
			{
				v3 forward = trs_get_forward(transform->transform_local);
				glm_vec3_add(movement_direction.data, forward.data, movement_direction.data);
			}
			if (core_key_pressed(KEY_S))
			{
				v3 backward = trs_get_backward(transform->transform_local);
				glm_vec3_add(movement_direction.data, backward.data, movement_direction.data);
			}
			if (core_key_pressed(KEY_D))
			{
				v3 right = trs_get_right(transform->transform_local);
				glm_vec3_sub(movement_direction.data, right.data, movement_direction.data);
			}
			if (core_key_pressed(KEY_A))
			{
				v3 left = trs_get_left(transform->transform_local);
				glm_vec3_sub(movement_direction.data, left.data, movement_direction.data);
			}
			if (core_key_pressed(KEY_Q))
			{
				v3 down = trs_get_down(transform->transform_local);
				glm_vec3_add(movement_direction.data, down.data, movement_direction.data);
			}
			if (core_key_pressed(KEY_E))
			{
				v3 up = trs_get_up(transform->transform_local);
				glm_vec3_add(movement_direction.data, up.data, movement_direction.data);
			}

			glm_vec3_normalize(movement_direction.data);

			// Wheel delta (used to adjust movement speed)
			{
				// Accumulate
				camera->free.movement_scroll_accumulator += wheel * 0.1f;

				// Prevent it from negating or zeroing the acceleration, see translation calculation.
				f32 min = -movement_acceleration + 0.1f;
				f32 max = movement_acceleration * 2.0f; // An empirically chosen max.

				camera->free.movement_scroll_accumulator =
				    glm_clamp(camera->free.movement_scroll_accumulator, min, max);
			}
		}

		v3 translation;
		glm_vec3_scale(movement_direction.data,
		    movement_acceleration + camera->free.movement_scroll_accumulator, translation.data);

		if (core_key_pressed(KEY_LEFT_SHIFT)) { glm_vec3_scale(translation.data, 2.0f, translation.data); }

		glm_vec3_scale(translation.data, ctx->dt, translation.data);
		glm_vec3_add(camera->free.speed.data, translation.data, camera->free.speed.data);

		// Apply drag
		glm_vec3_scale(camera->free.speed.data, 1.0f - movement_drag * ctx->dt, camera->free.speed.data);

		// clamp it
		if (glm_vec3_norm(camera->free.speed.data) > movement_speed_max)
		{
			glm_vec3_scale_as(camera->free.speed.data, movement_speed_max, camera->free.speed.data);
		}

		// translate for as long as there is speed
		if (!glm_vec3_eq(camera->free.speed.data, 0.0f))
		{
			// transform_translate(transform, camera->free.speed);
			scene_entity_translate(scene, entity, camera->free.speed);
		}
	}
	else if (camera->flags & CAMERA_FLAG_THIRD_PERSON)
	{
		core_hide_cursor();

		v4 q;
		glm_mat4_quat(transform->matrix.data, q.data);

		v3 angles = quat_to_euler_angles(q);
		camera->third_person.rotation_deg.x = glm_deg(angles.yaw);
		camera->third_person.rotation_deg.y = glm_deg(angles.pitch);

		f32 mouse_sensitivity = 0.5f;

		v2 mouse_delta;
		glm_vec2_scale(offset.data, -1 * mouse_sensitivity, mouse_delta.data);

		f32 mouse_smoothing = 0.5f;
		glm_vec2_lerp(camera->third_person.mouse_smoothed.data, mouse_delta.data,
		    glm_clamp_zo(1.0f - mouse_smoothing), camera->third_person.mouse_smoothed.data);

		glm_vec2_add(camera->third_person.rotation_deg.data, camera->third_person.mouse_smoothed.data,
		    camera->third_person.rotation_deg.data);

		// clamp rotation along the x-axis (but not exactly at 90 degrees, this is to avoid a
		// gimbal lock).
		camera->third_person.rotation_deg.y = glm_clamp(camera->third_person.rotation_deg.y, -75.0f, 75.0f);

		// Zoom
		if (wheel < 0.0f) { camera->third_person.target_distance *= 1.2f; }
		else if (wheel > 0.0f) { camera->third_person.target_distance /= 1.2f; }
		camera->third_person.target_distance = glm_clamp(camera->third_person.target_distance, 1.0f, 12.0f);

		v4 xq, yq;
		v4 rotation;
		glm_quatv(xq.data, glm_rad(camera->third_person.rotation_deg.x), v3_up().data);
		glm_quatv(yq.data, glm_rad(camera->third_person.rotation_deg.y), v3_right().data);
		glm_quat_mul(xq.data, yq.data, rotation.data);

		// Calculate the camera's position based on the target's position and rotation
		v3 offset;
		glm_vec3_scale(v3_forward().data, -camera->third_person.target_distance, offset.data);
		glm_quat_rotatev(rotation.data, offset.data, offset.data);

		// Update the camera's position
		v3 new_position;
		glm_vec3_add(camera->third_person.target.data, offset.data, new_position.data);

		// Camera occlusion
		trs ray_position = trs_lookat(camera->third_person.target, new_position, v3_up());
		struct ray ray;
		ray.position = ray_position.translation.v3;
		ray.direction = trs_get_backward(ray_position);

		struct intersect_result best_result = {0};
		struct scene_iter inner_iter = stage_iter_begin(TRANSFORM | STATIC_BODY | MESH);
		while (stage_iter_next(&inner_iter))
		{
			transform_component *transform = scene_iter_get_component(&inner_iter, TRANSFORM);
			mesh_component *mesh = scene_iter_get_component(&inner_iter, MESH);

			struct intersect_result result = collision_ray_mesh(ray, mesh, transform);
			if (result.valid)
			{
				if (!best_result.valid || result.depth < best_result.depth) { best_result = result; }
			}
		}

		if (best_result.valid)
		{
			if (best_result.depth <= camera->third_person.target_distance)
			{
				v3 offset;
				glm_vec3_scale(best_result.normal.data, 0.1f, offset.data);
				glm_vec3_add(best_result.position.data, offset.data, offset.data);
				new_position = offset;
			}
		}

		shake_do(&shake, &new_position);
		// transform_set_position(transform, new_position);
		scene_entity_set_position(scene, entity, new_position);

		// Update the camera's orientation
		// transform_set_rotation_local(transform, rotation);
		scene_entity_set_rotation_local(scene, entity, rotation);
	}

	camera_lerp_to_entity(scene, entity, camera, transform, ctx);
}

b8
scene_camera_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CAMERA | TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		camera_component *cam = scene_iter_get_component(&iter, CAMERA);
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		entity_t camera_ett = scene_iter_get_entity(&iter);

		cam->aspect_ratio = (f32)ctx->framebuffer_width / (f32)ctx->framebuffer_height;

		scene_camera_update_input(scene, camera_ett, cam, transform, ctx);

		// Get the view matrix
		v3 eye = transform->matrix.v3.position;
		v3 up = trs_get_up(transform->transform_local);
		v3 look_at = trs_get_forward(transform->transform_local);

		glm_look(eye.data, look_at.data, up.data, cam->view.data);

		// Get the projection matrix
		f32 fovy = camera_get_fov_y(cam);
		glm_perspective(fovy, cam->aspect_ratio, cam->z_near, cam->z_far, cam->projection.data);

		// Cache view projection matrix
		glm_mat4_mul(cam->projection.data, cam->view.data, cam->view_projection.data);

		// Sound
		// v3 look_at_inv;
		// glm_vec3_inv_to(transform->matrix.v3.forward.data, look_at_inv.data);
		audio_set_listener_position(eye);
		audio_set_listener_direction(transform->matrix.v3.forward);
	}

	return (true);
}

b8
scene_transform_clear_dirty(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		scene_entity_set_dirty(scene, entity, false);
	}
	return (true);
}

b8
scene_cfc_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CROSS_FADE_CONTROLLER | ARMATURE | POSE | CLIP);
	while (scene_iter_next(scene, &iter))
	{
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);

		if (clip->current_clip_ref == 0) { continue; }
		u32 targets = array_len(cfc->targets);
		for (u32 i = 0; i < targets; ++i)
		{
			f32 duration = cfc->targets[i].duration;

			if (cfc->targets[i].elapsed >= duration)
			{
				clip->current_clip_ref = cfc->targets[i].clip_ref;
				clip->time = cfc->targets[i].time;
				pose_copy(arena, current, cfc->targets[i].pose_ref);

				array_del(cfc->targets, i, 1);
				break;
			}
		}

		targets = array_len(cfc->targets);
		struct pose *rest = &armature->armature_ref->rest;

		pose_copy(arena, current, rest);
		clip->time = clip_sample(clip->current_clip_ref, current, clip->time + ctx->dt);

		for (u32 i = 0; i < targets; ++i)
		{
			struct cross_fade_target *target = &cfc->targets[i];
			target->time = clip_sample(target->clip_ref, target->pose_ref, target->time + ctx->dt);
			target->elapsed += ctx->dt;
			f32 t = target->elapsed / target->duration;
			if (t > 1.0f) { t = 1.0f; }
			pose_blend(current, current, target->pose_ref, t, -1);
		}
	}
	return (true);
}

b8
scene_fade_to_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CROSS_FADE_CONTROLLER | ARMATURE | POSE | CLIP);

	while (scene_iter_next(scene, &iter))
	{
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);
		if (clip->next_clip_ref == 0) { continue; }
		if (clip->current_clip_ref == 0 && clip->next_clip_ref == 0) { continue; }

		if (clip->current_clip_ref == 0 && clip->next_clip_ref != 0)
		{
			array_set_len(arena, cfc->targets, 0);
			clip->current_clip_ref = clip->next_clip_ref;
			clip->time = clip->current_clip_ref->start_time;
			struct pose *rest = &armature->armature_ref->rest;
			pose_copy(arena, current, rest);
			continue;
		}

		u32 targets = array_len(cfc->targets);
		if (targets >= 1)
		{
			if (cfc->targets[targets - 1].clip_ref == clip->next_clip_ref) { continue; }
		}
		else if (clip->current_clip_ref == clip->next_clip_ref) { continue; }

		struct cross_fade_target target = {
		    .pose_ref = &armature->armature_ref->rest,
		    .clip_ref = clip->next_clip_ref,
		    .duration = 0.5,
		    .time = clip->next_clip_ref->start_time,
		    .elapsed = 0.0f,
		};

		array_push(arena, cfc->targets, target);
	}
	return (true);
}

b8
scene_m4_palette_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, MESH | ARMATURE | POSE);

	while (scene_iter_next(scene, &iter))
	{
		pose_component *current = scene_iter_get_component(&iter, POSE);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);

		sm__assert(mesh->mesh_ref->flags & MESH_FLAG_SKINNED);
		struct arena *resource_arena = resource_get_arena();
		pose_get_matrix_palette(current, resource_arena, &mesh->mesh_ref->skin_data.pose_palette);

		for (u32 i = 0; i < array_len(mesh->mesh_ref->skin_data.pose_palette); ++i)
		{
			glm_mat4_mul(mesh->mesh_ref->skin_data.pose_palette[i].data,
			    armature->armature_ref->inverse_bind[i].data,
			    mesh->mesh_ref->skin_data.pose_palette[i].data);
		}
	}
	return (true);
}

#define ANIM_IDLE      0
#define ANIM_JUMP2     1
#define ANIM_JUMP      2
#define ANIM_LEAN_LEFT 3
#define ANIM_PICKUP    4
#define ANIM_PUNCH     5
#define ANIM_RUNNING   6
#define ANIM_SITIDLE   7
#define ANIM_SITTING   8
#define ANIM_WALKING   9

static str8 ctable_animation_names[] = {
    str8_from("woman-idle"),
    str8_from("woman-jump2"),
    str8_from("woman-jump"),
    str8_from("woman-lean-left"),
    str8_from("woman-pickup"),
    str8_from("woman-punch"),
    str8_from("woman-run"),
    str8_from("woman-sit-idle"),
    str8_from("woman-sit"),
    str8_from("woman-walk"),

};

b8
scene_player_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(
	    scene, TRANSFORM | MESH | MATERIAL | ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER | RIGID_BODY | PLAYER);
	while (scene_iter_next(scene, &iter))
	{
		entity_t cam_ett = scene_get_main_camera(scene);
		camera_component *camera = scene_component_get_data(scene, cam_ett, CAMERA); // TODO

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		// world_component *world = scene_iter_get_component(&iter, WORLD);
		// hierarchy_component *hierarchy = scene_iter_get_component(&iter, HIERARCHY);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		material_component *material = scene_iter_get_component(&iter, MATERIAL);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		rigid_body_component *rb = scene_iter_get_component(&iter, RIGID_BODY);
		player_component *player = scene_iter_get_component(&iter, PLAYER);
		entity_t player_ett = scene_iter_get_entity(&iter);

		v3 input;
		if (camera->flags & CAMERA_FLAG_FREE) { input = v3_zero(); }
		else
		{
			input = v3_new(core_key_pressed(KEY_A) - core_key_pressed(KEY_D), 0,
			    core_key_pressed(KEY_S) - core_key_pressed(KEY_W));
		}

		v3 direction;
		glm_vec3_normalize_to(input.data, direction.data);
		f32 dir_len = glm_vec3_norm(direction.data);

		if (dir_len > 0.2f)
		{
			m4 view = camera->view;

			v3 fwd = view.v3.forward;
			fwd.y = 0.0f;
			fwd.x = -fwd.x;

			v3 right = view.v3.right;
			right.x = -right.x;
			right.y = 0.0f;

			glm_vec3_scale(fwd.data, direction.z, fwd.data);
			glm_vec3_scale(right.data, direction.x, right.data);

			glm_vec3_add(fwd.data, right.data, direction.data);

			f32 angle = atan2f(direction.x, direction.z);

			v4 rotation;
			glm_quatv(rotation.data, angle, v3_up().data);
			glm_quat_slerp(transform->transform_local.rotation.data, rotation.data, .5f, rotation.data);
			// transform_set_rotation(transform, rotation);
			scene_entity_set_rotation(scene, player_ett, rotation);
			player->state = ANIM_WALKING;
		}
		else { player->state = ANIM_IDLE; }

		f32 sprint = 1.0f;
		if (player->state == ANIM_WALKING && core_key_pressed(KEY_LEFT_SHIFT))
		{
			sprint = 2.5f;
			player->state = ANIM_RUNNING;
		}
		glm_vec3_scale(direction.data, sprint * 25.0f * ctx->dt, direction.data);

		glm_vec3_add(rb->velocity.data, direction.data, rb->velocity.data);

		// glm_vec3_lerp(camera->target.data, transform->position.v3.data, .5f, camera->target.data);
		v3 target = transform->transform_local.translation.v3;

		if (glm_vec3_norm(rb->velocity.data) < 0.01f) { player->state = ANIM_IDLE; }

		v3 c;
		glm_vec3_sub(rb->capsule.tip.data, rb->capsule.base.data, c.data);
		f32 height = glm_vec3_norm(c.data);
		target.y += height;

		camera->third_person.target = target;

		// glm_vec3_smoothinterp(camera->target.data, target.data, 10.0f * ctx->dt, camera->target.data);

		if (core_key_pressed(KEY_H)) { player->state = ANIM_LEAN_LEFT; }
		struct resource *resource_clip = resource_get_by_name(ctable_animation_names[player->state]);
		if (resource_clip) { clip->next_clip_ref = resource_clip->data; }
		else { log_warn(str8_from("[{s}] clip resource not found"), ctable_animation_names[player->state]); }

		if (player->state == ANIM_RUNNING)
		{
			for (u32 i = 0; i < array_len(clip->current_clip_ref->tracks); ++i)
			{
				struct transform_track *tt = &clip->current_clip_ref->tracks[i];
				f32 t = clip_adjust_time(clip->current_clip_ref, clip->time);

				i32 frame_index = track_frame_index(&tt->position, t, clip->current_clip_ref->looping);
				i32 trigger_frames[2] = {0, 8};

				b8 found = false;
				for (u32 j = 0; j < ARRAY_SIZE(trigger_frames); ++j)
				{
					if (frame_index == trigger_frames[j])
					{
						str8 audios[] = {
						    str8_from("step1"), str8_from("step2"), str8_from("step3")};
						u32 idx = prng_min_max(0, ARRAY_SIZE(audios));
						audio_set_position(audios[idx], rb->capsule.base);
						audio_play(audios[idx]);
						found = true;
						break;
					}
				}
				if (found) { break; }
			}
		}
		if (player->state == ANIM_WALKING)
		{
			for (u32 i = 0; i < array_len(clip->current_clip_ref->tracks); ++i)
			{
				struct transform_track *tt = &clip->current_clip_ref->tracks[i];
				f32 t = clip_adjust_time(clip->current_clip_ref, clip->time);

				i32 frame_index = track_frame_index(&tt->position, t, clip->current_clip_ref->looping);
				i32 trigger_frames[2] = {10, 24};

				b8 found = false;
				for (u32 j = 0; j < ARRAY_SIZE(trigger_frames); ++j)
				{
					if (frame_index == trigger_frames[j])
					{
						str8 audios[] = {
						    str8_from("step1"), str8_from("step2"), str8_from("step3")};
						u32 idx = prng_min_max(0, ARRAY_SIZE(audios));
						audio_set_position(audios[idx], rb->capsule.base);
						audio_play(audios[idx]);
						found = true;
						break;
					}
				}
				if (found) { break; }
			}
		}
	}

	return (true);
}

b8
scene_player_update_viniL(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(
	    scene, TRANSFORM | MESH | MATERIAL | ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER | RIGID_BODY | PLAYER);
	while (scene_iter_next(scene, &iter))
	{
		entity_t cam_ett = scene_get_main_camera(scene);
		camera_component *camera = scene_component_get_data(scene, cam_ett, CAMERA); // TODO

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		// world_component *world = scene_iter_get_component(&iter, WORLD);
		// hierarchy_component *hierarchy = scene_iter_get_component(&iter, HIERARCHY);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		material_component *material = scene_iter_get_component(&iter, MATERIAL);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		rigid_body_component *rb = scene_iter_get_component(&iter, RIGID_BODY);
		player_component *player = scene_iter_get_component(&iter, PLAYER);

		v3 input = v3_new(core_key_pressed(KEY_A) - core_key_pressed(KEY_D), 0,
		    core_key_pressed(KEY_W) - core_key_pressed(KEY_S));

		v3 direction;
		glm_vec3_normalize_to(input.data, direction.data);
		f32 dir_len = glm_vec3_norm(direction.data);

		if (dir_len != 0.0f)
		{
			m4 view = (camera->view);

			v3 fwd = view.v3.forward;
			fwd.y = 0.0f;
			fwd.z = -fwd.z;

			v3 right = view.v3.right;
			right.y = 0.0f;
			right.x = -right.x;

			glm_vec3_scale(fwd.data, direction.z, fwd.data);
			glm_vec3_scale(right.data, direction.x, right.data);

			glm_vec3_add(fwd.data, right.data, direction.data);

			f32 angle = atan2f(direction.x, direction.z);

			v4 rotation;
			glm_quatv(rotation.data, angle, v3_up().data);
			glm_quat_slerp(transform->transform_local.rotation.data, rotation.data, .5f,
			    transform->transform_local.rotation.data);
			// transform_set_dirty(transform, true);
			// player->state = ANIM_WALKING;
		}
		else
		{ /* player->state = ANIM_IDLE; */
		}

		f32 sprint = 1.0f;
		if (core_key_pressed(KEY_LEFT_SHIFT))
		{
			sprint = 2.5f;
			// player->state = ANIM_RUNNING;
		}
		glm_vec3_scale(direction.data, sprint * 18.0f * ctx->dt, direction.data);

		// if (camera->fov < 12.0f) { camera->fov = 12.0f; }
		// else if (camera->fov > 80.0f) { camera->fov = 80.0f; }
		glm_vec3_add(rb->velocity.data, direction.data, rb->velocity.data);

		// glm_vec3_lerp(camera->target.data, transform->position.v3.data, .5f, camera->target.data);
		v3 target = transform->transform_local.translation.v3;

		if (glm_vec3_norm(rb->velocity.data) < 0.01f)
		{ /*  player->state = ANIM_IDLE;  */
		}

		v3 c;
		glm_vec3_sub(rb->capsule.tip.data, rb->capsule.base.data, c.data);
		f32 height = glm_vec3_norm(c.data);
		target.y += height;
		camera->third_person.target = target;

		// glm_vec3_smoothinterp(camera->target.data, target.data, 10.0f * ctx->dt, camera->target.data);

		// if (core_key_pressed(KEY_F)) { player->state = ANIM_SITIDLE; }
		// struct resource *resource_clip = resource_get_by_name(ctable_animation_names[player->state]);
		// sm__assert(resource_clip);
		// clip->next_clip_ref = resource_clip->data;
	}

	return (true);
}

b8
scene_hierarchy_update(sm__maybe_unused struct arena *arena, sm__maybe_unused struct scene *scene,
    sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		if (scene_entity_is_dirty(scene, entity)) { scene_entity_update_hierarchy(scene, entity); }
	}

	return (true);
}

void on_attach(sm__maybe_unused struct ctx *ctx);
void on_update(sm__maybe_unused struct ctx *ctx);
void on_draw(sm__maybe_unused struct ctx *ctx);
void on_detach(sm__maybe_unused struct ctx *ctx);

i32
main(i32 argc, i8 *argv[])
{
#if 0 // TIKOTEKO
#	define WIDTH  432
#	define HEIGHT 768
#	define SCALE  0.9
#elif 1 // silent hill
#	define WIDTH		   800
#	define HEIGHT		   600
#	define FRAMEBUFFER_WIDTH  320
#	define FRAMEBUFFER_HEIGHT 224
#	define SCALE		   0.5
#else
#	define WIDTH  800
#	define HEIGHT 450
#	define SCALE  0.5
#endif
	struct core_init init_c = {
	    .argc = argc,
	    .argv = argv,
	    .title = str8_from("Terror em SL"),
	    .w = WIDTH,
	    .h = HEIGHT,
	    .total_memory = MB(32),

	    .framebuffer_w = FRAMEBUFFER_WIDTH,
	    .framebuffer_h = FRAMEBUFFER_HEIGHT,
	    .target_fps = 24,
	    .fixed_fps = 48,

	    .prng_seed = 42,
	    .user_data = 0,
	    .assets_folder = str8_from("assets/"),
	    .num_layers = 1,
	    .layer_init[0] = {.name = str8_from("Main layer"),
			      .on_attach = on_attach,
			      .on_update = on_update,
			      .on_draw = on_draw,
			      .on_detach = on_detach},
	};

	if (!core_init(&init_c))
	{
		puts("error initializing core");
		return (EXIT_FAILURE);
	}
	core_main_loop();
	core_teardown();
	return (EXIT_SUCCESS);
}

void
on_attach(sm__maybe_unused struct ctx *ctx)
{
	shake = shake_make(1, 60, 0.5f);

	sm___resource_mock_read();

	// TODO: this should be per scene
	renderer_set_clear_color(cGRAY);

	stage_scene_new(str8_from("main scene"));
	{
		entity_t camera_ett = stage_entity_new(CAMERA | TRANSFORM);
		stage_set_gravity_force(v3_new(0.0f, -9.8f, 0.0f));
		stage_set_main_camera(camera_ett);

		camera_component *camera = stage_component_get_data(camera_ett, CAMERA);
		transform_component *camera_transform = stage_component_get_data(camera_ett, TRANSFORM);
		{
			camera_transform->matrix_local = m4_identity();
			camera_transform->transform_local = trs_identity();
			camera_transform->transform_local.translation.v3 = v3_new(0.0f, 5.0f, 5.0f);

			camera_transform->matrix = m4_identity();
			camera_transform->last_matrix = m4_identity();
		}
		{
			camera->z_near = 0.1f;
			camera->z_far = 100.0f;
			camera->fovx = glm_rad(75.0f);
			camera->aspect_ratio = ((f32)ctx->framebuffer_width / (f32)ctx->framebuffer_height);
			camera->flags = CAMERA_FLAG_FREE;
			camera->free.speed = v3_zero();
			camera->free.movement_scroll_accumulator = 0;
			camera->free.mouse_smoothed = v2_zero();
			camera->free.rotation_deg = v2_zero();
			camera->free.mouse_last_position = v2_zero();

			// camera->free.focus_entity = player_ett;
			camera->free.focus_entity = v3_zero();
			camera->free.lerp_to_target_position = v3_zero();
			camera->free.lerp_to_target_rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			camera->free.lerp_to_target_distance = 0;
			camera->free.lerp_to_target_alpha = 0;

			camera->free.lerp_to_target_p = false;
			camera->free.lerp_to_target_r = false;

			camera->third_person.target_distance = 5;
			camera->third_person.target = v3_zero();
			camera->third_person.rotation_deg = v2_zero();
			camera->third_person.mouse_smoothed = v2_zero();
		}

		renderer_shader_add(str8_from("default_program3D"), str8_from("shaders/default3D.vertex"),
		    str8_from("shaders/default3D.fragment"));
		renderer_shader_add(
		    str8_from("bayer"), str8_from("shaders/bayer.vertex"), str8_from("shaders/bayer.fragment"));
		renderer_shader_add(str8_from("skinned3D"), str8_from("shaders/skinned.vertex"),
		    str8_from("shaders/default3D.fragment"));

		renderer_texture_add(str8_from("bayer16tile2"));
		renderer_texture_add(str8_from("bayer8tile4"));
		renderer_texture_add(str8_from("witch-finger"));
		renderer_texture_add(str8_from("toshibasat8x8"));
		renderer_texture_add(str8_from("spark_flame"));
		renderer_texture_add(str8_from("Woman"));

		audio_set_master_volume(1.0f);
		audio_add_music(str8_from("bg_music1"), str8_from("exported/ghost-love.wav"));
		audio_play(str8_from("bg_music1"));
		audio_add_sound(str8_from("step1"), str8_from("exported/foottapping_01.wav"));
		audio_add_sound(str8_from("step2"), str8_from("exported/foottapping_02.wav"));
		audio_add_sound(str8_from("step3"), str8_from("exported/foottapping_03.wav"));

		stage_system_register(str8_from("Mesh"), scene_mesh_calculate_aabb_update, 0);
		stage_system_register(str8_from("Rigid body"), scene_rigid_body_update, 0);
		stage_system_register(str8_from("Particle emitter"), scene_particle_emitter_update, 0);
		stage_system_register(str8_from("Player"), scene_player_update, 0);
		stage_system_register(str8_from("Camera"), scene_camera_update, 0);
		stage_system_register(str8_from("Hierarchy"), scene_hierarchy_update, 0);
		stage_system_register(str8_from("Transform clear"), scene_transform_clear_dirty, 0);
		stage_system_register(str8_from("Particle emitter sort"), scene_particle_emitter_sort_update, 0);
		stage_system_register(str8_from("Cross fade controller"), scene_cfc_update, 0);
		stage_system_register(str8_from("Fade to"), scene_fade_to_update, 0);
		stage_system_register(str8_from("Palette"), scene_m4_palette_update, 0);

		stage_scene_asset_load(str8_from("Scene"));
		// stage_scene_asset_load(str8_from("praca-scene"));
		stage_scene_asset_load(str8_from("hierarchy"));
		stage_scene_asset_load(str8_from("simple-cube-scene"));

		struct arena *current_scene_arena = stage_scene_get_arena();
		// Particle emitter
		{
			component_t emitter_archetype = TRANSFORM | PARTICLE_EMITTER;
			entity_t emitter = stage_entity_new(emitter_archetype);

			transform_component *partcile_transform = stage_component_get_data(emitter, TRANSFORM);
			transform_init(partcile_transform);

			particle_emitter_component *p_emitter = stage_component_get_data(emitter, PARTICLE_EMITTER);
			p_emitter->emission_rate = 32;
			particle_emitter_init(current_scene_arena, p_emitter, 256);
			struct aabb aabb_emitter = {.min = v3_new(-2, 0, -2), .max = v3_new(2, 0, 2)};
			// particle_emitter_set_shape_box(p_emitter, aabb_emitter);
			trs cube = ((union trs){.translation = v4_zero(),
			    .rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f),
			    .scale = v3_new(4, 4, 4)});
			particle_emitter_set_shape_cube(p_emitter, cube);

			struct scene_iter iter = stage_iter_begin(TRANSFORM | MESH);
			while (stage_iter_next(&iter))
			{
				mesh_component *mesh = stage_iter_get_component(&iter, MESH);

				if (str8_eq(mesh->resource_ref->name, str8_from("child-cone-mesh")))
				{
					// transform_set_parent(current_scene_arena, partcile_transform, transform);

					entity_t entity = scene_iter_get_entity(&iter);
					struct scene *scene = stage_get_current_scene();
					scene_entity_set_parent(scene, emitter, entity);
				}
			}
		}

		player_ett = stage_scene_asset_load(str8_from("woman"));
		if (player_ett.handle != INVALID_HANDLE)
		{
			stage_entity_add_component(player_ett, RIGID_BODY | PLAYER);
			// player_ett = stage_animated_asset_load(str8_from("exported/Woman.gltf"));

			transform_component *transform = stage_component_get_data(player_ett, TRANSFORM);
			rigid_body_component *rigid_body = stage_component_get_data(player_ett, RIGID_BODY);
			material_component *material = stage_component_get_data(player_ett, MATERIAL);
			material->material_ref->image = str8_from("Woman");
			mesh_component *mesh = stage_component_get_data(player_ett, MESH);
			mesh->mesh_ref->flags |= MESH_FLAG_DRAW_AABB;

			transform->transform_local.translation = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
			transform->transform_local.scale = v3_new(0.0042f, 0.0042f, 0.0042f);
			transform->transform_local.rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);

			rigid_body->velocity = v3_zero();
			rigid_body->gravity = v3_zero();
			rigid_body->collision_shape = RB_SHAPE_CAPSULE;
			rigid_body->has_gravity = true;

			v3 base = transform->transform_local.translation.v3;
			v3 tip = transform->transform_local.translation.v3;
			tip.y += 2.1f;

			rigid_body->capsule = (struct capsule){.base = base, .tip = tip, 0.4f};
		}
	}

	// ================================================================
	// ================================================================
	// ================================================================

	// ================================================================
	// ================================================================
	// ================================================================

	stage_scene_new(str8_from("road"));
	{
		entity_t camera_ett = stage_entity_new(CAMERA | TRANSFORM);
		stage_set_gravity_force(v3_new(0.0f, -9.8f, 0.0f));
		stage_set_main_camera(camera_ett);

		camera_component *camera = stage_component_get_data(camera_ett, CAMERA);
		transform_component *camera_transform = stage_component_get_data(camera_ett, TRANSFORM);
		{
			camera_transform->matrix_local = m4_identity();
			camera_transform->transform_local = trs_identity();
			camera_transform->transform_local.translation.v3 = v3_new(0.0f, 5.0f, 5.0f);

			camera_transform->matrix = m4_identity();
			camera_transform->last_matrix = m4_identity();
		}
		{
			camera->z_near = 0.1f;
			camera->z_far = 100.0f;
			camera->fovx = glm_rad(75.0f);
			camera->aspect_ratio = ((f32)ctx->framebuffer_width / (f32)ctx->framebuffer_height);
			camera->flags = CAMERA_FLAG_FREE;
			camera->free.speed = v3_zero();
			camera->free.movement_scroll_accumulator = 0;
			camera->free.mouse_smoothed = v2_zero();
			camera->free.rotation_deg = v2_zero();
			camera->free.mouse_last_position = v2_zero();

			camera->free.focus_entity = v3_zero();
			camera->free.lerp_to_target_position = v3_zero();
			camera->free.lerp_to_target_rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			camera->free.lerp_to_target_distance = 0;
			camera->free.lerp_to_target_alpha = 0;

			camera->free.lerp_to_target_p = false;
			camera->free.lerp_to_target_r = false;

			camera->third_person.target_distance = 5;
			camera->third_person.target = v3_zero();
			camera->third_person.rotation_deg = v2_zero();
			camera->third_person.mouse_smoothed = v2_zero();
		}

		stage_system_register(str8_from("Mesh"), scene_mesh_calculate_aabb_update, 0);
		stage_system_register(str8_from("Rigid body"), scene_rigid_body_update, 0);
		stage_system_register(str8_from("Player"), scene_player_update_viniL, 0);
		stage_system_register(str8_from("Camera"), scene_camera_update, 0);
		stage_system_register(str8_from("Hierarchy"), scene_hierarchy_update, 0);
		stage_system_register(str8_from("Transform clear"), scene_transform_clear_dirty, 0);
		stage_system_register(str8_from("Cross fade controller"), scene_cfc_update, 0);
		stage_system_register(str8_from("Fade to"), scene_fade_to_update, 0);
		stage_system_register(str8_from("Palette"), scene_m4_palette_update, 0);

		stage_scene_asset_load(str8_from("Triangulo"));
		{
			entity_t viniL = stage_scene_asset_load(str8_from("viniL"));
			if (viniL.handle != INVALID_HANDLE)
			{
				struct resource *resource_clip = resource_get_by_name(str8_from("idle"));
				sm__assert(resource_clip);
				clip_component *clip = stage_component_get_data(viniL, CLIP);
				clip->next_clip_ref = resource_clip->data;

				transform_component *transform = stage_component_get_data(viniL, TRANSFORM);
				transform->transform_local.translation = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
				transform->transform_local.scale = v3_one();
				transform->transform_local.rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);

				rigid_body_component *rigid_body = stage_component_get_data(viniL, RIGID_BODY);
				rigid_body->velocity = v3_zero();
				rigid_body->gravity = v3_zero();
				rigid_body->collision_shape = RB_SHAPE_CAPSULE;
				rigid_body->has_gravity = true;

				v3 base = transform->transform_local.translation.v3;
				v3 tip = transform->transform_local.translation.v3;
				tip.y += 2.1f;

				rigid_body->capsule = (struct capsule){.base = base, .tip = tip, 0.4f};
			}
		}
		{
			entity_t spider = stage_scene_asset_load(str8_from("spider"));
			if (spider.handle != INVALID_HANDLE)
			{
				struct resource *spider_walk = resource_get_by_name(str8_from("spider-walk"));
				sm__assert(spider_walk);
				clip_component *spider_clip = stage_component_get_data(spider, CLIP);
				spider_clip->next_clip_ref = spider_walk->data;
			}
		}
	}
	stage_set_current_by_name(str8_from("main scene"));
}

void
on_update(sm__maybe_unused struct ctx *ctx)
{
	if (core_key_pressed(KEY_J)) { stage_set_current_by_name(str8_from("main scene")); }
	else if (core_key_pressed(KEY_K)) { stage_set_current_by_name(str8_from("road")); }
	camera_component *camera = stage_get_main_camera();

	if (core_key_pressed(KEY_N)) { shake_start(&shake); }

	struct scene_iter iter = stage_iter_begin(TRANSFORM | MESH);
	while (stage_iter_next(&iter))
	{
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		transform_component *transform = stage_iter_get_component(&iter, TRANSFORM);

		// scene_entity_update_hierarchy(transform);
		if (str8_eq(mesh->resource_ref->name, str8_from("child-cone-mesh")))
		{
			entity_t entity = scene_iter_get_entity(&iter);
			struct scene *scene = stage_get_current_scene();
			scene_entity_update_hierarchy(scene, entity);
			v4 q;
			// static f32 angle = 0.0f;
			// if ((angle += (30 * ctx->dt)) > 180.0f) angle = 0.0f;
			// glm_quat(q.data, glm_rad(angle), 0.0f, 1.0f, 0.0f);
			glm_quat(q.data, glm_rad(90.0 * ctx->dt), 1.0f, 0.0f, 0.0f);
			// q = quat_from_euler_angles(0.0f, glm_rad(90.0 * ctx->dt), 0.0f);

			// v4 rot = transform_get_rotation(transform);
			// glm_quat_lerp(rot.data, q.data, 0.5f, rot.data);
			// transform_set_rotation(transform, rot);

			f32 x = (sinf(ctx->time)) * 4.0f;
			// transform_translate(transform, v3_new(y * ctx->dt, 0.0f, 0.0f));
			// transform_rotate(transform, q);
			scene_entity_translate(scene, entity, v3_new(x * ctx->dt, 0.0f, 0.0f));
			scene_entity_rotate(scene, entity, q);
		}
	}

	if (core_key_pressed_lock(KEY_L, 24))
	{
		if (camera->flags & CAMERA_FLAG_FREE)
		{
			camera->flags &= ~CAMERA_FLAG_FREE;
			camera->flags |= CAMERA_FLAG_THIRD_PERSON;
		}
		else if (camera->flags & CAMERA_FLAG_THIRD_PERSON)
		{
			camera->flags &= ~CAMERA_FLAG_THIRD_PERSON;
			camera->flags |= CAMERA_FLAG_FREE;
		}
	}
}

struct blend
{
	u32 index;

	struct blend_sort
	{
		float dist;
		transform_component *transform;
		mesh_component *mesh;
		material_component *material;

	} sort[256];
};

enum cube_face
{
	Left,
	Bottom,
	Back,
	Right,
	Top,
	Front
};

enum cube_face
get_face_toward(trs cube, v3 observer_position)
{
	// https://gamedev.stackexchange.com/a/187077
	trs inv_cube = trs_inverse(cube);
	v3 to_observer = trs_point(inv_cube, observer_position);

	v3 absolute;
	glm_vec3_abs(to_observer.data, absolute.data);

	if (absolute.x >= absolute.y)
	{
		if (absolute.x >= absolute.z) { return to_observer.x > 0 ? Right : Left; }

		else { return to_observer.z > 0 ? Front : Back; }
	}
	else if (absolute.y >= absolute.z) { return to_observer.y > 0 ? Top : Bottom; }
	else { return to_observer.z > 0 ? Front : Back; }
}

i32
compare(const void *a, const void *b)
{
	const struct blend_sort *as = a;
	const struct blend_sort *bs = b;
	return (as->dist < bs->dist) - (as->dist > bs->dist);
}

static void
sm__debug_draw_rigid_body()
{
	u32 color = 0x000000FF;
	struct scene_iter iter = stage_iter_begin(TRANSFORM | MESH);
	while (stage_iter_next(&iter))
	{
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		if (mesh->mesh_ref->flags & MESH_FLAG_DRAW_AABB)
		{
			struct aabb mesh_aabb;
			glm_aabb_transform(mesh->mesh_ref->aabb.data, transform->matrix.data, mesh_aabb.data);
			draw_aabb(mesh_aabb, color_from_hex(color));
			color += (color >> 8) | 0x222222FF;
		}
	}

	iter = stage_iter_begin(TRANSFORM | RIGID_BODY);
	while (stage_iter_next(&iter))
	{
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		rigid_body_component *rb = scene_iter_get_component(&iter, RIGID_BODY);
		switch (rb->collision_shape)
		{
		case RB_SHAPE_CAPSULE:
			{
				v3 base = rb->capsule.base;
				base.y += rb->capsule.radius;
				v3 tip = rb->capsule.tip;
				tip.y -= rb->capsule.radius;

				draw_capsule_wires(base, tip, rb->capsule.radius, 7, 5, color_from_hex(color));
				color += (color >> 8) | 0x222222FF;
			}
			break;
		case RB_SHAPE_SPHERE:
			{
				trs t = trs_identity();
				t.translation.v3 = rb->sphere.center;
				t.scale = v3_fill(rb->sphere.radius);

				// draw_sphere(t, 5, 7, color_from_hex(color));
				color += (color >> 8) | 0x222222FF;
			}
			break;
		default: sm__unreachable();
		}
	}
}

void
sm__debug_draw_3d(void)
{
	static b8 debug_draw = false;
	if (core_key_pressed_lock(KEY_F2, 4)) { debug_draw = !debug_draw; }
	if (!debug_draw) { return; }

	static enum debug_draw_3d {
		DB_STATIC_BODY_DRAW,
		DB_MAX,
	} current = 0;

	if (core_key_pressed_lock(KEY_UP, 4))
	{
		current++;
		current = current % DB_MAX;
	}
	if (core_key_pressed_lock(KEY_DOWN, 4))
	{
		current--;
		current = current % DB_MAX;
	}

	switch (current)
	{
	case DB_STATIC_BODY_DRAW: sm__debug_draw_rigid_body(); break;
	default: break;
	}
}

#if 1
static void
sm__debug_draw_camera(sm__maybe_unused struct ctx *ctx)
{
	camera_component *camera = stage_get_main_camera();
	f32 pitch;
	f32 yaw;
	if (camera->flags & CAMERA_FLAG_THIRD_PERSON)
	{
		pitch = camera->third_person.rotation_deg.x;
		yaw = camera->third_person.rotation_deg.y;
	}
	else
	{
		pitch = camera->free.rotation_deg.x;
		yaw = camera->free.rotation_deg.y;
	}

	i8 buf[64] = {0};
	f32 x_ctx = 10;
	f32 y_ctx = 10;
	if (camera->flags & CAMERA_FLAG_THIRD_PERSON)
	{
		snprintf(buf, 64, "TARGET:  %.2f, %.2f, %.2f", camera->third_person.target.x,
		    camera->third_person.target.y, camera->third_person.target.z);
		str8 distance = str8_from_cstr_stack(buf);
		y_ctx += (16.0f / 2.0f);
		draw_text(distance, v2_new(x_ctx, y_ctx), 16, cWHITE);
		y_ctx += (16.0f / 2.0f) + 10;
	}
	if (camera->flags & CAMERA_FLAG_THIRD_PERSON)
	{
		snprintf(buf, 64, "DIST:    %.2f", camera->third_person.target_distance);
		str8 distance = str8_from_cstr_stack(buf);
		y_ctx += (16.0f / 2.0f);
		draw_text(distance, v2_new(x_ctx, y_ctx), 16, cWHITE);
		y_ctx += (16.0f / 2.0f) + 10;
	}
	{
		x_ctx += 15;
		y_ctx += 30;
		draw_circle(v2_new(x_ctx, y_ctx), 20, 0, pitch, 17, cRED);
		snprintf(buf, 64, "PITCH: %.2f", pitch);
		str8 ptext = str8_from_cstr_stack(buf);
		draw_text(ptext, v2_new(x_ctx + 20 + 10, y_ctx), 16, cWHITE);
		y_ctx += 30;
	}
	{
		y_ctx += 30;
		draw_circle(v2_new(x_ctx, y_ctx), 20, 0, yaw, 17, cGREEN);
		snprintf(buf, 64, "YAW:   %.2f", yaw);
		str8 ptext = str8_from_cstr_stack(buf);
		draw_text(ptext, v2_new(x_ctx + 20 + 10, y_ctx), 16, cWHITE);
		y_ctx += 30;
	}
}
#endif

static void
sm__debug_draw_fps(sm__maybe_unused struct ctx *ctx)
{
	f32 scale = ctx->framebuffer_width / (f32)ctx->win_width;
	i8 buf[64];
	u32 fps = core_get_fps();
	snprintf(buf, 64, "FPS: %d", fps);
	str8 text = str8_from_cstr_stack(buf);
	color fps_color = cWHITE;
	if (fps < 30) { fps_color = cYELLOW; }
	else if (fps < 20) { fps_color = cRED; }
	draw_text(text, v2_new(10 * scale, (ctx->win_height - 10 - 16) * scale), 16 * scale, fps_color);
}

void
sm__debug_draw_2d(sm__maybe_unused struct ctx *ctx)
{
	static b8 debug_draw = false;
	if (core_key_pressed_lock(KEY_F1, 4)) { debug_draw = !debug_draw; }
	if (!debug_draw) { return; }

	static enum debug_draw_2d {
		DB_CAMERA_DRAW,
		// DB_FOG_DRAW,
		DB_MAX,
	} current = 0;

	if (core_key_pressed_lock(KEY_LEFT, 4))
	{
		current += 1;
		current = current % DB_MAX;
	}
	if (core_key_pressed_lock(KEY_RIGHT, 4))
	{
		current -= 1;
		current = current % DB_MAX;
	}

	switch (current)
	{
	case DB_CAMERA_DRAW: sm__debug_draw_camera(ctx); break;
	default: break;
	}

	sm__debug_draw_fps(ctx);
}

void
on_draw(sm__maybe_unused struct ctx *ctx)
{
	renderer_clear_color();
	renderer_clear_color_buffer();
	renderer_clear_depth_buffer();

	// if (!stage_is_current_scene(str8_from("main scene"))) { return; }

	entity_t main_camera_ett = stage_get_main_camera_entity();
	camera_component *camera = stage_component_get_data(main_camera_ett, CAMERA);
	transform_component *camera_transform = stage_component_get_data(main_camera_ett, TRANSFORM);

	static struct blend blend;

	m4 view = camera->view;
	m4 view_projection_matrix = camera->view_projection;

	struct scene_iter iter = stage_iter_begin(TRANSFORM | MESH | MATERIAL);
	while (stage_iter_next(&iter))
	{
		struct scene *scene = stage_get_current_scene();
		entity_t entity = scene_iter_get_entity(&iter);
		if (scene_entity_has_components(scene, entity, ARMATURE)) { continue; }

		transform_component *transform = stage_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		material_component *material = stage_iter_get_component(&iter, MATERIAL);

		renderer_state_clear();

		renderer_shader_set(str8_from("bayer"));
		renderer_shader_set_uniform(str8_from("u_pv"), &view_projection_matrix, sizeof(m4), 1);
		renderer_shader_set_uniform(str8_from("u_model"), &transform->matrix, sizeof(m4), 1);

		struct material_resource *mat = material->material_ref;
		renderer_shader_set_uniform(str8_from("u_diffuse_color"), &color_to_v4(mat->color), sizeof(v4), 1);
		renderer_texture_set(mat->image, str8_from("u_tex0"));
		renderer_texture_set(str8_from("bayer16tile2"), str8_from("u_bayer"));

		renderer_depth_set(&(struct depth_state){.enable = STATE_TRUE, .depth_func = DEPTH_FUNC_LEQUAL});
		renderer_blend_set(&(struct blend_state){.enable = STATE_TRUE, .mode = BLEND_MODE_ALPHA});
		renderer_rasterizer_set(&(struct rasterizer_state){
		    .cull_enable = mesh->mesh_ref->flags & MESH_FLAG_DOUBLE_SIDED ? STATE_FALSE : STATE_TRUE,
		    .cull_mode = CULL_MODE_BACK,
		});
		renderer_state_commit();

		draw_mesh2(mesh);
	}

	iter = stage_iter_begin(TRANSFORM | MESH | MATERIAL | ARMATURE);
	while (stage_iter_next(&iter))
	{
		transform_component *transform = stage_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		material_component *material = stage_iter_get_component(&iter, MATERIAL);

		renderer_state_clear();

		renderer_shader_set(str8_from("skinned3D"));

		renderer_shader_set_uniform(str8_from("u_pv"), &view_projection_matrix, sizeof(m4), 1);
		renderer_shader_set_uniform(str8_from("u_model"), &transform->matrix, sizeof(m4), 1);

		u32 palette_len = array_len(mesh->mesh_ref->skin_data.pose_palette);
		m4 *palette_ptr = mesh->mesh_ref->skin_data.pose_palette;
		renderer_shader_set_uniform(str8_from("u_animated"), palette_ptr, sizeof(m4), palette_len);

		struct material_resource *mat = (material->resource_ref) ? material->resource_ref->material_data : 0;
		if (mat)
		{
			renderer_shader_set_uniform(
			    str8_from("u_diffuse_color"), &color_to_v4(material->material_ref->color), sizeof(v4), 1);
			renderer_texture_set(material->material_ref->image, str8_from("u_tex0"));
		}
		else
		{
			renderer_shader_set_uniform(str8_from("u_diffuse_color"), &v4_one(), sizeof(v4), 1);
			renderer_texture_set(str8_from(""), str8_from("u_tex0"));
		}
		renderer_depth_set(&(struct depth_state){.enable = STATE_TRUE, .depth_func = DEPTH_FUNC_LEQUAL});
		renderer_blend_set(&(struct blend_state){.enable = STATE_FALSE});
		renderer_rasterizer_set(&(struct rasterizer_state){
		    .cull_enable = STATE_TRUE,
		    .cull_mode = CULL_MODE_BACK,
		});

		renderer_state_commit();

		draw_mesh2(mesh);
	}

	trs trs_cube;
	renderer_batch3D_begin(camera);
	{
		renderer_batch_sampler_set(str8_from("spark_flame"));

		static enum blend_mode bl = 0;
		if (core_key_pressed_lock(KEY_P, 5)) { bl = (bl + 1) % SM__BLEND_MODE_MAX; }
		renderer_blend_set(&(struct blend_state){
		    .enable = STATE_TRUE,
		    .mode = bl,
		});
		renderer_depth_set(&(struct depth_state){
		    .enable = STATE_TRUE,
		    .depth_func = DEPTH_FUNC_LEQUAL,
		});
		renderer_rasterizer_set(&(struct rasterizer_state){
		    .cull_enable = STATE_TRUE,
		    .cull_mode = CULL_MODE_BACK,
		});

		iter = stage_iter_begin(TRANSFORM | PARTICLE_EMITTER);
		while (stage_iter_next(&iter))
		{
			transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
			particle_emitter_component *pe = scene_iter_get_component(&iter, PARTICLE_EMITTER);

			trs_cube = pe->cube;
			trs t = trs_from_m4(transform->matrix);
			trs_cube = trs_combine(t, trs_cube);

#if defined(PARTICLE_USE_POOL)

			for (u32 i = 0; i < pe->pool_size; ++i)
			{
				struct particle *p = &pe->particles_pool[i];
				if (p->energy_remaing <= 0.0f) { continue; }
				f32 frac = glm_clamp_zo((p->energy_remaing / p->energy));
				color c = color_lerp(p->color_begin, p->color_end, 1 - frac);

				draw_billboard(view, p->position, v2_one(), c);
			}

#else
			for (struct particle *p = pe->active_sentinel.next; p != &pe->active_sentinel; p = p->next)
			{
				f32 frac = glm_clamp_zo((p->energy_remaing / p->energy));
				color c = color_lerp(p->color_begin, p->color_end, 1 - frac);

				draw_billboard(view, p->position, v2_one(), c);
			}
#endif
		}
	}

	renderer_batch3D_end();

	str8 name = {0};
	renderer_batch3D_begin(camera);
	{
		renderer_blend_set(&(struct blend_state){.enable = STATE_FALSE});
		renderer_depth_set(&(struct depth_state){.enable = STATE_TRUE, .depth_func = DEPTH_FUNC_LEQUAL});
		renderer_rasterizer_set(&(struct rasterizer_state){
		    .cull_enable = STATE_TRUE,
		    .cull_mode = CULL_MODE_BACK,
		});

		draw_line_3D(v3_zero(), v3_right(), color_from_v3(v3_right()));
		draw_line_3D(v3_zero(), v3_forward(), color_from_v3(v3_forward()));
		draw_line_3D(v3_zero(), v3_up(), color_from_v3(v3_up()));

		draw_cube_wires(trs_cube, cRED);

		if (stage_is_current_scene(str8_from("main scene")))
		{
			transform_component *player_transform = stage_component_get_data(player_ett, TRANSFORM);
			trs t = player_transform->transform_local;

			struct ray ray;
			ray.position = t.translation.v3;
			ray.position.y += 2.0f;
			ray.direction = trs_get_forward(t);

			struct intersect_result best_result = {0};
			iter = stage_iter_begin(TRANSFORM | STATIC_BODY | MESH);
			while (stage_iter_next(&iter))
			{
				transform_component *world = scene_iter_get_component(&iter, TRANSFORM);
				mesh_component *mesh = scene_iter_get_component(&iter, MESH);

				struct intersect_result result = collision_ray_mesh(ray, mesh, world);
				if (result.valid)
				{
					if (!best_result.valid || result.depth < best_result.depth)
					{
						best_result = result;
					}
				}
			}

			if (best_result.valid)
			{
				trs sphere = trs_identity();
				sphere.translation.v3 = best_result.position;
				sphere.scale = v3_fill(.1f);
				// draw_sphere(sphere, 5, 7, cRED);

				v3 end;
				glm_vec3_add(best_result.position.data, best_result.normal.data, end.data);
				// draw_line_3D(best_result.position, end, cGREEN);

				// draw_text_billboard_3d(camera->position, name, best_result.position, scale,
				// cWHITE);
			}

			// draw_ray(ray, cGREY);
		}

		if (camera->flags & CAMERA_FLAG_FREE && !camera->free.is_controlled_by_keyboard_mouse)
		{
			v3 dir = camera_screen_to_world(camera, core_get_window_cursor_position(),
			    v4_new(0, 0, core_get_window_width(), core_get_window_height()));

			{
				v2 cursor_position = core_get_window_cursor_position();
				v4 viewport = v4_new(0, 0, core_get_window_width(), core_get_window_height());
				v3 position = v3_new(cursor_position.x, viewport.height - (cursor_position.y), 1.0f);

				m4 mvp;
				glm_mat4_mul(
				    camera->view_projection.data, camera_transform->matrix_local.data, mvp.data);
				glm_unproject(position.data, mvp.data, viewport.data, dir.data);
				glm_vec3_normalize(dir.data);
				v4 q;
				glm_mat4_quat(camera_transform->matrix.data, q.data);
				glm_quat_rotatev(q.data, dir.data, dir.data);
				glm_vec3_normalize(dir.data);
			}

			struct ray ray = {.position = camera_transform->matrix.v3.position, .direction = dir};

#if 0
			struct intersect_result best_result = {0};
			iter = stage_iter_begin(TRANSFORM | STATIC_BODY | MESH);
			while (stage_iter_next(&iter))
			{
				transform_component *world = scene_iter_get_component(&iter, TRANSFORM);
				mesh_component *mesh = scene_iter_get_component(&iter, MESH);

				struct intersect_result result = collision_ray_mesh(ray, mesh, world);
				if (result.valid)
				{
					if (!best_result.valid || result.depth < best_result.depth)
					{
						best_result = result;
						name = mesh->resource_ref->name;
						camera->free.focus_entity = world->self;
					}
				}
			}

			if (best_result.valid)
			{
				trs sphere = trs_identity();
				sphere.translation.v3 = best_result.position;
				sphere.scale = v3_fill(.2f);
				// draw_sphere(sphere, 5, 7, cRED);

				v3 end;
				glm_vec3_add(best_result.position.data, best_result.normal.data, end.data);
				// draw_line_3D(best_result.position, end, cGREEN);
			}

			// draw_ray(ray, cPINK);
#endif
		}
		sm__debug_draw_3d();
	}
	renderer_batch3D_end();
#if 1

	renderer_batch_begin();
	{
		renderer_batch_sampler_set(str8_from("toshibasat8x8"));
		renderer_blend_set(&(struct blend_state){.enable = STATE_FALSE});
		renderer_depth_set(&(struct depth_state){.enable = STATE_FALSE});
		renderer_rasterizer_set(&(struct rasterizer_state){
		    .cull_enable = STATE_TRUE,
		    .cull_mode = CULL_MODE_BACK,
		    .line_width = 1.0f,
		});

		v2 position = v2_new(256.0f / 2, (256.f / 2.0f) / 2.0f);
		v2 size = v2_new(256, 256.f / 2.0f);
		glm_vec2_scale(position.data, .5f, position.data);
		glm_vec2_scale(size.data, .5f, size.data);
		// draw_rectangle(position, size, 0.0f, cWHITE);
		// draw_circle(position, 10, 0.0f, 360.0f, 36, cGREEN);
		// DrawCircleSector(v2_fill(100), 20, 0, 360, 36, cBLUE);
		sm__debug_draw_2d(ctx);

		if (name.size > 0)
		{
			f32 scale = ctx->framebuffer_width / (f32)ctx->win_width;
			draw_text(name, v2_new(10 * scale, 16 * scale), 16, cWHITE);
		}
	}
	renderer_batch_end();
#endif

	return;

#if 0
	// camera_component *camera = stage_component_get_data(cam2_ett, CAMERA);
	draw_begin_3d(camera);

	iter = stage_iter_begin(TRANSFORM | MESH | MATERIAL);
	while (stage_iter_next(&iter))
	{
		transform_component *transform = stage_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		material_component *material = stage_iter_get_component(&iter, MATERIAL);

		if (mesh->mesh_ref->flags & MESH_FLAG_BLEND)
		{
			blend.sort[blend.index].transform = transform;
			blend.sort[blend.index].mesh = mesh;
			blend.sort[blend.index].material = material;

			// transform_component transform = transform_from_m4(world->matrix);

			blend.sort[blend.index].dist =
			    glm_vec3_distance(transform->matrix.v3.position.data, camera->position.data);

			blend.index++;
		}

		else { draw_mesh(camera, mesh, material, transform->matrix); }
	}

	qsort(blend.sort, blend.index, sizeof(struct blend_sort), compare);

	if (blend.index > 0)
	{
		renderer_begin_blend_mode(BLEND_MODE_ALPHA);
		for (u32 i = 0; i < blend.index; ++i)
		{
			draw_mesh(camera, blend.sort[i].mesh, blend.sort[i].material, blend.sort[i].transform->matrix);
		}
		blend.index = 0;
		renderer_end_blend_mode();
	}

	{
		m4 view = camera_get_view(camera);

		struct resource *resource = resource_get_by_name(str8_from("witch-finger"));
		sm__assert(resource);
		sm__assert(resource->type == RESOURCE_IMAGE);

		draw_billboard(view, resource->image_data, v3_new(0.0f, 5.0f, 0.0f),
		    v2_new(resource->image_data->width * 0.02f, resource->image_data->height * 0.02f), cWHITE);
	}

	static f32 scale = 1.0f;
	scale += (core_key_pressed(KEY_L) - core_key_pressed(KEY_H)) * 0.005f;

	draw_text_billboard_3d(camera, str8_from("hello"), v3_new(0.0f, 2.0f, 0.0), scale, cWHITE);

#	if 0
	trs c_t = trs_identity();
	c_t.T.y = 2;
	m4 camera_view = camera_get_view(camera);
	glm_quat_forp(c_t.position.data, camera->position.data, v3_up().data, c_t.rotation.data);
	{
		enum cube_face cb = get_face_toward(c_t, camera->position);
		if (cb == Left) { printf("LEFT\n"); }
		else if (cb == Right) { printf("RIGHT\n"); }
		else if (cb == Bottom) { printf("BOTTOM\n"); }
		else if (cb == Top) { printf("TOP\n"); }
		else if (cb == Back) { printf("BACK\n"); }
		else if (cb == Front) { printf("FRONT\n"); }
	}

	draw_cube_wires(c_t, WHITE);
	draw_cube(c_t, COLOR8);
#	endif

	sm__debug_draw_3d();

	draw_end_3d();

	draw_begin_3d(camera);
	renderer_set_line_width(5.0f);
	renderer_enable_smooth_lines();

	draw_line_3D(v3_zero(), v3_right(), color_from_v3(v3_right()));
	draw_line_3D(v3_zero(), v3_forward(), color_from_v3(v3_forward()));
	draw_line_3D(v3_zero(), v3_up(), color_from_v3(v3_up()));

	draw_end_3d();

#endif
}

void
on_detach(sm__maybe_unused struct ctx *ctx)
{
}
