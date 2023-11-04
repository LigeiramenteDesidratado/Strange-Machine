#include "ecs/smECS.h"
#include "ecs/smScene.h"
#include "math/smCollision.h"

struct intersect_result
rigid_body_intersects(struct scene *scene, rigid_body_component *rb)
{
	struct intersect_result best_result = {0};

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH | STATIC_BODY);

	u32 next = 0;
	while (scene_iter_next(scene, &iter))
	{
		static_body_component *static_body = scene_iter_get_component(&iter, STATIC_BODY);
		if (!static_body->enabled)
		{
			continue;
		}

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		struct sm__resource_mesh *mesh_at = resource_mesh_at(mesh->mesh_handle);

		struct intersect_result result;
		switch (rb->collision_shape)
		{
		case RB_SHAPE_CAPSULE: result = collision_capsule_mesh(rb->capsule, mesh_at, transform); break;
		case RB_SHAPE_SPHERE: result = collision_sphere_mesh(rb->sphere, mesh_at, transform); break;
		default: sm__unreachable();
		}

		if (result.valid)
		{
			if ((!best_result.valid) || (result.depth > best_result.depth))
			{
				best_result = result;
			}
		}
	}

	return (best_result);
}

void
rigid_body_handle_capsule(
    struct scene *scene, struct ctx *ctx, entity_t entity, rigid_body_component *rb, transform_component *transform)
{
	v3 position = transform->transform_local.translation.v3;
	f32 height = glm_vec3_distance(rb->capsule.tip.data, rb->capsule.base.data);
	f32 radius = rb->capsule.radius;
	b8 ground_intersect = 0;

	glm_vec3_add(rb->force.data, v3_new(0.0f, -0.2f, 0.0f).data, rb->force.data);
	glm_vec3_scale(rb->force.data, ctx->dt, rb->velocity.data);

	f32 fixed_update_remain = ctx->dt;
	f32 fixed_dt = ctx->fixed_dt / ctx->dt;

	while (fixed_update_remain > 0)
	{
		fixed_update_remain = fixed_update_remain - ctx->fixed_dt;
		v3 step;
		glm_vec3_scale(rb->velocity.data, fixed_dt, step.data);
		glm_vec3_add(position.data, step.data, position.data);

		v3 tip;
		glm_vec3_add(position.data, v3_new(0.0f, height, 0.0f).data, tip.data);
		rb->capsule = (struct capsule){.base = position, .tip = tip, radius};

		struct intersect_result result = rigid_body_intersects(scene, rb);
		if (!result.valid)
		{
			continue;
		}

		f32 slope = glm_vec3_dot(result.normal.data, v3_up().data);
		f32 slope_threshold = 0.1f;

		if (rb->velocity.y < 0.0f && slope > slope_threshold)
		{
			rb->velocity.y = 0.0f;
			glm_vec3_add(position.data, v3_new(0.0f, result.depth, 0.0f).data, position.data);
			// glm_vec3_add(position.data, result.velocity.data, position.data);

			const f32 GROUND_FRICTION = 0.75;
			const f32 AIR_FRICTION = 0.62;
			glm_vec3_scale(rb->force.data, GROUND_FRICTION, rb->force.data);
			ground_intersect = 1;
		}
		else if (slope <= slope_threshold)
		{
			// Slide on contact surface:
			f32 velocity_len = glm_vec3_norm(rb->velocity.data);

			v3 velocity_normalized;
			glm_vec3_normalize_to(rb->velocity.data, velocity_normalized.data);
			v3 undesired_motion, desired_motion;
			glm_vec3_scale(result.normal.data, glm_vec3_dot(velocity_normalized.data, result.normal.data),
			    undesired_motion.data);
			glm_vec3_sub(velocity_normalized.data, undesired_motion.data, desired_motion.data);
			if (ground_intersect)
			{
				desired_motion.y = 0.0f;
			}
			glm_vec3_scale(desired_motion.data, velocity_len, rb->velocity.data);
			v3 offset;
			glm_vec3_scale(result.normal.data, result.depth, offset.data);
			glm_vec3_add(position.data, offset.data, position.data);
		}
	}

	v3 translate;
	v3 original_position = transform->transform_local.translation.v3;
	glm_vec3_sub(position.data, original_position.data, translate.data);
	scene_entity_translate(scene, entity, translate);

#if 0
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
					ground_intersect = 1;
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
#endif
	// transform_translate(transform, sub);
	// glm_vec3_add(
	//     transform->transform_local.position.v3.data, sub.data, transform->transform_local.position.v3.data);
	// transform_set_dirty(transform, 1);
}

