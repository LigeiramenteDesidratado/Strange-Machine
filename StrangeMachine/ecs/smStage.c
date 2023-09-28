#include "ecs/smStage.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "ecs/smScene.h"

struct scene_object
{
	str8 name;
	struct scene_object *next;
	struct scene_object *prev;

	struct arena arena;
	struct scene scene;
};

struct stage
{
	struct arena global_arena;
	u32 sub_arena_size;

	struct scene_object scenes_active;
	struct scene_object scenes_free;

	struct scene_object *current;

	struct scene_object scenes[8]; // max scenes
};

static struct stage SC; // SCENE CONTEXT

b8
stage_init(struct buf base_memory)
{
	arena_make(&SC.global_arena, base_memory);
	SC.sub_arena_size = base_memory.size / 8;

	dll_init_sentinel(&SC.scenes_active);
	dll_init_sentinel(&SC.scenes_free);

	for (u32 i = 0; i < ARRAY_SIZE(SC.scenes); ++i) { dll_insert_back(&SC.scenes_free, SC.scenes + i); }

	return (true);
}

void
stage_teardown(void)
{
	for (struct scene_object *sn = SC.scenes_active.next; sn != &SC.scenes_active; sn = sn->next)
	{
		scene_unmake_refs(&sn->scene);
	}

	arena_release(&SC.global_arena);
	SC = (struct stage){0};
}

void
stage_do(struct ctx *ctx)
{
	scene_system_run(&SC.current->arena, &SC.current->scene, ctx);
}

void
stage_set_main_camera(entity_t camera_entity)
{
	scene_set_main_camera(&SC.current->scene, camera_entity);
}

camera_component *
stage_get_main_camera(void)
{
	camera_component *result;
	entity_t cam = scene_get_main_camera(&SC.current->scene);
	result = scene_component_get_data(&SC.current->scene, cam, CAMERA);
	return (result);
}

entity_t
stage_get_main_camera_entity(void)
{
	entity_t result;
	result = scene_get_main_camera(&SC.current->scene);
	return (result);
}

void
stage_set_gravity_force(v3 gravity_force)
{
	scene_set_gravity_force(&SC.current->scene, gravity_force);
}

v3
stage_get_gravity_force(void)
{
	v3 result;

	result = SC.current->scene.gravity_force;

	return (result);
}

static void
sm__stage_construct(struct scene_object *scene_obj)
{
	struct buf base_memory = {
	    .data = arena_reserve(&SC.global_arena, SC.sub_arena_size), .size = SC.sub_arena_size};
	arena_make(&scene_obj->arena, base_memory);
	scene_make(&scene_obj->arena, &scene_obj->scene);
}

void
stage_scene_new(str8 name)
{
	for (struct scene_object *n = SC.scenes_active.next; n != &SC.scenes_active; n = n->next)
	{
		if (str8_eq(name, n->name))
		{
			log_error(str8_from("scene with {s} already exist"), name);
			exit(1);
		}
	}
	struct scene_object *n = SC.scenes_free.next;
	if (n != &SC.scenes_free)
	{
		dll_remove(n);
		dll_insert(&SC.scenes_active, n);
		n->name = name;
		SC.current = n;
		sm__stage_construct(SC.current);

		return;
	}

	assert(0);
}

void
stage_set_current_by_name(str8 name)
{
	for (struct scene_object *n = SC.scenes_active.next; n != &SC.scenes_active; n = n->next)
	{
		if (str8_eq(name, n->name))
		{
			SC.current = n;
			return;
		}
	}

	log_warn(str8_from("scene {s} not found"), name);
	assert(0);
}

b8
stage_is_current_scene(str8 name)
{
	return str8_eq(name, SC.current->name);
}

entity_t
stage_scene_asset_load(str8 name)
{
	return scene_load(&SC.current->arena, &SC.current->scene, name);
}

struct arena *
stage_scene_get_arena(void)
{
	struct arena *result;

	result = &SC.current->arena;

	return (result);
}

struct scene *
stage_get_current_scene(void)
{
	struct scene *result;

	result = &SC.current->scene;

	return (result);
}

entity_t
stage_animated_asset_load(str8 name)
{
	(void)name;
	// return scene_load_animated(&SC.current->arena, &SC.current->scene, name);

	return (entity_t){0};
}

entity_t
stage_entity_new(component_t archetype)
{
	return scene_entity_new(&SC.current->arena, &SC.current->scene, archetype);
}

void
stage_entity_remove(entity_t entity)
{
	scene_entity_remove(&SC.current->scene, entity);
}

b8
stage_entity_is_valid(entity_t entity)
{
	return scene_entity_is_valid(&SC.current->scene, entity);
}

b8
stage_entity_has_components(entity_t entity, component_t components)
{
	return scene_entity_has_components(&SC.current->scene, entity, components);
}

void
stage_entity_add_component(entity_t entity, component_t components)
{
	scene_entity_add_component(&SC.current->arena, &SC.current->scene, entity, components);
}

void *
stage_component_get_data(entity_t entity, component_t component)
{
	return scene_component_get_data(&SC.current->scene, entity, component);
}

void
stage_system_register(str8 name, system_f system, void *user_data)
{
	scene_system_register(&SC.current->arena, &SC.current->scene, name, system, user_data);
}

struct scene_iter
stage_iter_begin(component_t constraint)
{
	return scene_iter_begin(&SC.current->scene, constraint);
}

b8
stage_iter_next(struct scene_iter *iter)
{
	return scene_iter_next(&SC.current->scene, iter);
}

void *
stage_iter_get_component(struct scene_iter *iter, component_t component)
{
	return scene_iter_get_component(iter, component);
}
