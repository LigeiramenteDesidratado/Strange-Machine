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

#include "scene01.h"

#if 0
b8
scene_player_update_viniL(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene_iter iter = scene_iter_begin(
	    scene, TRANSFORM | MESH | MATERIAL | ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER | RIGID_BODY | PLAYER);
	while (scene_iter_next(scene, &iter))
	{
		entity_t player_ett = scene_iter_get_entity(&iter);
		entity_t cam_ett = scene_get_main_camera(scene);
		camera_component *camera = scene_component_get_data(scene, cam_ett, CAMERA);		     // TODO
		transform_component *camera_transform = scene_component_get_data(scene, cam_ett, TRANSFORM); // TODO

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

		v3 input;
		if (camera->flags & CAMERA_FLAG_FREE) { input = v3_zero(); }
		else
		{
			input = v3_new(core_key_pressed(KEY_A) - core_key_pressed(KEY_D), 0,
			    core_key_pressed(KEY_W) - core_key_pressed(KEY_S));
		}

		v3 direction;
		glm_vec3_normalize_to(input.data, direction.data);
		f32 dir_len = glm_vec3_norm(direction.data);
		if (dir_len != 0.0f)
		{
			v4 q;
			glm_mat4_quat(camera_transform->matrix.data, q.data);
			v3 camera_angles = quat_to_euler_angles(q);

			v3 player_angles = quat_to_euler_angles(transform->transform_local.rotation);

			player->target_angle = atan2f(direction.x, direction.z) + camera_angles.y;

			static f32 rotation_angle = 0.0f;
			const f32 rotation_smooth_time = 0.12f;
			f32 rotation = smooth_damp_angle(player_angles.y, player->target_angle, &rotation_angle,
			    rotation_smooth_time, 1000.0f, ctx->dt);

			v4 quat_rot = quat_from_euler_angles(0.0f, rotation, 0.0f);
			scene_entity_set_rotation(scene, player_ett, quat_rot);
			player->anim_state = ANIM_WALKING;

			v3 target_direction;
			quat_rot = quat_from_euler_angles(0.0f, player->target_angle, 0.0f);
			glm_quat_rotatev(quat_rot.data, v3_forward().data, target_direction.data);
			glm_vec3_normalize(target_direction.data);

			f32 sprint = 1.0f;
			if (player->anim_state == ANIM_WALKING && core_key_pressed(KEY_LEFT_SHIFT))
			{
				sprint = 2.5f;
				player->anim_state = ANIM_RUNNING;
			}

			glm_vec3_scale(target_direction.data, sprint * player->speed * ctx->dt, target_direction.data);
			glm_vec3_add(rb->force.data, target_direction.data, rb->force.data);

			if (glm_vec3_norm(rb->force.data) < 0.01f) { player->anim_state = ANIM_IDLE; }
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
		// glm_vec3_scale(direction.data, sprint * 18.0f * ctx->dt, direction.data);

		// if (camera->fov < 12.0f) { camera->fov = 12.0f; }
		// else if (camera->fov > 80.0f) { camera->fov = 80.0f; }
		// glm_vec3_add(rb->velocity.data, direction.data, rb->velocity.data);

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
#endif

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
#elif 1 // silent hill
#	define WIDTH		   800
#	define HEIGHT		   600
#	define FRAMEBUFFER_WIDTH  320
#	define FRAMEBUFFER_HEIGHT 224
#else
#	define WIDTH  800
#	define HEIGHT 450
#endif
	struct core_init init_c = {
	    .argc = argc,
	    .argv = argv,
	    .title = str8_from("Terror em SL"),
	    .w = WIDTH,
	    .h = HEIGHT,
	    .total_memory = MB(32),
	    .target_fps = 30,
	    .fixed_fps = 48,

	    .prng_seed = 42,
	    .user_data = 0,
	    .assets_folder = str8_from("assets/"),
	    .pipeline = {.on_attach = on_attach, .on_update = on_update, .on_draw = on_draw, .on_detach = on_detach}
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
	struct scene *scene01 = stage_scene_new(str8_from("scene01"));
	scene_mount_pipeline(scene01, scene01_on_attach, scene01_on_update, scene01_on_draw, 0);
	stage_on_attach(ctx);

#if 0 
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

		// stage_system_register(str8_from("Mesh"), scene_mesh_calculate_aabb_update, 0);
		// stage_system_register(str8_from("Rigid body"), scene_rigid_body_update, 0);
		// stage_system_register(str8_from("Player"), scene_player_update_viniL, 0);
		// stage_system_register(str8_from("Camera"), scene_camera_update, 0);
		// stage_system_register(str8_from("Hierarchy"), scene_hierarchy_update, 0);
		// stage_system_register(str8_from("Transform clear"), scene_transform_clear_dirty, 0);
		// stage_system_register(str8_from("Cross fade controller"), scene_cfc_update, 0);
		// stage_system_register(str8_from("Fade to"), scene_fade_to_update, 0);
		// stage_system_register(str8_from("Palette"), scene_m4_palette_update, 0);

		stage_scene_asset_load(str8_from("Triangulo"));
		{
			entity_t viniL = stage_scene_asset_load(str8_from("viniL"));
			if (viniL.handle != INVALID_HANDLE)
			{
				struct resource *resource_clip = resource_get_by_label(str8_from("idle"));
				sm__assert(resource_clip);
				clip_component *clip = stage_component_get_data(viniL, CLIP);
				clip->next_clip_handle.id = resource_clip->slot.id;

				transform_component *transform = stage_component_get_data(viniL, TRANSFORM);
				transform->transform_local.translation = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
				transform->transform_local.scale = v3_one();
				transform->transform_local.rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);

				rigid_body_component *rigid_body = stage_component_get_data(viniL, RIGID_BODY);
				rigid_body->velocity = v3_zero();
				// rigid_body->gravity = v3_zero();
				rigid_body->collision_shape = RB_SHAPE_CAPSULE;
				rigid_body->has_gravity = true;

				v3 base = transform->transform_local.translation.v3;
				v3 tip = transform->transform_local.translation.v3;
				tip.y += 2.1f;

				player_component *player = stage_component_get_data(viniL, PLAYER);
				f32 speed = 8.0f;
				memcpy((f32 *)&player->speed, &speed, sizeof(f32));
				player->anim_state = ANIM_IDLE;
				player->target_angle = 0.0f;

				rigid_body->capsule = (struct capsule){.base = base, .tip = tip, 0.4f};
			}
		}
		{
			entity_t spider = stage_scene_asset_load(str8_from("spider"));
			if (spider.handle != INVALID_HANDLE)
			{
				struct resource *spider_walk = resource_get_by_label(str8_from("spider-walk"));
				sm__assert(spider_walk);
				clip_component *spider_clip = stage_component_get_data(spider, CLIP);
				spider_clip->next_clip_handle.id = spider_walk->slot.id;
			}
		}
	}

#endif
	stage_set_current_by_name(str8_from("scene01"));
}

void
on_update(sm__maybe_unused struct ctx *ctx)
{
}

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

void
on_draw(sm__maybe_unused struct ctx *ctx)
{
}

void
on_detach(sm__maybe_unused struct ctx *ctx)
{
}
