#include "animation/smAnimation.h"
#include "audio/smAudio.h"
#include "core/smCore.h"

#include "ecs/smScene.h"

#include "common.h"

struct scene01
{
	entity_t camera_ett;
	entity_t player_ett;

	struct
	{
		pass_handle pass;
		pipeline_handle pipeline;
		shader_handle program;
		sampler_handle sampler;

		pipeline_handle skinned_pipeline;
		shader_handle skinned_program;
	} first;

	struct
	{
		struct renderer_pass_action pass_action;
		sampler_handle sampler;
		pipeline_handle pipeline;
		struct renderer_bindings bind;
	} display;
};

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

b32
scene01_player_update(
    struct arena *arena, struct scene *scene, sm__maybe_unused struct ctx *ctx, sm__maybe_unused void *user_data)
{
	struct scene01 *scene01 = user_data;
	struct scene_iter iter = scene_iter_begin(
	    scene, TRANSFORM | MESH | MATERIAL | ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER | RIGID_BODY | PLAYER);
	while (scene_iter_next(scene, &iter))
	{
		entity_t player_ett = scene_iter_get_entity(&iter);
		// entity_t cam_ett = scene_get_main_camera(scene);
		camera_component *camera = scene_component_get_data(scene, scene01->camera_ett, CAMERA);
		transform_component *camera_transform = scene_component_get_data(scene, scene01->camera_ett, TRANSFORM);

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
		if (camera->flags & CAMERA_FLAG_FREE)
		{
			input = v3_zero();
		}
		else
		{
			input = v3_new(core_key_pressed(KEY_A) - core_key_pressed(KEY_D), 0,
			    core_key_pressed(KEY_W) - core_key_pressed(KEY_S));
		}

		v3 direction;
		glm_vec3_normalize_to(input.data, direction.data);
		f32 dir_len = glm_vec3_norm(direction.data);
		if (dir_len > 0.2f)
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

			if (glm_vec3_norm(rb->force.data) < 0.01f)
			{
				player->anim_state = ANIM_IDLE;
			}
		}
		else
		{
			player->anim_state = ANIM_IDLE;
			player->target_angle = 0.0f;
		}

		camera->third_person.target = transform->transform_local.translation.v3;
		camera->third_person.target.y += glm_vec3_distance(rb->capsule.tip.data, rb->capsule.base.data);

		// glm_vec3_smoothinterp(camera->target.data, target.data, 10.0f * ctx->dt, camera->target.data);

		if (core_key_pressed(KEY_H))
		{
			player->anim_state = ANIM_LEAN_LEFT;
		}
		struct resource *resource_clip = resource_get_by_label(ctable_animation_names[player->anim_state]);
		if (resource_clip)
		{
			clip->next_clip_handle.id = resource_clip->slot.id;
		}
		else
		{
			log_warn(
			    str8_from("[{s}] clip resource not found"), ctable_animation_names[player->anim_state]);
		}

