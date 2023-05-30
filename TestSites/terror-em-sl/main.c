#include "core/smArray.h"
#include "core/smCore.h"
#include "core/smLog.h"

#include "ecs/smECS.h"

#include "ecs/smStage.h"
#include "math/smCollision.h"
#include "math/smMath.h"
#include "math/smShape.h"

#include "animation/smAnimation.h"
#include "renderer/smRenderer.h"

entity_t player_ett;
entity_t sphere_ett;
entity_t player_ett2;
entity_t sphere_ett2;

intersect_result
rigid_body_intersects(struct scene *scene, rigid_body_component *rb)
{
	intersect_result result = {0};

	aabb rb_aabb;

	switch (rb->collision_shape)
	{
	case RIGID_BODY_COLLISION_SHAPE_SPHEPE: rb_aabb = shape_get_aabb_sphere(rb->sphere); break;
	case RIGID_BODY_COLLISION_SHAPE_CAPSULE: rb_aabb = shape_get_aabb_capsule(rb->capsule); break;
	default: return (result);
	};

	u32 alloca_index = 0;
	const u32 MAX_RESULTS = 128;
	intersect_result *results = alloca(MAX_RESULTS * sizeof(intersect_result));

	struct scene_iter iter = scene_iter_begin(scene, WORLD | MESH | STATIC_BODY);

	while (scene_iter_next(scene, &iter))
	{
		static_body_component *static_body = scene_iter_get_component(&iter, STATIC_BODY);
		if (!static_body->enabled) { continue; }

		world_component *world = scene_iter_get_component(&iter, WORLD);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);

		const mesh_resource *mesh_ref = mesh->mesh_ref;

		aabb mesh_aabb = mesh_ref->aabb;

		glm_mat4_mulv3(world->matrix.data, mesh_aabb.max.data, 1.0f, mesh_aabb.max.data);
		glm_mat4_mulv3(world->matrix.data, mesh_aabb.min.data, 1.0f, mesh_aabb.min.data);

		if (!collision_aabbs(rb_aabb, mesh_aabb)) { continue; }

		// TODO: support for vertex array without indices
		assert(array_len(mesh_ref->indices));
		for (u32 index = 0; index < array_len(mesh_ref->indices); index += 3)
		{
			triangle triangle;
			intersect_result result = {0};

			glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 0]].data, triangle.p0.data);
			glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 1]].data, triangle.p1.data);
			glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 2]].data, triangle.p2.data);

			glm_mat4_mulv3(world->matrix.data, triangle.p0.data, 1.0f, triangle.p0.data);
			glm_mat4_mulv3(world->matrix.data, triangle.p1.data, 1.0f, triangle.p1.data);
			glm_mat4_mulv3(world->matrix.data, triangle.p2.data, 1.0f, triangle.p2.data);

			aabb triangle_aabb = shape_get_aabb_triangle(triangle);
			if (!collision_aabbs(rb_aabb, triangle_aabb)) continue;

			switch (rb->collision_shape)
			{
			case RIGID_BODY_COLLISION_SHAPE_SPHEPE:
				collision_sphere_triangle(rb->sphere, triangle, world, &result);
				break;
			case RIGID_BODY_COLLISION_SHAPE_CAPSULE:
				collision_capsule_triangle(rb->capsule, triangle, world, &result);
				break;
			default: sm__unreachable();
			};

			if (result.valid)
			{
				assert(alloca_index < MAX_RESULTS);
				results[alloca_index++] = result;
			}
		}
	}

	intersect_result best_result = {0};
	if (alloca_index)
	{
		best_result = results[0];
		for (u32 i = 1; i < alloca_index; ++i)
		{
			if (results[i].depth > best_result.depth) { best_result = results[i]; }
		}
	}

	return best_result;
}

void
rigid_body_handle_capsule(
    struct scene *scene, sm__maybe_unused struct ctx *ctx, rigid_body_component *rb, transform_component *transform)
{
	v3 position = transform->position.v3;
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
		rb->capsule = (capsule){.base = position, .tip = tip, radius};

		intersect_result result = rigid_body_intersects(scene, rb);
		if (!result.valid) continue;

		v3 r_norm;
		glm_vec3_copy(result.normal.data, r_norm.data);

		f32 velocityLen = glm_vec3_norm(rb->velocity.data);

		v3 velocity_norm;
		glm_vec3_normalize_to(rb->velocity.data, velocity_norm.data);

