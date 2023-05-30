#include "ecs/smScene.h"
#include "core/smArray.h"
#include "core/smLog.h"
#include "core/smResource.h"
#include "ecs/smECS.h"

void
scene_make(struct arena *arena, struct scene *scene)
{
	handle_pool_make(arena, &scene->indirect_handle_pool, 8);
	array_set_cap(arena, scene->indirect_access, scene->indirect_handle_pool.cap);
	scene->component_handle_pool = 0;
	scene->sys_info = 0;
	scene->main_camera = (entity_t){INVALID_HANDLE};
	scene->gravity_force = v3_zero();
}

void
scene_release(struct arena *arena, struct scene *scene)
{
	array_release(arena, scene->sys_info);
	for (u32 i = 0; i < array_len(scene->component_handle_pool); ++i)
	{
		component_pool_release(arena, &scene->component_handle_pool[i]);
	}
	array_release(arena, scene->component_handle_pool);

	handle_pool_release(arena, &scene->indirect_handle_pool);
	array_release(arena, scene->indirect_access);
}

static handle_t
sm__scene_indirect_access_new_handle(struct arena *arena, struct scene *scene)
{
	handle_t result;

	result = handle_new(arena, &scene->indirect_handle_pool);
	if (array_cap(scene->indirect_access) != scene->indirect_handle_pool.cap)
	{
		array_set_cap(arena, scene->indirect_access, scene->indirect_handle_pool.cap);
	}

	return (result);
}

void
scene_unmake_refs(struct scene *scene)
{
	for (u32 i = 0; i < array_len(scene->component_handle_pool); ++i)
	{
		component_pool_unmake_refs(&scene->component_handle_pool[i]);
	}
}

void
scene_load(struct arena *arena, struct scene *scene, str8 name)
{
	struct resource *scene_resource = resource_get_by_name(name);
	if (scene_resource == 0)
	{
		log_error(str8_from("[{s}] scene not found"), name);
		return;
	}
	resource_make_reference(scene_resource);

	const struct scene_resource *r_scene = scene_resource->scene_data;

	struct child_parent_hierarchy
	{
		component_t archetype;
		struct scene_node *child_ptr, *parent_ptr;
		entity_t child_ett, parent_ett;
	};

	struct child_parent_hierarchy *ett_h = 0;
	array_set_len(arena, ett_h, array_len(r_scene->nodes));
	for (u32 i = 0; i < array_len(r_scene->nodes); ++i)
	{
		struct scene_node *node = &r_scene->nodes[i];

		component_t archetype = TRANSFORM | WORLD | HIERARCHY;
		if (node->mesh.size > 0) { archetype |= MESH | MATERIAL | STATIC_BODY; }

		entity_t ett = scene_entity_new(arena, scene, archetype);

		ett_h[i].archetype = archetype;

		ett_h[i].child_ptr = node;
		ett_h[i].parent_ptr = (node->parent_index > -1) ? &r_scene->nodes[node->parent_index] : 0;

		ett_h[i].child_ett = ett;
		ett_h[i].parent_ett = (entity_t){INVALID_HANDLE};
	}

	for (u32 i = 0; i < array_len(ett_h); ++i)
	{
		struct child_parent_hierarchy *child = &ett_h[i];
		if (child->parent_ptr == 0) { continue; }

		for (u32 j = 0; j < array_len(ett_h); ++j)
		{
			if (i == j) { continue; }
			struct child_parent_hierarchy *parent = &ett_h[j];

			if (child->parent_ptr == parent->child_ptr)
			{
				child->parent_ett = parent->child_ett;
				break;
			}
		}
		assert(child->child_ett.handle != child->parent_ett.handle);
	}

	for (u32 i = 0; i < array_len(ett_h); ++i)
	{
		struct child_parent_hierarchy *child = &ett_h[i];

		transform_component *transform = scene_component_get_data(scene, child->child_ett, TRANSFORM);
		{
			glm_vec3_copy(r_scene->nodes[i].position.data, transform->position.v3.data);
			transform->position.w = 0.0f;
			glm_vec3_copy(r_scene->nodes[i].scale.data, transform->scale.data);
			glm_vec4_copy(r_scene->nodes[i].rotation.data, transform->rotation.data);

			transform->flags = TRANSFORM_FLAG_DIRTY;
		}

		world_component *world = scene_component_get_data(scene, child->child_ett, WORLD);
		{
			world->matrix = m4_identity();
			world->last_matrix = m4_identity();
		}

		hierarchy_component *hierarchy = scene_component_get_data(scene, child->child_ett, HIERARCHY);
		{
			hierarchy->parent = child->parent_ett;
		}

		if ((child->archetype & (MATERIAL | MESH)) != (MATERIAL | MESH)) { continue; }

		material_component *material = scene_component_get_data(scene, child->child_ett, MATERIAL);
		{
			if (r_scene->nodes[i].material.size > 0)
			{
				struct resource *material_resource = resource_get_by_name(r_scene->nodes[i].material);
				material->resource_ref = resource_make_reference(material_resource);
				material->material_ref = material->resource_ref->material_data;
			}
		}

		mesh_component *mesh = scene_component_get_data(scene, child->child_ett, MESH);
		{
			assert(r_scene->nodes[i].mesh.size > 0);
			struct resource *mesh_resource = resource_get_by_name(r_scene->nodes[i].mesh);
			mesh->resource_ref = resource_make_reference(mesh_resource);
			mesh->mesh_ref = mesh->resource_ref->mesh_data;
		}
		static_body_component *static_body = scene_component_get_data(scene, child->child_ett, STATIC_BODY);
		{
			static_body->enabled = true;
		}
	}

	array_release(arena, ett_h);
}