		if (player->anim_state == ANIM_RUNNING)
		{
			struct sm__resource_clip *clip_resource = resource_clip_at(clip->current_clip_handle);
			for (u32 i = 0; i < array_len(clip_resource->tracks); ++i)
			{
				struct transform_track *tt = &clip_resource->tracks[i];
				f32 t = resource_clip_adjust_time(clip->current_clip_handle, clip->time);

				i32 frame_index = track_frame_index(&tt->position, t, clip_resource->looping);
				i32 trigger_frames[2] = {0, 8};

				b8 found = 0;
				for (u32 j = 0; j < ARRAY_SIZE(trigger_frames); ++j)
				{
					if (frame_index == trigger_frames[j])
					{
						str8 audios[] = {
						    str8_from("step1"), str8_from("step2"), str8_from("step3")};
						u32 idx = prng_min_max(0, ARRAY_SIZE(audios));
						audio_set_position(audios[idx], rb->capsule.base);
						audio_play(audios[idx]);
						found = 1;
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
		}
		if (player->anim_state == ANIM_WALKING)
		{
			struct sm__resource_clip *clip_resource = resource_clip_at(clip->current_clip_handle);
			for (u32 i = 0; i < array_len(clip_resource->tracks); ++i)
			{
				struct transform_track *tt = &clip_resource->tracks[i];
				f32 t = resource_clip_adjust_time(clip->current_clip_handle, clip->time);

				i32 frame_index = track_frame_index(&tt->position, t, clip_resource->looping);
				i32 trigger_frames[2] = {10, 24};

				b8 found = 0;
				for (u32 j = 0; j < ARRAY_SIZE(trigger_frames); ++j)
				{
					if (frame_index == trigger_frames[j])
					{
						str8 audios[] = {
						    str8_from("step1"), str8_from("step2"), str8_from("step3")};
						u32 idx = prng_min_max(0, ARRAY_SIZE(audios));
						audio_set_position(audios[idx], rb->capsule.base);
						audio_play(audios[idx]);
						found = 1;
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
		}
	}

	return (1);
}

void
scene01_on_attach(struct arena *arena, sm__maybe_unused struct scene *scene, struct ctx *ctx)
{
	struct scene01 *scene01 = arena_reserve(arena, sizeof(struct scene01));
	scene->user_data = scene01;

	scene01->camera_ett = scene_entity_new(arena, scene, CAMERA | TRANSFORM);
	scene_set_main_camera(scene, scene01->camera_ett);

	camera_component *camera = scene_component_get_data(scene, scene01->camera_ett, CAMERA);
	transform_component *camera_transform = scene_component_get_data(scene, scene01->camera_ett, TRANSFORM);
	{
		camera_transform->matrix_local = m4_identity();
		camera_transform->transform_local = trs_identity();
		camera_transform->transform_local.translation.v3 = v3_new(0.0f, 1.0f, 1.0f);

		camera_transform->matrix = m4_identity();
		camera_transform->last_matrix = m4_identity();
	}
	{
		camera->z_near = 0.1f;
		camera->z_far = 100.0f;
		camera->fovx = glm_rad(75.0f);
		camera->aspect_ratio = ((f32)ctx->win_width / (f32)ctx->win_height);
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

		camera->free.lerp_to_target_p = 0;
		camera->free.lerp_to_target_r = 0;

		camera->third_person.target_distance = 5;
		camera->third_person.target = v3_zero();
		camera->third_person.rotation_deg = v2_zero();
		camera->third_person.mouse_smoothed = v2_zero();
	}

	text_resource default_vert = resource_text_get_by_label(str8_from("shaders/default3D.vertex"));
	text_resource default_frag = resource_text_get_by_label(str8_from("shaders/default3D.fragment"));

	struct renderer_shader_desc desc2 = {
	    .vs = {.handle = default_vert},
	    .fs = {.handle = default_frag},
	    .label = str8_from("default_program3D"),
	};

	scene01->first.program = renderer_shader_make(&desc2);

	struct renderer_sampler_desc default_sampler = {
	    .label = str8_from("default_sampler"),
	    .mag_filter = FILTER_NEAREST,
	    .min_filter = FILTER_NEAREST,
	};
	scene01->first.sampler = renderer_sampler_make(&default_sampler);

	struct renderer_pipeline_desc pip_desc = {.label = str8_from("default pipeline"),
	    .shader = scene01->first.program,
	    .depth = {.enable = STATE_TRUE},
	    .rasterizer = {.cull_enable = STATE_TRUE},
	    .layout = {.attrs = {
			   {.name = str8_from("a_position"), .format = VERTEX_FORMAT_FLOAT3, .buffer_index = 0},
			   {.name = str8_from("a_uv"), .format = VERTEX_FORMAT_FLOAT2, .buffer_index = 1},
			   {.name = str8_from("a_color"), .format = VERTEX_FORMAT_FLOAT4, .buffer_index = 2},
			   {.name = str8_from("a_normal"), .format = VERTEX_FORMAT_FLOAT3, .buffer_index = 3},
		       }}};
	scene01->first.pipeline = renderer_pipeline_make(&pip_desc);

	text_resource skinned_vert = resource_text_get_by_label(str8_from("shaders/skinned.vertex"));
	text_resource skinned_frag = resource_text_get_by_label(str8_from("shaders/default3D.fragment"));

	struct renderer_shader_desc skinned_desc = {
	    .vs = {.handle = skinned_vert},
	    .fs = {.handle = default_frag},
	    .label = str8_from("skinned_program"),
	};

	scene01->first.skinned_program = renderer_shader_make(&skinned_desc);

	struct renderer_pipeline_desc skinned_pip_desc = {.label = str8_from("skinned pipeline"),
	    .shader = scene01->first.skinned_program,
	    .depth = {.enable = STATE_TRUE},
	    .rasterizer = {.cull_enable = STATE_FALSE},
	    .layout = {.attrs = {
			   {.name = str8_from("a_position"), .format = VERTEX_FORMAT_FLOAT3, .buffer_index = 0},
			   {.name = str8_from("a_uv"), .format = VERTEX_FORMAT_FLOAT2, .buffer_index = 1},
			   {.name = str8_from("a_color"), .format = VERTEX_FORMAT_FLOAT4, .buffer_index = 2},
			   {.name = str8_from("a_normal"), .format = VERTEX_FORMAT_FLOAT3, .buffer_index = 3},
			   {.name = str8_from("a_weights"), .format = VERTEX_FORMAT_FLOAT4, .buffer_index = 4},
			   {.name = str8_from("a_joints"), .format = VERTEX_FORMAT_FLOAT4, .buffer_index = 5},
		       }}};
	scene01->first.skinned_pipeline = renderer_pipeline_make(&skinned_pip_desc);

	struct renderer_texture_desc text_desc = {
	    .width = 320,
	    .height = 224,
	    .pixel_format = TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	    // .sample_count = OFFSCREEN_SAMPLE_COUNT,
	    .label = str8_from("color_image"),
	};
	texture_handle txt_color = renderer_texture_make(&text_desc);

#if 0 
	struct renderer_texture_desc text_dummy_desc = {
	    .label = str8_from("witch-finger"), .handle = resource_image_get_by_label(str8_from("witch-finger")),
	    // .handle = image_resource_
	};
	texture_handle text_dummy = renderer_texture_make(&text_dummy_desc);
#endif

	text_desc.pixel_format = TEXTURE_PIXELFORMAT_DEPTH;
	text_desc.label = str8_from("depth-image");
	texture_handle txt_depth = renderer_texture_make(&text_desc);

	pass_handle offscreen_pass = renderer_pass_make(&(struct renderer_pass_desc){
	    .label = str8_from("offscreen_pass"),
	    .color_attachments = {[0] = txt_color},
	    .depth_stencil_attachment = txt_depth,
	});
	scene01->first.pass = offscreen_pass;

	// DEFAULT BEGIN

	text_resource framebuffer_vert = resource_text_get_by_label(str8_from("shaders/framebuffer.vertex"));
	text_resource framebuffer_frag = resource_text_get_by_label(str8_from("shaders/dither.fragment"));
	// text_resource framebuffer_frag = resource_text_get_by_label(str8_from("shaders/framebuffer.fragment"));

	struct renderer_shader_desc default_desc = {
	    .vs = {.handle = framebuffer_vert},
	    .fs = {.handle = framebuffer_frag},
	    .label = str8_from("default_framebuffer"),
	};

	shader_handle default_shader = renderer_shader_make(&default_desc);

	scene01->display.sampler = scene01->first.sampler;

	texture_handle bayer = renderer_texture_make(&(struct renderer_texture_desc){
	    .label = str8_from("bayer8tile4"), .handle = resource_image_get_by_label(str8_from("bayer16tile2"))});

	// clang-format off
	static f32 rectangle_vertices[] = 
	{
		// Coords    // texCoords
		1.0f, -1.0f,  1.0f, 0.0f,
		-1.0f, -1.0f,  0.0f, 0.0f,
		-1.0f,  1.0f,  0.0f, 1.0f,

		1.0f,  1.0f,  1.0f, 1.0f,
		1.0f, -1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f,  0.0f, 1.0f
	};
	// clang-format on

	buffer_handle rectangle_buffer = renderer_buffer_make(&(struct renderer_buffer_desc){
	    .data = &rectangle_vertices,
	    .size = sizeof(rectangle_vertices),
	    .usage = BUFFER_USAGE_IMMUTABLE,
	    .label = str8_from("default_rectangle"),
	});

	struct renderer_pipeline_desc default_pipeline = {.label = str8_from("default_pipeline"),
	    .shader = default_shader,
	    .depth = {.enable = STATE_FALSE},
	    .rasterizer = {.cull_enable = STATE_FALSE},
	    .layout = {.attrs = {
			   {.name = str8_from("a_position"), .format = VERTEX_FORMAT_FLOAT2},
			   {.name = str8_from("a_uv"), .format = VERTEX_FORMAT_FLOAT2, .offset = 8},
		       }}};
	scene01->display.pipeline = renderer_pipeline_make(&default_pipeline);

	struct renderer_pass_action default_pass_action = (struct renderer_pass_action){
	    .colors[0] =
		{
			    .load_action = LOAD_ACTION_CLEAR,
			    .clear_value = color_from_v3(v4_new(0.07, 0.07, 0.07, 1.0f)),
			    },
	};
	scene01->display.pass_action = default_pass_action;

	struct renderer_bindings default_bindings = {
	    .textures =
		{
			   {.name = str8_from("u_framebuffer"), .texture = txt_color, .sampler = scene01->display.sampler},
			   {
			.name = str8_from("u_bayer"),
			.texture = bayer,
			.sampler = scene01->display.sampler,
		    }, },
	    .buffers =
		{
			   {.name = str8_from("a_position"), .buffer = rectangle_buffer},
			   {.name = str8_from("a_uv"), .buffer = rectangle_buffer},
			   },
	};

	scene01->display.bind = default_bindings;

	// struct renderer_pass pass_dec = {.label = };

	// struct resource_text_desc vertex = {.label = str8_from("vs_3D"), .text = };
	// renderer_shader_add(
	// str8_from("bayer"), str8_from("shaders/bayer.vertex"), str8_from("shaders/bayer.fragment"));
	// renderer_shader_add(str8_from("skinned3D"), str8_from("shaders/skinned.vertex"),
	// str8_from("shaders/default3D.fragment"));

	// renderer_texture_add(str8_from("bayer16tile2"));
	// renderer_texture_add(str8_from("bayer8tile4"));
	// renderer_texture_add(str8_from("witch-finger"));
	// renderer_texture_add(str8_from("toshibasat8x8"));
	// renderer_texture_add(str8_from("spark_flame"));
	// renderer_texture_add(str8_from("Woman"));

	audio_set_master_volume(1.0f);
	audio_add_music(str8_from("bg_music1"), str8_from("exported/ghost-love.wav"));
	audio_play(str8_from("bg_music1"));
	audio_add_sound(str8_from("step1"), str8_from("exported/foottapping_01.wav"));
	audio_add_sound(str8_from("step2"), str8_from("exported/foottapping_02.wav"));
	audio_add_sound(str8_from("step3"), str8_from("exported/foottapping_03.wav"));

	scene_system_register(arena, scene, str8_from("Mesh"), common_mesh_calculate_aabb_update, scene01);
	scene_system_register(arena, scene, str8_from("Rigid body"), common_rigid_body_update, scene01);
	scene_system_register(arena, scene, str8_from("Particle emitter"), common_particle_emitter_update, scene01);
	scene_system_register(arena, scene, str8_from("Player"), scene01_player_update, scene01);
	scene_system_register(arena, scene, str8_from("Camera"), common_camera_update, scene01);
	scene_system_register(arena, scene, str8_from("Hierarchy"), common_hierarchy_update, scene01);
	scene_system_register(arena, scene, str8_from("Transform clear"), common_transform_clear_dirty, scene01);
	scene_system_register(arena, scene, str8_from("Particle emitter sort"), common_pe_sort_update, scene01);
	scene_system_register(arena, scene, str8_from("Cross fade controller"), common_cfc_update, scene01);
	scene_system_register(arena, scene, str8_from("Fade to"), common_fade_to_update, scene01);
	scene_system_register(arena, scene, str8_from("Palette"), common_m4_palette_update, scene01);

	// scene_load(arena, scene, str8_from("praca-scene"));
	// scene_load(arena, scene, str8_from("simple-cube-scene"));
	// scene_load(arena, scene, str8_from("cube-scene"));
	scene_load(arena, scene, str8_from("mainscene"));
	// scene_load(arena, scene, str8_from("n"));
	// scene_load(arena, scene, str8_from("dense"));
	// scene_load(arena, scene, str8_from("cubes"));

	// scene_load(arena, scene, str8_from("Scene"));
	// scene_load(arena, scene, str8_from("hierarchy"));

#if 0

	// particle emitter
	{
		component_t emitter_archetype = TRANSFORM | PARTICLE_EMITTER;
		entity_t emitter = scene_entity_new(arena, scene, emitter_archetype);

		transform_component *partcile_transform = scene_component_get_data(scene, emitter, TRANSFORM);
		transform_init(partcile_transform);

		particle_emitter_component *p_emitter = scene_component_get_data(scene, emitter, PARTICLE_EMITTER);
		p_emitter->emission_rate = 32;
		particle_emitter_init(arena, p_emitter, 256);
		struct aabb aabb_emitter = {.min = v3_new(-2, 0, -2), .max = v3_new(2, 0, 2)};
		// particle_emitter_set_shape_box(p_emitter, aabb_emitter);
		trs cube = ((union trs){
		    .translation = v4_zero(), .rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f), .scale = v3_new(4, 4, 4)});
		particle_emitter_set_shape_cube(p_emitter, cube);

		struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH);
		while (scene_iter_next(scene, &iter))
		{
			mesh_component *mesh = scene_iter_get_component(&iter, MESH);

			if (str8_eq(mesh->resource_ref->label, str8_from("child-cone-mesh")))
			{
				// transform_set_parent(current_scene_arena, partcile_transform, transform);

				entity_t entity = scene_iter_get_entity(&iter);
				scene_entity_set_parent(scene, emitter, entity);
			}
		}
	}
#endif

	scene_load(arena, scene, str8_from("woman"));
	entity_t player_ett = {INVALID_HANDLE};

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH | ARMATURE);
	while (scene_iter_next(scene, &iter))
	{
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);

		if (str8_eq(mesh->resource_ref->label, str8_from("woman-mesh.002")))
		{
			player_ett = scene_iter_get_entity(&iter);
		}
	}

	if (player_ett.handle != INVALID_HANDLE)
	{
		scene_entity_add_component(arena, scene, player_ett, RIGID_BODY | PLAYER);
		// player_ett = stage_animated_asset_load(str8_from("exported/Woman.gltf"));

		transform_component *transform = scene_component_get_data(scene, player_ett, TRANSFORM);
		rigid_body_component *rigid_body = scene_component_get_data(scene, player_ett, RIGID_BODY);
		material_component *material = scene_component_get_data(scene, player_ett, MATERIAL);
		player_component *player = scene_component_get_data(scene, player_ett, PLAYER);
		// material->material_ref->image = str8_from("Woman");
		// mesh->mesh_ref->flags |= MESH_FLAG_DRAW_AABB;
		// mesh_component *mesh = stage_component_get_data(player_ett, MESH);

		transform->transform_local.translation = v4_new(0.0f, 19.0f, 0.0f, 0.0f);
		transform->transform_local.scale = v3_new(0.0042f, 0.0042f, 0.0042f);
		transform->transform_local.rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f);

		rigid_body->velocity = v3_zero();
		// rigid_body->gravity = v3_zero();
		rigid_body->collision_shape = RB_SHAPE_CAPSULE;
		rigid_body->has_gravity = 1;

		v3 base = transform->transform_local.translation.v3;
		v3 tip = transform->transform_local.translation.v3;
		tip.y += 2.1f;

		f32 speed = 8.0f;
		memcpy((f32 *)&player->speed, &speed, sizeof(f32));
		player->anim_state = ANIM_IDLE;
		player->target_angle = 0.0f;

		rigid_body->capsule = (struct capsule){.base = base, .tip = tip, 0.4f};
	}