		v3 undesired_motion;
		glm_vec3_scale(r_norm.data, glm_vec3_dot(velocity_norm.data, r_norm.data), undesired_motion.data);

		v3 desired_motion;
		glm_vec3_sub(velocity_norm.data, undesired_motion.data, desired_motion.data);

		glm_vec3_scale(desired_motion.data, velocityLen, rb->velocity.data);

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

				v3 tip;
				glm_vec3_add(position.data, v3_new(0.0f, height, 0.0f).data, tip.data);
				rb->capsule = (capsule){.base = position, .tip = tip, radius};

				intersect_result result = rigid_body_intersects(scene, rb);
				if (!result.valid) continue;

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

	const f32 GROUND_FRICTION = 0.55f;
	const f32 AIR_FRICTION = 0.58f;
	if (ground_intersect) { glm_vec3_scale(rb->velocity.data, GROUND_FRICTION, rb->velocity.data); }
	else { glm_vec3_scale(rb->velocity.data, AIR_FRICTION, rb->velocity.data); }

	v3 sub;
	v3 original_position = transform->position.v3;
	glm_vec3_sub(position.data, original_position.data, sub.data);
	glm_vec3_add(transform->position.v3.data, sub.data, transform->position.v3.data);
	transform_set_dirty(transform, true);
}

void
rigid_body_handle_sphere(
    struct scene *scene, sm__maybe_unused struct ctx *ctx, rigid_body_component *rb, transform_component *transform)
{
	v3 position = transform->position.v3;
	f32 radius = rb->sphere.radius;

	b8 ground_intersect = false;

	const u32 ccd_max = 5;
	for (u32 i = 0; i < ccd_max; ++i)
	{
		v3 step;
		glm_vec3_scale(rb->velocity.data, 1.0f / ccd_max * ctx->fixed_dt, step.data);
		glm_vec3_add(position.data, step.data, position.data);

		rb->sphere.center = position;

		intersect_result result = rigid_body_intersects(scene, rb);
		if (!result.valid) continue;

		v3 r_norm;
		glm_vec3_copy(result.normal.data, r_norm.data);

		f32 velocityLen = glm_vec3_norm(rb->velocity.data);

		v3 velocity_norm;
		glm_vec3_normalize_to(rb->velocity.data, velocity_norm.data);

		v3 undesired_motion;
		glm_vec3_scale(r_norm.data, glm_vec3_dot(velocity_norm.data, r_norm.data), undesired_motion.data);

		v3 desired_motion;
		glm_vec3_sub(velocity_norm.data, undesired_motion.data, desired_motion.data);

		glm_vec3_scale(desired_motion.data, velocityLen, rb->velocity.data);

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

				intersect_result result = rigid_body_intersects(scene, rb);
				if (!result.valid) continue;

				// Remove penetration (penetration epsilon added to handle infinitely small penetration)
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

	const f32 GROUND_FRICTION = 1.5f;
	const f32 AIR_FRICTION = 1.48f;
	if (ground_intersect) { glm_vec3_scale(rb->velocity.data, GROUND_FRICTION, rb->velocity.data); }
	else { glm_vec3_scale(rb->velocity.data, AIR_FRICTION, rb->velocity.data); }

	v3 sub;
	v3 original_position = transform->position.v3;
	glm_vec3_sub(position.data, original_position.data, sub.data);
	glm_vec3_add(transform->position.v3.data, sub.data, transform->position.v3.data);
	transform_set_dirty(transform, true);
}

b8
scene_rigid_body_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | RIGID_BODY);
	while (scene_iter_next(scene, &iter))
	{
		rigid_body_component *rb = scene_iter_get_component(&iter, RIGID_BODY);
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);

		switch (rb->collision_shape)
		{
		case RIGID_BODY_COLLISION_SHAPE_CAPSULE: rigid_body_handle_capsule(scene, ctx, rb, transform); break;
		case RIGID_BODY_COLLISION_SHAPE_SPHEPE: rigid_body_handle_sphere(scene, ctx, rb, transform); break;
		default: sm__unreachable();
		};
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