#if 0
entity_t
scene_load_animated(struct arena *arena, struct scene *scene, str8 name)
{
	entity_t result = (entity_t){INVALID_HANDLE};

	const struct scene_resource *r_scene = load_animated_model_gltf(name);

	for (u32 i = 0; i < array_len(r_scene->nodes); ++i)
	{
		assert(r_scene->nodes[i].armature_ref);
		assert(r_scene->nodes[i].material_ref);
		assert(r_scene->nodes[i].mesh_ref);
		assert(array_len(r_scene->nodes) == 1); // Only one for now. TODO

		component_t archetype =
		    TRANSFORM | WORLD | HIERARCHY | MESH | MATERIAL | ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER;
		entity_t ett = scene_entity_new(arena, scene, archetype);
		result = ett;

		transform_component *transform = scene_component_get_data(scene, ett, TRANSFORM);
		{
			glm_vec3_copy(r_scene->nodes[i].position.data, transform->position.v3.data);
			transform->position.w = 0.0f;
			glm_vec3_copy(r_scene->nodes[i].scale.data, transform->scale.data);
			glm_vec4_copy(r_scene->nodes[i].rotation.data, transform->rotation.data);

			transform->flags = TRANSFORM_FLAG_DIRTY;
		}
		world_component *world = scene_component_get_data(scene, ett, WORLD);
		{
			world->matrix = m4_identity();
			world->last_matrix = m4_identity();
		}
		hierarchy_component *hierarchy = scene_component_get_data(scene, ett, HIERARCHY);
		{
			hierarchy->parent = (entity_t){INVALID_HANDLE};
		}

		material_component *material = scene_component_get_data(scene, ett, MATERIAL);
		{
			material->material_ref = 0;
			material->material_ref = resource_material_make_reference(r_scene->nodes[i].material_ref);
		}

		mesh_component *mesh = scene_component_get_data(scene, ett, MESH);
		{
			mesh->mesh_ref = 0;
			mesh->mesh_ref = resource_mesh_make_reference(r_scene->nodes[i].mesh_ref);
		}

		armature_component *armature = scene_component_get_data(scene, ett, ARMATURE);
		{
			armature->armature_ref = 0;
			armature->armature_ref = resource_armature_make_reference(r_scene->nodes[i].armature_ref);
		}

		pose_component *current = scene_component_get_data(scene, ett, POSE);
		{
			current->parents = 0;
			current->joints = 0;
			struct pose *rest = &armature->armature_ref->rest;
			pose_copy(arena, current, rest);
		}

		clip_component *clip = scene_component_get_data(scene, ett, CLIP);
		{
			clip->next_clip_ref = 0;
			clip->current_clip_ref = 0;
			clip->time = 0.0f;
		}

		cross_fade_controller_component *cfc = scene_component_get_data(scene, ett, CROSS_FADE_CONTROLLER);
		{
			cfc->targets = 0;
		}
	}

	return (result);
}
#endif