void
rigid_body_handle_sphere(
    struct scene *scene, struct ctx *ctx, entity_t entity, rigid_body_component *rb, transform_component *transform)
{
#if 0 
	v3 position = transform->transform_local.translation.v3;
	f32 radius = rb->sphere.radius;

	b8 ground_intersect = 0;

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
					ground_intersect = 1;
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
	// transform_set_dirty(transform, 1);
#endif
}

b32
common_rigid_body_update(
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

	return (1);
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

b32
common_particle_emitter_update(
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
				if (p->energy_remaing > 0.0f)
				{
					continue;
				}

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

		if (!pe->enable)
		{
			continue;
		}
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

	return (1);
}

b32
common_pe_sort_update(sm__maybe_unused struct arena *arena, sm__maybe_unused struct scene *scene,
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

	return (1);
}

b32
common_mesh_calculate_aabb_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, MESH);
	while (scene_iter_next(scene, &iter))
	{
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		struct sm__resource_mesh *resource_mesh = resource_mesh_at(mesh->mesh_handle);
		if (resource_mesh->flags & MESH_FLAG_DIRTY)
		{
			resource_mesh_calculate_aabb(mesh->mesh_handle);
			resource_mesh->flags &= ~(u32)MESH_FLAG_DIRTY;
		}
	}
	return (1);
}

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
			struct sm__resource_mesh *mesh_resource = resource_mesh_at(mesh->mesh_handle);

			struct aabb aabb;
			glm_aabb_transform(mesh_resource->aabb.data, target_transform->matrix.data, aabb.data);

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
	if (core_key_pressed_lock(KEY_F, 60))
	{
		camera_focus_on_selected_entity(scene, cam, camera_transform, ctx);
	}

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
			cam->free.lerp_to_target_p = 0;
			cam->free.lerp_to_target_r = 0;
			cam->free.lerp_to_target_alpha = 0.0f;
			cam->free.lerp_to_target_position = v3_zero();
		}
	}
}

void
camera_update_input(struct scene *scene, entity_t entity, camera_component *camera, transform_component *transform,
    sm__maybe_unused struct ctx *ctx)
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
				camera->free.is_controlled_by_keyboard_mouse = 1;
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

		if (core_key_pressed(KEY_LEFT_SHIFT))
		{
			glm_vec3_scale(translation.data, 2.0f, translation.data);
		}

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
		if (wheel < 0.0f)
		{
			camera->third_person.target_distance *= 1.2f;
		}
		else if (wheel > 0.0f)
		{
			camera->third_person.target_distance /= 1.2f;
		}
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
		struct scene_iter inner_iter = scene_iter_begin(scene, TRANSFORM | STATIC_BODY | MESH);
		while (scene_iter_next(scene, &inner_iter))
		{
			transform_component *transform = scene_iter_get_component(&inner_iter, TRANSFORM);
			mesh_component *mesh = scene_iter_get_component(&inner_iter, MESH);
			struct sm__resource_mesh *mesh_at = resource_mesh_at(mesh->mesh_handle);

			struct intersect_result result = collision_ray_mesh(ray, mesh_at, transform);
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
			if (best_result.depth <= camera->third_person.target_distance)
			{
				v3 offset;
				glm_vec3_scale(best_result.normal.data, 0.1f, offset.data);
				glm_vec3_add(best_result.position.data, offset.data, offset.data);
				new_position = offset;
			}
		}

		// transform_set_position(transform, new_position);
		scene_entity_set_position(scene, entity, new_position);

		// Update the camera's orientation
		// transform_set_rotation_local(transform, rotation);
		scene_entity_set_rotation_local(scene, entity, rotation);
	}

	camera_lerp_to_entity(scene, entity, camera, transform, ctx);
}

b32
common_camera_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CAMERA | TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		camera_component *cam = scene_iter_get_component(&iter, CAMERA);
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		entity_t camera_ett = scene_iter_get_entity(&iter);

		cam->aspect_ratio = (f32)ctx->win_width / (f32)ctx->win_height;

		camera_update_input(scene, camera_ett, cam, transform, ctx);

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
		// audio_set_listener_position(eye);
		// audio_set_listener_direction(transform->matrix.v3.forward);
	}

	return (1);
}

b32
common_transform_clear_dirty(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		scene_entity_set_dirty(scene, entity, 0);
	}
	return (1);
}