b8
scene_camera_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, CAMERA);
	while (scene_iter_next(scene, &iter))
	{
		camera_component *cam = scene_iter_get_component(&iter, CAMERA);

		if (cam->flags & CAMERA_FLAG_THIRD_PERSON)
		{
			v2 offset = core_get_cursor_rel_pos();
			f32 wheel = core_get_scroll();

			cam->angle.x -= (offset.x * -cam->sensitive * ctx->dt);
			cam->angle.y -= (offset.y * -cam->sensitive * ctx->dt);

			// Angle clamp
			if (cam->angle.y > glm_rad(25.0f)) { cam->angle.y = glm_rad(25.0f); }
			else if (cam->angle.y < glm_rad(-75.0f)) { cam->angle.y = glm_rad(-75.0f); }

			// Camera zoom
			cam->target_distance -= (wheel * 48.0f * ctx->dt);

			// Camera distance clamp
			// if (cam->target_distance < 1.2f) { cam->target_distance = 1.2f; }
			cam->target_distance = MAX(1.2f, cam->target_distance);
			cam->target_distance = MIN(12.0f, cam->target_distance);

			v3 position;
			position.x = sinf(cam->angle.x) * cam->target_distance * cosf(cam->angle.y) + cam->target.x;

			if (cam->angle.y <= 0.0f)
			{
				position.y =
				    sinf(cam->angle.y) * cam->target_distance * sinf(cam->angle.y) + cam->target.y;
			}
			else
			{
				position.y =
				    -sinf(cam->angle.y) * cam->target_distance * sinf(cam->angle.y) + cam->target.y;
			}

			position.z = cosf(cam->angle.x) * cam->target_distance * cosf(cam->angle.y) + cam->target.z;

			glm_vec3_copy(position.data, cam->position.data);
			// glm_vec3_lerp(cam->position.data, position.data, 0.5, cam->position.data);
		}
		else if (cam->flags & CAMERA_FLAG_FREE) {}
		else {}
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
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		transform_set_dirty(transform, false);
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

		assert(mesh->mesh_ref->skin_data.is_skinned);
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
    str8_from("Idle_Armature"),
    str8_from("Jump2_Armature"),
    str8_from("Jump_Armature"),
    str8_from("Lean_Left_Armatur"),
    str8_from("PickUp_Armature"),
    str8_from("Punch_Armature"),
    str8_from("Running_Armature"),
    str8_from("SitIdle_Armature"),
    str8_from("Sitting_Armature"),
    str8_from("Walking_Armature"),

};

b8
scene_player_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | WORLD | HIERARCHY | MESH | MATERIAL | ARMATURE |
							     CLIP | POSE | CROSS_FADE_CONTROLLER | RIGID_BODY | PLAYER);
	while (scene_iter_next(scene, &iter))
	{
		entity_t cam_ett = scene_get_main_camera(scene);
		camera_component *camera = scene_component_get_data(scene, cam_ett, CAMERA); // TODO

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		world_component *world = scene_iter_get_component(&iter, WORLD);
		hierarchy_component *hierarchy = scene_iter_get_component(&iter, HIERARCHY);
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
			m4 view = camera_get_view(camera);
			v3 fwd;
			fwd.data[0] = view.data[2][0];
			fwd.data[1] = 0.0f;
			fwd.data[2] = -view.data[2][2];
			v3 right;

			right.data[0] = -view.data[0][0];
			right.data[1] = 0.0f;
			right.data[2] = view.data[0][2];

			glm_vec3_scale(fwd.data, direction.z, fwd.data);
			glm_vec3_scale(right.data, direction.x, right.data);

			glm_vec3_add(fwd.data, right.data, direction.data);

			glm_vec3_normalize(direction.data);
			f32 yaw = atan2f(direction.x, direction.z);

			v4 rotation;

			glm_quatv(rotation.data, yaw, v3_up().data);
			glm_quat_slerp(
			    transform->rotation.data, rotation.data, 10.0f * ctx->dt, transform->rotation.data);
			transform_set_dirty(transform, true);
			player->state = ANIM_WALKING;
		}
		else { player->state = ANIM_IDLE; }

		f32 sprint = 1.0f;
		if (core_key_pressed(KEY_LEFT_SHIFT))
		{
			sprint = 2.5f;
			player->state = ANIM_RUNNING;
		}
		glm_vec3_scale(direction.data, sprint * 18.0f * ctx->dt, direction.data);

		f32 fov = (core_key_pressed(KEY_K) - core_key_pressed(KEY_J)) * 0.5f;
		camera->fov += fov;
		if (camera->fov < 12.0f) { camera->fov = 12.0f; }
		else if (camera->fov > 80.0f) { camera->fov = 80.0f; }
		glm_vec3_add(rb->velocity.data, direction.data, rb->velocity.data);

		camera->target = transform->position.v3;

		if (glm_vec3_norm(rb->velocity.data) < 0.01f) { player->state = ANIM_IDLE; }

		v3 c;
		glm_vec3_sub(rb->capsule.tip.data, rb->capsule.base.data, c.data);
		f32 height = glm_vec3_norm(c.data);
		camera->target.y += height;

		if (core_key_pressed(KEY_F)) { player->state = ANIM_SITIDLE; }
		struct resource *resource_clip = resource_get_by_name(ctable_animation_names[player->state]);
		assert(resource_clip);
		clip->next_clip_ref = resource_clip->data;
	}

	return (true);
}