entity_t
scene_set_main_camera(struct scene *scene, entity_t camera_entity)
{
	assert(scene_entity_is_valid(scene, camera_entity));

	entity_t old_camera = scene->main_camera;
	scene->main_camera = camera_entity;

	return (old_camera);
}

entity_t
scene_get_main_camera(struct scene *scene)
{
	entity_t result;

	assert(scene_entity_is_valid(scene, scene->main_camera));

	result = scene->main_camera;

	return (result);
}

v3
scene_set_gravity_force(struct scene *scene, v3 gravity)
{
	v3 old_gravity = scene->gravity_force;
	scene->gravity_force = gravity;

	return (old_gravity);
}

// void
// scene_copy(struct scene *dest, struct scene *src)
// {
// 	if (dest == src) { return; }
//
// 	handle_pool_copy(&dest->indirect_handle_pool, &src->indirect_handle_pool);
// 	array_copy(dest->indirect_access, src->indirect_access);
// 	array_copy(dest->sys_info, src->sys_info);
// 	array_copy(dest->component_handle_pool, src->component_handle_pool);
// }

entity_t
scene_entity_new(struct arena *arena, struct scene *scene, component_t archetype)
{
	entity_t result;

	for (u32 i = 0; i < array_len(scene->component_handle_pool); ++i)
	{
		if (scene->component_handle_pool[i].archetype == archetype)
		{
			handle_t handle = component_pool_handle_new(arena, &scene->component_handle_pool[i]);
			// result.handle = handle_new(arena, &scene->indirect_handle_pool);
			result.handle = sm__scene_indirect_access_new_handle(arena, scene);

			u32 index = handle_index(result.handle);
			scene->indirect_access[index].handle = handle;
			scene->indirect_access[index].component_pool_index = i;
			scene->indirect_access[index].archetype = archetype;

			return (result);
		}
	}

	struct component_pool comp_pool = {0};
	array_push(arena, scene->component_handle_pool, comp_pool);

	u32 component_index = array_len(scene->component_handle_pool) - 1;
	component_pool_make(arena, &scene->component_handle_pool[component_index], 8, archetype);

	handle_t component_handle = component_pool_handle_new(arena, &scene->component_handle_pool[component_index]);

	// result.handle = handle_new(arena, &scene->indirect_handle_pool);
	result.handle = sm__scene_indirect_access_new_handle(arena, scene);
	u32 index = handle_index(result.handle);

	scene->indirect_access[index].handle = component_handle;
	scene->indirect_access[index].component_pool_index = component_index;
	scene->indirect_access[index].archetype = archetype;

	return (result);
}