	scene01->player_ett = player_ett;
}

void
scene01_on_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data)
{
	struct scene01 *scene01 = user_data;

	camera_component *camera = scene_component_get_data(scene, scene01->camera_ett, CAMERA);

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH);
	while (scene_iter_next(scene, &iter))
	{
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);

		// scene_entity_update_hierarchy(transform);
		if (str8_eq(mesh->resource_ref->label, str8_from("child-cone-mesh")))
		{
			entity_t entity = scene_iter_get_entity(&iter);
			v4 q;
			f32 x = (sinf(ctx->time)) * 4.0f;

			scene_entity_update_hierarchy(scene, entity);
			glm_quat(q.data, glm_rad(90.0 * ctx->dt), 1.0f, 0.0f, 0.0f);
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

void
scene01_on_draw(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data)
{
	struct scene01 *scene01 = user_data;

	camera_component *camera = scene_component_get_data(scene, scene01->camera_ett, CAMERA);
	transform_component *camera_transform = scene_component_get_data(scene, scene01->camera_ett, TRANSFORM);

	// m4 view = camera->view;
	m4 view_projection_matrix = camera->view_projection;

#if 1
	renderer_pass_begin(scene01->first.pass, &scene01->display.pass_action);
	renderer_pipiline_apply(scene01->first.pipeline);

	struct scene_iter iter = scene_iter_begin(scene, TRANSFORM | MESH | MATERIAL);
	while (scene_iter_next(scene, &iter))
	{
		entity_t entity = scene_iter_get_entity(&iter);
		if (scene_entity_has_components(scene, entity, ARMATURE))
		{
			continue;
		}

		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		material_component *material = scene_iter_get_component(&iter, MATERIAL);

		struct sm__resource_mesh *mesh_resource = resource_mesh_at(mesh->mesh_handle);
		struct sm__resource_material *material_resource = resource_material_at(material->material_handle);

		struct renderer_bindings bind = {
		    .buffers =
			{
				  {.name = str8_from("a_position"), .buffer = mesh->position_buffer},
				  {.name = str8_from("a_uv"), .buffer = mesh->uv_buffer},
				  {.name = str8_from("a_color"), .buffer = mesh->color_buffer},
				  {.name = str8_from("a_normal"), .buffer = mesh->normal_buffer},
				  },
		    .index_buffer = mesh->index_buffer,

		    .textures =
			{
				  {
				.name = str8_from("u_tex0"),
				.texture = material->texture_handle,
				.sampler = scene01->first.sampler,
			    }, },
		    .uniforms =
			{
				  {.name = str8_from("u_pv"), .type = SHADER_TYPE_M4, .data = &view_projection_matrix},
				  {.name = str8_from("u_model"), .type = SHADER_TYPE_M4, .data = &transform->matrix},
				  {
				.name = str8_from("u_diffuse_color"),
				.type = SHADER_TYPE_V4,
				.data = &color_to_v4(material_resource->color),
			    }, },
		};

		renderer_bindings_apply(&bind);
		renderer_draw(array_len(mesh_resource->indices));
	}

#	if 0

	renderer_pipiline_apply(scene01->first.skinned_pipeline);
	iter = scene_iter_begin(scene, TRANSFORM | MESH | MATERIAL | ARMATURE);
	while (scene_iter_next(scene, &iter))
	{
		transform_component *transform = scene_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = scene_iter_get_component(&iter, MESH);
		material_component *material = scene_iter_get_component(&iter, MATERIAL);

		struct sm__resource_mesh *mesh_resource = resource_mesh_at(mesh->mesh_handle);
		struct sm__resource_material *material_resource = resource_material_at(material->material_handle);

		u32 palette_len = array_len(mesh_resource->skin_data.pose_palette);
		m4 *palette_ptr = mesh_resource->skin_data.pose_palette;

		struct renderer_bindings bind = {
		    .buffers =
			{
				  {.name = str8_from("a_position"), .buffer = mesh->position_buffer},
				  {.name = str8_from("a_uv"), .buffer = mesh->uv_buffer},
				  {.name = str8_from("a_color"), .buffer = mesh->color_buffer},
				  {.name = str8_from("a_normal"), .buffer = mesh->normal_buffer},
				  {.name = str8_from("a_weights"), .buffer = mesh->weights_buffer},
				  {.name = str8_from("a_joints"), .buffer = mesh->influences_buffer},
				  },
		    .index_buffer = mesh->index_buffer,

		    .textures =
			{
				  {.name = str8_from("u_tex0"),
				.texture = material->texture_handle,
				.sampler = scene01->first.sampler},
				  },
		    .uniforms =
			{
				  {.name = str8_from("u_pv"), .type = SHADER_TYPE_M4, .data = &view_projection_matrix},
				  {.name = str8_from("u_model"), .type = SHADER_TYPE_M4, .data = &transform->matrix},
				  {
				.name = str8_from("u_animated"),
				.type = SHADER_TYPE_M4,
				.data = palette_ptr,
				.count = palette_len,
			    }, {
				.name = str8_from("u_diffuse_color"),
				.type = SHADER_TYPE_V4,
				// .data = &v4_new(0.0f, 1.0f, 0.0f, 1.0f),
				.data = &color_to_v4(material_resource->color),
			    }, },
		};

		renderer_bindings_apply(&bind);
		renderer_draw(array_len(mesh_resource->indices));
	}
#	endif

	renderer_pass_end();

	// Begin the default pass
	renderer_pass_begin((pass_handle){0}, &scene01->display.pass_action);
	renderer_pipiline_apply(scene01->display.pipeline);
	renderer_bindings_apply(&scene01->display.bind);
	renderer_draw(6);
	renderer_pass_end();
#endif

#if 0
	iter = stage_iter_begin(TRANSFORM | MESH | MATERIAL | ARMATURE);
	while (stage_iter_next(&iter))
	{
		transform_component *transform = stage_iter_get_component(&iter, TRANSFORM);
		mesh_component *mesh = stage_iter_get_component(&iter, MESH);
		material_component *material = stage_iter_get_component(&iter, MATERIAL);

		struct sm__resource_mesh *mesh_resource = resource_mesh_at(mesh->mesh_handle);
		struct sm__resource_material *material_resource = resource_material_at(material->material_handle);

		renderer_state_clear();

		renderer_shader_set(str8_from("skinned3D"));

		renderer_shader_set_uniform(str8_from("u_pv"), &view_projection_matrix, sizeof(m4), 1);
		renderer_shader_set_uniform(str8_from("u_model"), &transform->matrix, sizeof(m4), 1);

		u32 palette_len = array_len(mesh_resource->skin_data.pose_palette);
		m4 *palette_ptr = mesh_resource->skin_data.pose_palette;
		renderer_shader_set_uniform(str8_from("u_animated"), palette_ptr, sizeof(m4), palette_len);

		if (material_resource)
		{
			renderer_shader_set_uniform(
			    str8_from("u_diffuse_color"), &color_to_v4(material_resource->color), sizeof(v4), 1);
			renderer_texture_set(material_resource->image, str8_from("u_tex0"));
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

#endif
}