b8
scene_hierarchy_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | WORLD | HIERARCHY);
	while (scene_iter_next(scene, &iter))
	{
		transform_component *child_transform = scene_iter_get_component(&iter, TRANSFORM);
		const hierarchy_component *child_hierarchy = scene_iter_get_component(&iter, HIERARCHY);

		entity_t parent = child_hierarchy->parent;
		b8 dirty = transform_is_dirty(child_transform);
		while (parent.handle && !dirty)
		{
			if (!scene_entity_is_valid(scene, parent)) { continue; }
			if (scene_entity_has_components(scene, parent, TRANSFORM | WORLD | HIERARCHY))
			{
				transform_component *parent_transform =
				    scene_component_get_data(scene, parent, TRANSFORM);
				dirty = transform_is_dirty(parent_transform);

				hierarchy_component *parent_hierarchy =
				    scene_component_get_data(scene, parent, HIERARCHY);
				parent = parent_hierarchy->parent;
			}
			else { assert(0); }
		}

		if (!dirty) { continue; }

		parent = child_hierarchy->parent;
		transform_component accumulator = *child_transform;

		while (parent.handle)
		{
			assert(scene_entity_is_valid(scene, parent));
			if (scene_entity_has_components(scene, parent, TRANSFORM | WORLD | HIERARCHY))
			{
				transform_component *parent_transform =
				    scene_component_get_data(scene, parent, TRANSFORM);
				accumulator = transform_combine(*parent_transform, accumulator);

				hierarchy_component *parent_hierarchy =
				    scene_component_get_data(scene, parent, HIERARCHY);
				parent = parent_hierarchy->parent;
			}
			else { assert(0); }
		}

		world_component *child_world = scene_iter_get_component(&iter, WORLD);
		m4 world = transform_to_m4(accumulator);
		world_store_matrix(child_world, world);
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
	struct core_init init_c = {
	    .argc = argc,
	    .argv = argv,
	    .title = str8_from("Terror em SL"),
	    .w = 800.0f,
	    .h = 600.0f,
	    .total_memory = MB(30),
	    .target_fps = 24,
	    .fixed_fps = 24,
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
	sm___resource_mock_read();
	stage_scene_new(str8_from("main scene"));
	{
		entity_t cam_ett = stage_entity_new(CAMERA);
		stage_set_main_camera(cam_ett);
		stage_set_gravity_force(v3_new(0.0f, -9.8f, 0.0f));

		camera_component *camera = stage_component_get_data(cam_ett, CAMERA);
		{
			v3 camera_position = v3_new(1.8f, 5.0f, -5.0f);
			v3 camera_target = v3_zero();

			v3 dist;
			glm_vec3_sub(camera_target.data, camera_position.data, dist.data);
			f32 target_distance = glm_vec3_norm(dist.data);

			v3 angle = {
			    .x = atan2f(dist.x, dist.z),
			    .y = atan2f(dist.y, sqrtf(dist.x * dist.x + dist.z * dist.z)),
			};

			camera->position = camera_position;
			camera->target = camera_target;
			camera->target_distance = target_distance;
			camera->move_speed = 1.0f;
			camera->sensitive = 0.5f;
			camera->aspect_ratio = ((f32)ctx->win_width / ctx->win_height);
			camera->fov = 75.0f;
			camera->angle = angle;
			camera->flags = CAMERA_FLAG_THIRD_PERSON;

			core_hide_cursor();
		}

		stage_system_register(str8_from("Mesh"), scene_mesh_calculate_aabb_update, 0);
		stage_system_register(str8_from("Rigid body"), scene_rigid_body_update, 0);
		stage_system_register(str8_from("Player"), scene_player_update, 0);
		stage_system_register(str8_from("Camera"), scene_camera_update, 0);
		stage_system_register(str8_from("Hierarchy"), scene_hierarchy_update, 0);
		stage_system_register(str8_from("Transform clear"), scene_transform_clear_dirty, 0);
		stage_system_register(str8_from("Cross fade controller"), scene_cfc_update, 0);
		stage_system_register(str8_from("Fade to"), scene_fade_to_update, 0);
		stage_system_register(str8_from("Palette"), scene_m4_palette_update, 0);

		stage_scene_asset_load(str8_from("MainScene"));
		// stage_scene_asset_load(str8_from("exported/woman-agent.gltf"));
		// stage_scene_asset_load(str8_from("exported/city-general-props.gltf"));
		// stage_scene_asset_load(str8_from("exported/triangulo-praca.gltf"));
		// scene_load(scene->arena, scene, str8_from("exported/prefeitura-predio.gltf"));

		sphere_ett = stage_entity_new(TRANSFORM | WORLD | RIGID_BODY);
		{
			transform_component *transform = stage_component_get_data(sphere_ett, TRANSFORM);
			rigid_body_component *rigid_body = stage_component_get_data(sphere_ett, RIGID_BODY);

			transform->position = v4_new(5.0f, 20.0f, 0.0f, 0.0f);
			transform->scale = v3_one();
			transform->rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			transform->flags = TRANSFORM_FLAG_DIRTY;

			rigid_body->velocity = v3_zero();
			rigid_body->gravity = v3_zero();
			rigid_body->collision_shape = RIGID_BODY_COLLISION_SHAPE_SPHEPE;
			rigid_body->has_gravity = true;

			v3 center = transform->position.v3;

			rigid_body->sphere = (sphere){.center = center, .radius = 0.5f};
		}

#if 0
		player_ett = stage_animated_asset_load(str8_from("exported/Woman.gltf"));
		stage_entity_add_component(player_ett, RIGID_BODY | PLAYER);

		transform_component *transform = stage_component_get_data(player_ett, TRANSFORM);
		rigid_body_component *rigid_body = stage_component_get_data(player_ett, RIGID_BODY);
		{
			transform->position = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
			transform->scale = v3_new(0.0042f, 0.0042f, 0.0042f);
			transform->rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			transform->flags = TRANSFORM_FLAG_DIRTY;

			rigid_body->velocity = v3_zero();
			rigid_body->gravity = v3_zero();
			rigid_body->collision_shape = RIGID_BODY_COLLISION_SHAPE_CAPSULE;
			rigid_body->has_gravity = true;

			v3 base = transform->position.v3;
			v3 tip = transform->position.v3;
			tip.y += 2.1f;

			rigid_body->capsule = (capsule){.base = base, .tip = tip, 0.4f};
		}
#endif
	}
	// ================================================================
	// ================================================================
	// ================================================================

	stage_scene_new(str8_from("road"));
	{
		entity_t cam_ett = stage_entity_new(CAMERA);
		stage_set_main_camera(cam_ett);
		stage_set_gravity_force(v3_new(0.0f, -9.8f, 0.0f));

		camera_component *camera = stage_component_get_data(cam_ett, CAMERA);
		{
			v3 camera_position = v3_new(1.8f, 5.0f, -5.0f);
			v3 camera_target = v3_zero();

			v3 dist;
			glm_vec3_sub(camera_target.data, camera_position.data, dist.data);
			f32 target_distance = glm_vec3_norm(dist.data);

			v3 angle = {
			    .x = atan2f(dist.x, dist.z),
			    .y = atan2f(dist.y, sqrtf(dist.x * dist.x + dist.z * dist.z)),
			};

			camera->position = camera_position;
			camera->target = camera_target;
			camera->target_distance = target_distance;
			camera->move_speed = 1.0f;
			camera->sensitive = 0.5f;
			camera->aspect_ratio = ((f32)ctx->win_width / ctx->win_height);
			camera->fov = 75.0f;
			camera->angle = angle;
			camera->flags = CAMERA_FLAG_THIRD_PERSON;

			core_hide_cursor();
		}

		stage_system_register(str8_from("Mesh"), scene_mesh_calculate_aabb_update, 0);
		stage_system_register(str8_from("Rigid body"), scene_rigid_body_update, 0);
		stage_system_register(str8_from("Player"), scene_player_update, 0);
		stage_system_register(str8_from("Camera"), scene_camera_update, 0);
		stage_system_register(str8_from("Hierarchy"), scene_hierarchy_update, 0);
		stage_system_register(str8_from("Transform clear"), scene_transform_clear_dirty, 0);
		stage_system_register(str8_from("Cross fade controller"), scene_cfc_update, 0);
		stage_system_register(str8_from("Fade to"), scene_fade_to_update, 0);
		stage_system_register(str8_from("Palette"), scene_m4_palette_update, 0);

		// stage_scene_asset_load(str8_from("exported/MainScene.gltf"));
		// stage_scene_asset_load(str8_from("exported/womana-gent.gltf"));
		// stage_scene_asset_load(str8_from("exported/city-general-props.gltf"));
		stage_scene_asset_load(str8_from("Scene"));
		// scene_load(scene->arena, scene, str8_from("exported/prefeitura-predio.gltf"));

		sphere_ett2 = stage_entity_new(TRANSFORM | WORLD | RIGID_BODY);
		{
			transform_component *transform = stage_component_get_data(sphere_ett2, TRANSFORM);
			rigid_body_component *rigid_body = stage_component_get_data(sphere_ett2, RIGID_BODY);

			transform->position = v4_new(5.0f, 20.0f, 0.0f, 0.0f);
			transform->scale = v3_one();
			transform->rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			transform->flags = TRANSFORM_FLAG_DIRTY;

			rigid_body->velocity = v3_zero();
			rigid_body->gravity = v3_zero();
			rigid_body->collision_shape = RIGID_BODY_COLLISION_SHAPE_SPHEPE;
			rigid_body->has_gravity = true;

			v3 center = transform->position.v3;

			rigid_body->sphere = (sphere){.center = center, .radius = 0.5f};
		}
#if 0

		player_ett2 = stage_animated_asset_load(str8_from("exported/Woman.gltf"));
		stage_entity_add_component(player_ett2, RIGID_BODY | PLAYER);

		transform_component *transform = stage_component_get_data(player_ett2, TRANSFORM);
		rigid_body_component *rigid_body = stage_component_get_data(player_ett2, RIGID_BODY);
		{
			transform->position = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
			transform->scale = v3_new(0.0042f, 0.0042f, 0.0042f);
			transform->rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);
			transform->flags = TRANSFORM_FLAG_DIRTY;

			rigid_body->velocity = v3_zero();
			rigid_body->gravity = v3_zero();
			rigid_body->collision_shape = RIGID_BODY_COLLISION_SHAPE_CAPSULE;
			rigid_body->has_gravity = true;

			v3 base = transform->position.v3;
			v3 tip = transform->position.v3;
			tip.y += 2.1f;

			rigid_body->capsule = (capsule){.base = base, .tip = tip, 0.4f};
		}
#endif
	}
}

