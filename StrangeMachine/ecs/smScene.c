#include "ecs/smScene.h"
#include "core/smArray.h"
#include "core/smLog.h"
#include "core/smResource.h"
#include "ecs/smECS.h"
#include "renderer/smRenderer.h"

void
scene_make(struct arena *arena, struct scene *scene)
{
	handle_pool_make(arena, &scene->nodes_handle_pool, 8);
	// array_set_cap(arena, scene->indirect_access, scene->indirect_handle_pool.cap);
	scene->nodes = arena_reserve(arena, sizeof(struct node) * scene->nodes_handle_pool.cap);
	scene->arena = arena;
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

	handle_pool_release(arena, &scene->nodes_handle_pool);
	// array_release(arena, scene->indirect_access);
	arena_free(arena, scene->nodes);
}

static handle_t
sm__scene_indirect_access_new_handle(struct arena *arena, struct scene *scene)
{
	handle_t result;

	result = handle_new(arena, &scene->nodes_handle_pool);
	if (scene->nodes_cap != scene->nodes_handle_pool.cap)
	{
		scene->nodes = arena_resize(arena, scene->nodes, sizeof(struct node) * scene->nodes_handle_pool.cap);
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

entity_t
scene_load(struct arena *arena, struct scene *scene, str8 name)
{
	entity_t result = {0};
	struct resource *res = resource_get_by_name(name);
	if (res == 0)
	{
		log_error(str8_from("[{s}] scene not found"), name);
		return result;
	}
	resource_make_reference(res);

	const struct scene_resource *scene_res = res->scene_data;

	struct child_parent_hierarchy
	{
		component_t archetype;
		struct scene_node *ptr, *parent_ptr;
		entity_t ett, parent_ett;
	};

	struct child_parent_hierarchy *nodes_hierarchy = 0;
	array_set_len(arena, nodes_hierarchy, array_len(scene_res->nodes));
	for (u32 i = 0; i < array_len(scene_res->nodes); ++i)
	{
		struct scene_node *node = &scene_res->nodes[i];

		component_t archetype = TRANSFORM;
		if (node->mesh.size > 0) { archetype |= MESH | MATERIAL; }
		if (node->armature.size > 0) { archetype |= ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER; }
		if (node->prop & NODE_PROP_STATIC_BODY) { archetype |= STATIC_BODY; }
		if (node->prop & NODE_PROP_RIGID_BODY) { archetype |= RIGID_BODY; }
		if (node->prop & NODE_PROP_PLAYER) { archetype |= PLAYER; }

		entity_t ett = scene_entity_new(arena, scene, archetype);
		if (archetype & ARMATURE) { result = ett; }

		nodes_hierarchy[i].archetype = archetype;

		nodes_hierarchy[i].ptr = node;
		nodes_hierarchy[i].parent_ptr = (node->parent_index > -1) ? &scene_res->nodes[node->parent_index] : 0;

		nodes_hierarchy[i].ett = ett;
		nodes_hierarchy[i].parent_ett = (entity_t){INVALID_HANDLE};
		// ett_h[i].transform = scene_component_get_data(scene, ett, TRANSFORM);
	}

	for (u32 i = 0; i < array_len(nodes_hierarchy); ++i)
	{
		struct child_parent_hierarchy *self = &nodes_hierarchy[i];
		if (self->parent_ptr == 0) { continue; }

		for (u32 j = 0; j < array_len(nodes_hierarchy); ++j)
		{
			if (i == j) { continue; }
			struct child_parent_hierarchy *parent = &nodes_hierarchy[j];

			if (self->parent_ptr == parent->ptr)
			{
				self->parent_ett = parent->ett;

				scene_entity_set_parent(scene, self->ett, parent->ett);
				break;
			}
		}
		sm__assert(self->ett.handle != self->parent_ett.handle);
	}

	for (u32 i = 0; i < array_len(nodes_hierarchy); ++i)
	{
		struct child_parent_hierarchy *self = &nodes_hierarchy[i];

		transform_component *transform = scene_component_get_data(scene, self->ett, TRANSFORM);
		{
			transform->matrix_local = m4_identity();

			transform->matrix = m4_identity();
			transform->last_matrix = m4_identity();

			glm_vec3_copy(scene_res->nodes[i].position.data, transform->transform_local.translation.data);
			glm_vec4_copy(scene_res->nodes[i].rotation.data, transform->transform_local.rotation.data);

			v3 scale;
			scale.x = (scene_res->nodes[i].scale.x == 0.0f) ? GLM_FLT_EPSILON : scene_res->nodes[i].scale.x;
			scale.y = (scene_res->nodes[i].scale.y == 0.0f) ? GLM_FLT_EPSILON : scene_res->nodes[i].scale.y;
			scale.z = (scene_res->nodes[i].scale.z == 0.0f) ? GLM_FLT_EPSILON : scene_res->nodes[i].scale.z;

			glm_vec3_copy(scale.data, transform->transform_local.scale.data);

			u32 self_index = handle_index(self->ett.handle);
			struct node *self_node = &scene->nodes[self_index];
			self_node->flags |= HIERARCHY_FLAG_DIRTY;
			// transform->flags = TRANSFORM_FLAG_DIRTY;
		}

		if ((self->archetype & (MATERIAL | MESH)) != (MATERIAL | MESH)) { continue; }

		material_component *material = scene_component_get_data(scene, self->ett, MATERIAL);
		{
			struct resource *material_res = 0;

			if (scene_res->nodes[i].material.size > 0)
			{
				material_res = resource_get_by_name(scene_res->nodes[i].material);

				if (material_res->material_data->image.size > 0)
				{
					renderer_texture_add(material_res->material_data->image);
				}
			}
			else { material_res = resource_get_default_material(); }

			material->resource_ref = resource_make_reference(material_res);
			material->material_ref = material->resource_ref->material_data;
		}

		mesh_component *mesh = scene_component_get_data(scene, self->ett, MESH);
		{
			sm__assert(scene_res->nodes[i].mesh.size > 0);
			struct resource *mesh_res = resource_get_by_name(scene_res->nodes[i].mesh);
			mesh->resource_ref = resource_make_reference(mesh_res);
			mesh->mesh_ref = mesh->resource_ref->mesh_data;
		}
		if ((self->archetype & STATIC_BODY))
		{
			static_body_component *static_body = scene_component_get_data(scene, self->ett, STATIC_BODY);
			static_body->enabled = true;
		}

		if ((self->archetype & (ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER)) !=
		    (ARMATURE | CLIP | POSE | CROSS_FADE_CONTROLLER))
		{
			continue;
		}

		armature_component *armature = scene_component_get_data(scene, self->ett, ARMATURE);
		{
			struct resource *armature_res = resource_get_by_name(scene_res->nodes[i].armature);
			armature->resource_ref = resource_make_reference(armature_res);
			armature->armature_ref = armature->resource_ref->armature_data;
		}

		pose_component *current = scene_component_get_data(scene, self->ett, POSE);
		{
			current->parents = 0;
			current->joints = 0;
			struct pose *rest = &armature->armature_ref->rest;
			pose_copy(arena, current, rest);
		}

		clip_component *clip = scene_component_get_data(scene, self->ett, CLIP);
		{
			clip->next_clip_ref = 0;
			clip->current_clip_ref = 0;
			clip->time = 0.0f;
		}

		cross_fade_controller_component *cfc =
		    scene_component_get_data(scene, self->ett, CROSS_FADE_CONTROLLER);
		{
			cfc->targets = 0;
		}
	}

	for (u32 i = 0; i < array_len(nodes_hierarchy); ++i)
	{
		struct child_parent_hierarchy *self = &nodes_hierarchy[i];
		scene_entity_update_hierarchy(scene, self->ett);

		u32 self_index = handle_index(self->ett.handle);
		struct node *self_node = &scene->nodes[self_index];
		self_node->flags &= ~(u32)HIERARCHY_FLAG_DIRTY;
	}

	array_release(arena, nodes_hierarchy);

	return result;
}

entity_t
scene_set_main_camera(struct scene *scene, entity_t camera_entity)
{
	sm__assert(scene_entity_is_valid(scene, camera_entity));

	entity_t old_camera = scene->main_camera;
	scene->main_camera = camera_entity;

	return (old_camera);
}

entity_t
scene_get_main_camera(struct scene *scene)
{
	entity_t result;

	sm__assert(scene_entity_is_valid(scene, scene->main_camera));

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

entity_t
scene_entity_new(struct arena *arena, struct scene *scene, component_t archetype)
{
	entity_t result;

	for (u32 i = 0; i < array_len(scene->component_handle_pool); ++i)
	{
		if (scene->component_handle_pool[i].archetype == archetype)
		{
			handle_t component_handle = component_pool_handle_new(arena, &scene->component_handle_pool[i]);
			// result.handle = handle_new(arena, &scene->indirect_handle_pool);
			result.handle = sm__scene_indirect_access_new_handle(arena, scene);

			u32 index = handle_index(result.handle);
			scene->nodes[index].handle = component_handle;
			scene->nodes[index].component_pool_index = i;
			scene->nodes[index].archetype = archetype;

			scene->nodes[index].self = result;
			scene->nodes[index].parent.handle = INVALID_HANDLE;
			scene->nodes[index].children = 0;
			scene->nodes[index].flags = 0;

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

	scene->nodes[index].handle = component_handle;
	scene->nodes[index].component_pool_index = component_index;
	scene->nodes[index].archetype = archetype;

	scene->nodes[index].self = result;
	scene->nodes[index].parent.handle = INVALID_HANDLE;
	scene->nodes[index].children = 0;
	scene->nodes[index].flags = 0;

	return (result);
}

void
scene_entity_remove(struct scene *scene, entity_t entity)
{
	sm__assert(handle_valid(&scene->nodes_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	sm__assert(index < scene->nodes_handle_pool.cap);

	handle_t ett = scene->nodes[index].handle;
	u32 comp_pool_index = scene->nodes[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	component_pool_handle_remove(comp_pool, ett);

	handle_remove(&scene->nodes_handle_pool, entity.handle);
}

b8
scene_entity_is_valid(struct scene *scene, entity_t entity)
{
	b8 result;

	result = handle_valid(&scene->nodes_handle_pool, entity.handle);
	if (!result) { return (result); }

	u32 index = handle_index(entity.handle);
	sm__assert(index < scene->nodes_handle_pool.cap);

	handle_t ett = scene->nodes[index].handle;
	u32 comp_pool_index = scene->nodes[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	result &= component_pool_handle_is_valid(comp_pool, ett);

	return (result);
}

b8
scene_entity_has_components(struct scene *scene, entity_t entity, component_t components)
{
	b8 result;

	sm__assert(handle_valid(&scene->nodes_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	sm__assert(index < scene->nodes_handle_pool.cap);

	component_t archetype = scene->nodes[index].archetype;

	result = (archetype & components) == components;

	return (result);
}

void
scene_entity_add_component(struct arena *arena, struct scene *scene, entity_t entity, component_t components)
{
	sm__assert(handle_valid(&scene->nodes_handle_pool, entity.handle));

	const u32 indirect_index = handle_index(entity.handle);
	const component_t old_archetype = scene->nodes[indirect_index].archetype;

	const handle_t old_handle = scene->nodes[indirect_index].handle;
	const u32 old_component_pool_index = scene->nodes[indirect_index].component_pool_index;

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

			scene->nodes[indirect_index].handle = new_handle;
			scene->nodes[indirect_index].component_pool_index = i;
			scene->nodes[indirect_index].archetype = new_archetype;

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

		scene->nodes[indirect_index].handle = new_handle;
		scene->nodes[indirect_index].component_pool_index = component_index;
		scene->nodes[indirect_index].archetype = new_archetype;
	}
	sm__assert(new_handle != INVALID_HANDLE);

	u32 new_index = handle_index(new_handle);
	u32 old_index = handle_index(old_handle);
	struct component_pool *old_comp_pool = &scene->component_handle_pool[old_component_pool_index];
	struct component_pool *new_comp_pool =
	    &scene->component_handle_pool[scene->nodes[indirect_index].component_pool_index];

	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t old_cmp = old_archetype & i;
		component_t new_cmp = new_archetype & i;
		if (new_cmp & old_cmp)
		{
			u32 cmp_index = fast_log2_64(new_cmp);
			sm__assert(cmp_index < 64);
			struct component_view *old_view = &old_comp_pool->view[cmp_index];
			struct component_view *new_view = &new_comp_pool->view[cmp_index];
			sm__assert(old_view->id == new_view->id);

			sm__assert(new_index < new_comp_pool->handle_pool.cap);
			void *dest = (u8 *)new_comp_pool->data + (new_index * new_comp_pool->size) + new_view->offset;

			sm__assert(old_index < old_comp_pool->handle_pool.cap);
			void *src = (u8 *)old_comp_pool->data + (old_index * old_comp_pool->size) + old_view->offset;

			memcpy(dest, src, new_view->size);
		}
	}

	handle_remove(&old_comp_pool->handle_pool, old_handle);
	memset((u8 *)old_comp_pool->data + (old_index * old_comp_pool->size), 0x0, old_comp_pool->size);
}

void *
scene_component_get_data(struct scene *scene, entity_t entity, component_t component)
{
	void *result;
	sm__assert(handle_valid(&scene->nodes_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	sm__assert(index < scene->nodes_handle_pool.cap);
	sm__assert(scene->nodes[index].archetype & component);

	handle_t ett = scene->nodes[index].handle;
	u32 comp_pool_index = scene->nodes[index].component_pool_index;

	struct component_pool *comp_pool = &scene->component_handle_pool[comp_pool_index];

	result = component_pool_get_data(comp_pool, ett, component);

	return (result);
}

void
scene_entity_set_dirty(struct scene *scene, entity_t entity, b8 dirty)
{
	u32 entity_index = handle_index(entity.handle);
	struct node *entity_node = &scene->nodes[entity_index];

	if (dirty) { entity_node->flags |= (u32)HIERARCHY_FLAG_DIRTY; }
	else { entity_node->flags &= ~(u32)HIERARCHY_FLAG_DIRTY; }
}

b8
scene_entity_is_dirty(struct scene *scene, entity_t entity)
{
	u32 entity_index = handle_index(entity.handle);
	struct node *entity_node = &scene->nodes[entity_index];

	return (entity_node->flags & HIERARCHY_FLAG_DIRTY);
}

void
scene_entity_update_hierarchy(struct scene *scene, entity_t self)
{
	sm__assert(scene_entity_is_valid(scene, self));
	sm__assert(scene_entity_has_components(scene, self, TRANSFORM));
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);

	// Compute local transform
	self_transform->matrix_local = trs_to_m4(self_transform->transform_local);

	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];

	// Compute world transform
	if (self_node->parent.handle)
	{
		sm__assert(scene_entity_is_valid(scene, self_node->parent));
		transform_component *parent_transform = scene_component_get_data(scene, self_node->parent, TRANSFORM);

		glm_mat4_mul(
		    parent_transform->matrix.data, self_transform->matrix_local.data, self_transform->matrix.data);
	}
	else { glm_mat4_copy(self_transform->matrix_local.data, self_transform->matrix.data); }

	for (u32 i = 0; i < array_len(self_node->children); ++i)
	{
		scene_entity_update_hierarchy(scene, self_node->children[i]);
	}
}

b8
scene_entity_is_descendant_of(struct scene *scene, entity_t self, entity_t entity)
{
	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];

	if (self_node->parent.handle != INVALID_HANDLE) { return (false); }
	if (self_node->parent.handle == entity.handle) { return (true); }

	u32 entity_index = handle_index(entity.handle);
	struct node *entity_node = &scene->nodes[entity_index];

	for (u32 i = 0; i < array_len(entity_node->children); ++i)
	{
		if (scene_entity_is_descendant_of(scene, self, entity_node->children[i])) { return (true); }
	}

	return (false);
}

void
scene_entity_set_parent(struct scene *scene, entity_t self, entity_t new_parent)
{
	sm__assert(scene_entity_is_valid(scene, self));
	sm__assert(scene_entity_is_valid(scene, new_parent));

	sm__assert(scene_entity_has_components(scene, self, TRANSFORM));
	sm__assert(scene_entity_has_components(scene, new_parent, TRANSFORM));

	if (self.handle == new_parent.handle)
	{
		log_warn(str8_from("adding self as parent"));
		return;
	}

	u32 self_index = handle_index(self.handle);
	sm__assert(self_index < scene->nodes_handle_pool.cap);
	sm__assert(scene->nodes[self_index].archetype & TRANSFORM);
	struct node *self_node = &scene->nodes[self_index];

	if (self_node->parent.handle == new_parent.handle) { return; }

	if (new_parent.handle && scene_entity_is_descendant_of(scene, new_parent, self))
	{
		for (u32 i = 0; i < array_len(self_node->children); ++i)
		{
			// self->chidren_transform[i]->parent_transform = self->parent_transform;
			entity_t motherless = self_node->children[i];

			u32 motherless_index = handle_index(motherless.handle);
			sm__assert(motherless_index < scene->nodes_handle_pool.cap);
			sm__assert(scene->nodes[motherless_index].archetype & TRANSFORM);
			struct node *motherless_node = &scene->nodes[motherless_index];
			motherless_node->parent = self_node->parent;
		}

		array_set_len(scene->arena, self_node->children, 0);
	}

	if (self_node->parent.handle)
	{
		entity_t parent_entity = self_node->parent;

		u32 parent_index = handle_index(parent_entity.handle);
		sm__assert(parent_index < scene->nodes_handle_pool.cap);
		sm__assert(scene->nodes[parent_index].archetype & TRANSFORM);
		struct node *parent_node = &scene->nodes[parent_index];

		i32 new_self_index = -1;
		for (u32 i = 0; i < array_len(parent_node->children); ++i)
		{
			if (self_node->self.handle == parent_node->children[i].handle)
			{
				new_self_index = i;
				break;
			}
		}
		sm__assert(new_self_index != -1);

		array_del(parent_node->children, new_self_index, 1);
	}

	// Push self as child of the new parent
	if (new_parent.handle)
	{
		b8 is_child = false;

		u32 new_parent_index = handle_index(new_parent.handle);
		struct node *new_parent_node = &scene->nodes[new_parent_index];

		for (u32 i = 0; i < array_len(new_parent_node->children); ++i)
		{
			if (new_parent_node->children[i].handle == self_node->self.handle)
			{
				is_child = true;
				break;
			}
		}
		if (!is_child) { array_push(scene->arena, new_parent_node->children, self); }
		new_parent_node->flags |= HIERARCHY_FLAG_DIRTY;
	}

	// Set the new parent in self
	self_node->parent = new_parent;
	self_node->flags |= HIERARCHY_FLAG_DIRTY;
}

void
scene_entity_add_child(struct scene *scene, entity_t self, entity_t child)
{
	scene_entity_set_parent(scene, child, self);
}

void
scene_entity_set_position_local(struct scene *scene, entity_t self, v3 position)
{
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);

	if (glm_vec3_eqv(self_transform->transform_local.translation.data, position.data)) { return; }

	glm_vec3_copy(position.data, self_transform->transform_local.translation.data);

	scene_entity_update_hierarchy(scene, self);
}

void
scene_entity_set_position(struct scene *scene, entity_t self, v3 position)
{
	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];

	if (self_node->parent.handle == INVALID_HANDLE) { scene_entity_set_position_local(scene, self, position); }
	else
	{
		transform_component *parent_transform = scene_component_get_data(scene, self_node->parent, TRANSFORM);
		m4 inv;

		glm_mat4_inv(parent_transform->matrix.data, inv.data);
		position = m4_v3(inv, position);

		scene_entity_set_position_local(scene, self, position);
	}
}

void
scene_entity_set_rotation_local(struct scene *scene, entity_t self, v4 rotation)
{
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);
	if (glm_vec4_eqv(self_transform->transform_local.rotation.data, rotation.data)) { return; }

	glm_vec4_copy(rotation.data, self_transform->transform_local.rotation.data);

	scene_entity_update_hierarchy(scene, self);
}

void
scene_entity_set_rotation(struct scene *scene, entity_t self, v4 rotation)
{
	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];

	if (self_node->parent.handle == INVALID_HANDLE) { scene_entity_set_rotation_local(scene, self, rotation); }
	else
	{
		v4 inv;
		transform_component *parent_transform = scene_component_get_data(scene, self_node->parent, TRANSFORM);
		glm_mat4_quat(parent_transform->matrix.data, inv.data);
		glm_quat_inv(inv.data, inv.data);

		glm_quat_mul(rotation.data, inv.data, rotation.data);

		scene_entity_set_rotation_local(scene, self, rotation);
	}
}

void
scene_entity_set_scale_local(struct scene *scene, entity_t self, v3 scale)
{
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);
	if (glm_vec3_eqv(self_transform->transform_local.scale.data, scale.data)) { return; }

	scale.x = (scale.x == 0.0f) ? GLM_FLT_EPSILON : scale.x;
	scale.y = (scale.y == 0.0f) ? GLM_FLT_EPSILON : scale.y;
	scale.z = (scale.z == 0.0f) ? GLM_FLT_EPSILON : scale.z;

	glm_vec3_copy(scale.data, self_transform->transform_local.scale.data);

	scene_entity_update_hierarchy(scene, self);
}

void
scene_entity_translate(struct scene *scene, entity_t self, v3 delta)
{
	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);

	if (self_node->parent.handle == INVALID_HANDLE)
	{
		glm_vec3_add(self_transform->transform_local.translation.data, delta.data,
		    self_transform->transform_local.translation.data);

		scene_entity_update_hierarchy(scene, self);
	}
	else
	{
		transform_component *parent_transform = scene_component_get_data(scene, self_node->parent, TRANSFORM);
		m4 inv;
		glm_mat4_inv(parent_transform->matrix.data, inv.data);
		delta = m4_v3(inv, delta);
		glm_vec3_add(self_transform->transform_local.translation.data, delta.data,
		    self_transform->transform_local.translation.data);

		scene_entity_update_hierarchy(scene, self);
	}
}

void
scene_entity_rotate(struct scene *scene, entity_t self, v4 delta)
{
	u32 self_index = handle_index(self.handle);
	struct node *self_node = &scene->nodes[self_index];
	transform_component *self_transform = scene_component_get_data(scene, self, TRANSFORM);

	if (self_node->parent.handle == INVALID_HANDLE)
	{
		glm_quat_mul(self_transform->transform_local.rotation.data, delta.data,
		    self_transform->transform_local.rotation.data);
		glm_quat_normalize(self_transform->transform_local.rotation.data);

		scene_entity_update_hierarchy(scene, self);
	}
	else
	{
		// TODO: investigate this
		v4 inv, q;
		m4 rotation_matrix;
		v3 discard;

		glm_decompose_rs(self_transform->matrix.data, rotation_matrix.data, discard.data);
		glm_mat4_quat(rotation_matrix.data, q.data);

		glm_quat_inv(q.data, inv.data);
		glm_quat_mul(self_transform->transform_local.rotation.data, inv.data, inv.data);
		glm_quat_mul(inv.data, delta.data, delta.data);
		glm_quat_mul(delta.data, q.data, q.data);

		scene_entity_set_rotation_local(scene, self, q);
	}
}

void
scene_system_register(struct arena *arena, struct scene *scene, str8 name, system_f system, void *user_data)
{
	sm__assert(system);

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
	result.scene_ref = scene;

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
		sm__assert(iter->index == 0);
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

	sm__assert((iter->constraint & component) == component);

	u32 comp_index = fast_log2_64(component);
	sm__assert(comp_index < 64);

	const struct component_view *v = &iter->comp_pool_ref->view[comp_index];
	sm__assert(component == v->id);

	// If the view has the specified component, return a pointer to its data
	result = (u8 *)iter->comp_pool_ref->data + (iter->index * iter->comp_pool_ref->size) + v->offset;

	return (result);
}

entity_t
scene_iter_get_entity(struct scene_iter *iter)
{
	entity_t result = {INVALID_HANDLE};

	struct node *nodes = iter->scene_ref->nodes;
	const struct scene *scene = iter->scene_ref;

	for (u32 i = 0; i < scene->nodes_handle_pool.len; ++i)
	{
		handle_t entity_handle = handle_at(&scene->nodes_handle_pool, i);
		u32 index = handle_index(entity_handle);
		if (nodes[index].archetype == iter->comp_pool_ref->archetype)
		{
			handle_t handle = handle_at(&iter->comp_pool_ref->handle_pool, iter->index);
			if (nodes[index].handle == handle)
			{
				result.handle = entity_handle;
				break;
			}
		}
	}

	sm__assert(result.handle != INVALID_HANDLE);

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
	sm__assert(handle_valid(&scene->nodes_handle_pool, entity.handle));

	u32 index = handle_index(entity.handle);
	sm__assert(index < scene->nodes_handle_pool.cap);

	component_t archetype = scene->nodes[index].archetype;
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