b32
common_cfc_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CROSS_FADE_CONTROLLER | ARMATURE | POSE | CLIP);
	while (scene_iter_next(scene, &iter))
	{
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);

		if (clip->current_clip_handle.id == INVALID_HANDLE)
		{
			continue;
		}
		u32 targets = array_len(cfc->targets);
		for (u32 i = 0; i < targets; ++i)
		{
			f32 duration = cfc->targets[i].duration;

			if (cfc->targets[i].elapsed >= duration)
			{
				clip->current_clip_handle = cfc->targets[i].clip_handle;
				clip->time = cfc->targets[i].time;
				pose_copy(arena, current, cfc->targets[i].pose_ref);

				array_del(cfc->targets, i, 1);
				break;
			}
		}

		targets = array_len(cfc->targets);
		struct sm__resource_armature *armature_resource = resource_armature_at(armature->armature_handle);
		struct pose *rest = &armature_resource->rest;
		// struct pose *rest = &armature->armature_ref->rest;

		pose_copy(arena, current, rest);
		clip->time = resource_clip_sample(clip->current_clip_handle, current, clip->time + ctx->dt);

		for (u32 i = 0; i < targets; ++i)
		{
			struct cross_fade_target *target = cfc->targets + i;
			target->time =
			    resource_clip_sample(target->clip_handle, target->pose_ref, target->time + ctx->dt);
			target->elapsed += ctx->dt;
			f32 t = target->elapsed / target->duration;
			if (t > 1.0f)
			{
				t = 1.0f;
			}
			pose_blend(current, current, target->pose_ref, t, -1);
		}
	}
	return (1);
}

b32
common_fade_to_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CROSS_FADE_CONTROLLER | ARMATURE | POSE | CLIP);

	while (scene_iter_next(scene, &iter))
	{
		cross_fade_controller_component *cfc = scene_iter_get_component(&iter, CROSS_FADE_CONTROLLER);
		pose_component *current = scene_iter_get_component(&iter, POSE);
		clip_component *clip = scene_iter_get_component(&iter, CLIP);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);

		if (clip->next_clip_handle.id == INVALID_HANDLE)
		{
			continue;
		}
		if (clip->current_clip_handle.id == INVALID_HANDLE && clip->next_clip_handle.id == INVALID_HANDLE)
		{
			continue;
		}

		struct sm__resource_armature *armature_resource = resource_armature_at(armature->armature_handle);
		if (clip->current_clip_handle.id == INVALID_HANDLE && clip->next_clip_handle.id != INVALID_HANDLE)
		{
			array_set_len(arena, cfc->targets, 0);
			clip->current_clip_handle = clip->next_clip_handle;

			struct sm__resource_clip *clip_resource = resource_clip_at(clip->current_clip_handle);
			clip->time = clip_resource->start_time;

			struct pose *rest = &armature_resource->rest;
			pose_copy(arena, current, rest);
			continue;
		}

		u32 targets = array_len(cfc->targets);
		if (targets >= 1)
		{
			if (cfc->targets[targets - 1].clip_handle.id == clip->next_clip_handle.id)
			{
				continue;
			}
		}
		else if (clip->current_clip_handle.id == clip->next_clip_handle.id)
		{
			continue;
		}

		struct sm__resource_clip *next_clip_resource = resource_clip_at(clip->next_clip_handle);
		struct cross_fade_target target = {
		    .pose_ref = &armature_resource->rest,
		    .clip_handle = clip->next_clip_handle,
		    .duration = 0.5,
		    .time = next_clip_resource->start_time,
		    .elapsed = 0.0f,
		};

		array_push(arena, cfc->targets, target);
	}
	return (1);
}

b32
common_m4_palette_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, MESH | ARMATURE | POSE);

	while (scene_iter_next(scene, &iter))
	{
		pose_component *current = scene_iter_get_component(&iter, POSE);
		armature_component *armature = scene_iter_get_component(&iter, ARMATURE);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);

		struct sm__resource_mesh *mesh_resource = resource_mesh_at(mesh->mesh_handle);
		struct sm__resource_armature *armature_resource = resource_armature_at(armature->armature_handle);

		sm__assert(mesh_resource->flags & MESH_FLAG_SKINNED);
		struct arena *resource_arena = resource_get_arena();
		pose_get_matrix_palette(current, resource_arena, &mesh_resource->skin_data.pose_palette);

		for (u32 i = 0; i < array_len(mesh_resource->skin_data.pose_palette); ++i)
		{
			m4 *pose_palette = mesh_resource->skin_data.pose_palette + i;
			m4 *inverse_bind = armature_resource->inverse_bind + i;
			m4 *dest = mesh_resource->skin_data.pose_palette + i;

			glm_mat4_mul(pose_palette->data, inverse_bind->data, dest->data);
		}
	}
	return (1);
}

b32
common_hierarchy_update(sm__maybe_unused struct arena *arena, sm__maybe_unused struct scene *scene,
    sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		if (scene_entity_is_dirty(scene, entity))
		{
			scene_entity_update_hierarchy(scene, entity);
		}
	}

	return (1);
}