void
on_update(sm__maybe_unused struct ctx *ctx)
{
	if (core_key_pressed(KEY_J)) { stage_set_current_by_name(str8_from("main scene")); }
	else if (core_key_pressed(KEY_K)) { stage_set_current_by_name(str8_from("road")); }
}

void
on_draw(sm__maybe_unused struct ctx *ctx)
{
	camera_component *camera = stage_get_main_camera();

	camera->aspect_ratio = (f32)ctx->win_width / ctx->win_height;

	draw_begin_3d(camera);

	struct scene_iter iter = stage_iter_begin(MESH | MATERIAL | WORLD);
	while (stage_iter_next(&iter))
	{
		world_component *world = stage_iter_get_component(&iter, WORLD);
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		material_component *material = stage_iter_get_component(&iter, MATERIAL);

		draw_mesh(camera, mesh, material, world->matrix);
	}

	// transform_component *transform = scene_component_get_data(scene, player_ett, TRANSFORM);
	// rigid_body_component *rb = stage_component_get_data(player_ett, RIGID_BODY);

	// draw_sphere(transform->position.v3, 1.0f, 8.0f, 12.0f, v4_one());
	// v3 base = rb->capsule.base;
	// base.y += rb->capsule.radius;
	// v3 tip = rb->capsule.tip;
	// tip.y -= rb->capsule.radius;
	//
	// sm_draw_capsule_wires(base, tip, rb->capsule.radius, 7, 5, BLACK);
	//
	// rigid_body_component *sphere_rb = stage_component_get_data(sphere_ett, RIGID_BODY);
	// draw_sphere(sphere_rb->sphere.center, sphere_rb->sphere.radius, 5, 8, WHITE);
	//
	// draw_grid(32, 3);

	draw_end_3d();

	// draw_rectangle(v2_zero(), v2_new(100, 100), 0, v4_new(1.0f, 1.0f, 1.0f, 1.0f));
}

void
on_detach(sm__maybe_unused struct ctx *ctx)
{
}