void
scene_entity_remove(struct scene *scene, entity_t entity)
{
	assert(handle_valid(&scene->indirect_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	assert(index < scene->indirect_handle_pool.cap);

	handle_t ett = scene->indirect_access[index].handle;
	u32 comp_pool_index = scene->indirect_access[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	component_pool_handle_remove(comp_pool, ett);

	handle_remove(&scene->indirect_handle_pool, entity.handle);
}

b8
scene_entity_is_valid(struct scene *scene, entity_t entity)
{
	b8 result;

	result = handle_valid(&scene->indirect_handle_pool, entity.handle);
	if (!result) { return (result); }

	u32 index = handle_index(entity.handle);
	assert(index < scene->indirect_handle_pool.cap);

	handle_t ett = scene->indirect_access[index].handle;
	u32 comp_pool_index = scene->indirect_access[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	result &= component_pool_handle_is_valid(comp_pool, ett);

	return (result);
}

b8
scene_entity_has_components(struct scene *scene, entity_t entity, component_t components)
{
	b8 result;

	assert(handle_valid(&scene->indirect_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	assert(index < scene->indirect_handle_pool.cap);

	component_t archetype = scene->indirect_access[index].archetype;

	result = (archetype & components) == components;

	return (result);
}

void
scene_entity_add_component(struct arena *arena, struct scene *scene, entity_t entity, component_t components)
{
	assert(handle_valid(&scene->indirect_handle_pool, entity.handle));

	const u32 indirect_index = handle_index(entity.handle);
	;
	const component_t old_archetype = scene->indirect_access[indirect_index].archetype;
	;
	const handle_t old_handle = scene->indirect_access[indirect_index].handle;
	const u32 old_component_pool_index = scene->indirect_access[indirect_index].component_pool_index;

	if (old_archetype & components)
	{
		log_warn(str8_from("entity {u3d} already has {u6d} component"), entity.handle, components);
		return;
	}

	component_t new_archetype = old_archetype | components;
	handle_t new_handle = INVALID_HANDLE;
	for (u32 i = 0; i < array_len(scene->component_handle_pool); ++i)
	{
		if (scene->component_handle_pool[i].archetype == new_archetype)
		{
			new_handle = component_pool_handle_new(arena, &scene->component_handle_pool[i]);

			scene->indirect_access[indirect_index].handle = new_handle;
			scene->indirect_access[indirect_index].component_pool_index = i;
			scene->indirect_access[indirect_index].archetype = new_archetype;

			break;
		}
	}

	if (new_handle == INVALID_HANDLE)
	{
		struct component_pool comp_pool = {0};
		array_push(arena, scene->component_handle_pool, comp_pool);

		u32 component_index = array_len(scene->component_handle_pool) - 1;
		component_pool_make(arena, &scene->component_handle_pool[component_index], 8, new_archetype);

		new_handle = component_pool_handle_new(arena, &scene->component_handle_pool[component_index]);

		scene->indirect_access[indirect_index].handle = new_handle;
		scene->indirect_access[indirect_index].component_pool_index = component_index;
		scene->indirect_access[indirect_index].archetype = new_archetype;
	}
	assert(new_handle != INVALID_HANDLE);

	struct component_pool *old_comp_pool = &scene->component_handle_pool[old_component_pool_index];
	struct component_pool *new_comp_pool =
	    &scene->component_handle_pool[scene->indirect_access[indirect_index].component_pool_index];

	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t old_cmp = old_archetype & i;
		component_t new_cmp = new_archetype & i;
		if (new_cmp & old_cmp)
		{
			u32 cmp_index = fast_log2_64(new_cmp);
			assert(cmp_index < 64);
			struct component_view *old_view = &old_comp_pool->view[cmp_index];
			struct component_view *new_view = &new_comp_pool->view[cmp_index];
			assert(old_view->id == new_view->id);

			u32 new_index = handle_index(new_handle);
			assert(new_index < new_comp_pool->handle_pool.cap);
			void *dest = (u8 *)new_comp_pool->data + (new_index * new_comp_pool->size) + new_view->offset;

			u32 old_index = handle_index(old_handle);
			assert(old_index < old_comp_pool->handle_pool.cap);
			void *src = (u8 *)old_comp_pool->data + (old_index * old_comp_pool->size) + old_view->offset;

			memcpy(dest, src, new_view->size);
		}
	}

	component_pool_handle_remove(old_comp_pool, old_handle);
}

void *
scene_component_get_data(struct scene *scene, entity_t entity, component_t component)
{
	void *result;
	assert(handle_valid(&scene->indirect_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	assert(index < scene->indirect_handle_pool.cap);
	assert(scene->indirect_access[index].archetype & component);

	handle_t ett = scene->indirect_access[index].handle;
	u32 comp_pool_index = scene->indirect_access[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	result = component_pool_get_data(comp_pool, ett, component);

	return (result);
}

void
scene_system_register(struct arena *arena, struct scene *scene, str8 name, system_f system, void *user_data)
{
	assert(system);

	struct system_info sys_info = {
	    .name = name.size ? name : str8_from("unnamed"),

	    .system = system,
	    .user_data = user_data,
	};

	array_push(arena, scene->sys_info, sys_info);
}

struct scene_iter
scene_iter_begin(struct scene *scene, component_t constraint)
{
	struct scene_iter result;

	result.constraint = constraint;
	result.index = 0;
	result.comp_pool_index = 0;
	result.comp_pool_ref = 0;
	result.first_iter = true;

	for (u32 i = result.comp_pool_index; i < array_len(scene->component_handle_pool); ++i)
	{
		component_t archetype = scene->component_handle_pool[i].archetype;
		if ((result.constraint & archetype) == result.constraint)
		{
			result.comp_pool_index = i;
			result.comp_pool_ref = &scene->component_handle_pool[i];
			break;
		}
	}

	return (result);
}

b8
scene_iter_next(struct scene *scene, struct scene_iter *iter)
{
	if (!iter->comp_pool_ref) { return (false); }

	if (!iter->first_iter) { ++iter->index; }
	else
	{
		assert(iter->index == 0);
		iter->first_iter = false;
	}

	while (iter->index < iter->comp_pool_ref->handle_pool.len)
	{
		handle_t handle = handle_at(&iter->comp_pool_ref->handle_pool, iter->index);
		if (handle_valid(&iter->comp_pool_ref->handle_pool, handle)) { return (true); }
		else { iter->index++; }
	}

	iter->comp_pool_index++;

	for (u32 i = iter->comp_pool_index; i < array_len(scene->component_handle_pool); ++i)
	{
		const struct component_pool *cpool = &scene->component_handle_pool[i];
		component_t archetype = cpool->archetype;
		if ((archetype & iter->constraint) == iter->constraint)
		{
			if (cpool->handle_pool.len > 0)
			{
				iter->index = 0;
				iter->comp_pool_ref = cpool;
				iter->comp_pool_index = i;

				return (true);
			}
		}
	}

	return (false);
}

void *
scene_iter_get_component(struct scene_iter *iter, component_t component)
{
	void *result = 0;

	assert((iter->constraint & component) == component);

	u32 comp_index = fast_log2_64(component);
	assert(comp_index < 64);

	const struct component_view *v = &iter->comp_pool_ref->view[comp_index];
	assert(component == v->id);

	// If the view has the specified component, return a pointer to its data
	result = (u8 *)iter->comp_pool_ref->data + (iter->index * iter->comp_pool_ref->size) + v->offset;

	return (result);
}

void
scene_system_run(struct arena *arena, struct scene *scene, struct ctx *ctx)
{
	// log_debug(str8_from("System running:"));
	for (u32 i = 0; i < array_len(scene->sys_info); ++i)
	{
		system_f system = scene->sys_info[i].system;
		void *user_data = scene->sys_info[i].user_data;
		// str8 name = scene->sys_info[i].name;
		// log_debug(str8_from("    - {s}"), name);

		if (!system(arena, scene, ctx, user_data)) { break; }
	}
}

void
scene_print_archeype(struct arena *arena, struct scene *scene, entity_t entity)
{
	assert(handle_valid(&scene->indirect_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	assert(index < scene->indirect_handle_pool.cap);

	component_t archetype = scene->indirect_access[index].archetype;
	b8 first = true;

	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t component = archetype & i;
		if (component)
		{
			u32 component_index = fast_log2_64(component);
			if (first)
			{
				str8_printf(arena, str8_from("({s}"), ctable_components[component_index].name);
				first = false;
			}
			else { str8_printf(arena, str8_from("|{s}"), ctable_components[component_index].name); }
		}
	}
	str8_println(str8_from(")"));
}
