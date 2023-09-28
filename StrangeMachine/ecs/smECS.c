#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smLog.h"
#include "ecs/smECS.h"

struct components_info ctable_components[64] = {
    {
	.name = str8_from("Transform"),
	.id = TRANSFORM,
	.size = sizeof(transform_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Material"),
	.id = MATERIAL,
	.size = sizeof(material_component),
	.has_ref_counter = true,
     },
    {
	.name = str8_from("Camera"),
	.id = CAMERA,
	.size = sizeof(camera_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Mesh"),
	.id = MESH,
	.size = sizeof(mesh_component),
	.has_ref_counter = true,
     },
    {
	.name = str8_from("Rigid Body"),
	.id = RIGID_BODY,
	.size = sizeof(rigid_body_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Static Body"),
	.id = STATIC_BODY,
	.size = sizeof(static_body_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Armature"),
	.id = ARMATURE,
	.size = sizeof(armature_component),
	.has_ref_counter = true,
     },
    {
	.name = str8_from("Pose"),
	.id = POSE,
	.size = sizeof(pose_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Clip"),
	.id = CLIP,
	.size = sizeof(clip_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Cross Fade Controller"),
	.id = CROSS_FADE_CONTROLLER,
	.size = sizeof(cross_fade_controller_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Player"),
	.id = PLAYER,
	.size = sizeof(player_component),
	.has_ref_counter = false,
     },
    {
	.name = str8_from("Particle Emitter"),
	.id = PARTICLE_EMITTER,
	.size = sizeof(particle_emitter_component),
	.has_ref_counter = false,
     },
};

static void
component_pool_generate_view(struct component_pool *comp_pool, component_t archetype)
{
	u32 size = 0;
	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t component = archetype & i;
		if (component)
		{
			size = (size + 0xFUL) & ~(0xFUL); // Align

			u32 index = fast_log2_64(component);
			comp_pool->view[index].size = ctable_components[index].size;
			comp_pool->view[index].offset = size;
			comp_pool->view[index].id = ctable_components[index].id;

			size += ctable_components[index].size;
		}
	}
	log_trace(str8_from("Bsize: {u3d}"), size);
	comp_pool->size = (size + 0xFUL) & ~(0xFUL);
	log_trace(str8_from("Asize: {u3d}"), comp_pool->size);
}

void
component_pool_make(struct arena *arena, struct component_pool *comp_pool, u32 capacity, component_t archetype)
{
	handle_pool_make(arena, &comp_pool->handle_pool, capacity);
	comp_pool->archetype = archetype;
	component_pool_generate_view(comp_pool, archetype);
	comp_pool->cap = capacity;
	ecs_manager_print_archeype(arena, archetype);
	comp_pool->data = arena_aligned(arena, 16, comp_pool->size * capacity);
	log_trace(str8_from("data ptr: 0x{u6x}"), comp_pool->data);
}

void
component_pool_release(struct arena *arena, struct component_pool *comp_pool)
{
	component_pool_unmake_refs(comp_pool);

	handle_pool_release(arena, &comp_pool->handle_pool);
	arena_free(arena, comp_pool->data);
}

static void
sm__component_pool_unmake_ref(struct component_pool *comp_pool, handle_t handle)
{
	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t component = comp_pool->archetype & i;
		if (!component_has_ref_counter(component)) { continue; }

		u32 index = handle_index(handle);

		u32 comp_index = fast_log2_64(component);
		struct component_view *v = &comp_pool->view[comp_index];

		void *data = comp_pool->data + (index * comp_pool->size) + v->offset;
		switch (component)
		{
		case MESH:
			{
				mesh_component *mesh = (mesh_component *)data;
				if (mesh->resource_ref) { resource_unmake_reference(mesh->resource_ref); }
			}
			break;
		case MATERIAL:
			{
				material_component *material = (material_component *)data;
				if (material->resource_ref) { resource_unmake_reference(material->resource_ref); }
			}
			break;
		case ARMATURE:
			{
				armature_component *armature = (armature_component *)data;
				if (armature->resource_ref) { resource_unmake_reference(armature->resource_ref); }
			}
			break;
		default:
			{
				log_error(str8_from("component {s} has no reference count"),
				    ctable_components[comp_index].name);
				break;
			}
		}
	}
}

void
component_pool_unmake_refs(struct component_pool *comp_pool)
{
	for (u32 i = 0; i < comp_pool->handle_pool.len; ++i)
	{
		handle_t handle = handle_at(&comp_pool->handle_pool, i);
		sm__component_pool_unmake_ref(comp_pool, handle);
	}
}

/**
 * Returns a pointer to the data of the specified component of the given entity.
 *
 * @param ecs_world Pointer to the ECS world.
 * @param entity Entity to get the component data from.
 * @param component Component to get the data from.
 * @return Pointer to the component data.
 */
void *
component_pool_get_data(struct component_pool *comp_pool, handle_t handle, component_t component)
{
	void *result;
	assert(comp_pool);

	// Check that the entity has the specified component
	assert((comp_pool->archetype & component));

	// Check that the handle is valid
	assert(handle_valid(&comp_pool->handle_pool, handle));

	// Get the index of the handle
	u32 index = handle_index(handle);
	assert(index < comp_pool->handle_pool.cap);

	u32 comp_index = fast_log2_64(component);
	assert(comp_index < 64);

	struct component_view *v = &comp_pool->view[comp_index];
	assert(component == v->id);

	// If the view has the specified component, return a pointer to its data
	result = (u8 *)comp_pool->data + (index * comp_pool->size) + v->offset;

	return (result);
}

b8
component_pool_handle_is_valid(struct component_pool *comp_pool, handle_t handle)
{
	b8 result;
	assert(comp_pool);

	result = handle_valid(&comp_pool->handle_pool, handle);
	return (result);
}

handle_t
component_pool_handle_new(struct arena *arena, struct component_pool *comp_pool)
{
	handle_t result = INVALID_HANDLE;

	result = handle_new(arena, &comp_pool->handle_pool);
	assert(result != INVALID_HANDLE);

	if (comp_pool->cap != comp_pool->handle_pool.cap)
	{
		// comp_pool->data = arena_resize(arena, comp_pool->data, comp_pool->handle_pool.cap * comp_pool->size);
		void *new_data = arena_aligned(arena, 16, comp_pool->handle_pool.cap * comp_pool->size);
		memcpy(new_data, comp_pool->data, comp_pool->cap * comp_pool->size);
		arena_free(arena, comp_pool->data);
		comp_pool->data = new_data;
		comp_pool->cap = comp_pool->handle_pool.cap;
	}

	return (result);
}

void
component_pool_handle_remove(struct component_pool *comp_pool, handle_t handle)
{
	assert(comp_pool);

	// Check that the handle is valid
	assert(handle_valid(&comp_pool->handle_pool, handle));

	sm__component_pool_unmake_ref(comp_pool, handle);

	handle_remove(&comp_pool->handle_pool, handle);

	u32 index = handle_index(handle);

	memset((u8 *)comp_pool->data + (index * comp_pool->size), 0x0, comp_pool->size);
}

b8
component_has_ref_counter(component_t component)
{
	b8 result;

	u32 component_index = fast_log2_64(component);
	result = ctable_components[component_index].has_ref_counter;

	return (result);
}

// const i8 *
// ecs_managr_get_archetype_string(component_t archetype)
// {
// 	static i8 buf[512];
// 	buf[0] = 0;
//
// 	b8 first = true;
//
// 	u32 size = 0;
// 	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
// 	{
// 		component_t component = archetype & i;
// 		if (component)
// 		{
// 			u32 component_index = fast_log2_64(component);
// 			if (first)
// 			{
// 				strcat(buf, "(");
// 				strcat(buf, ctable_components[component_index].name.idata);
// 				first = false;
// 			}
// 			else
// 			{
// 				strcat(buf, "|");
// 				strcat(buf, ctable_components[component_index].name.idata);
// 			}
// 		}
// 	}
// 	if (strlen(buf) != 0) { strcat(buf, ")"); }
//
// 	return buf;
// }
//

void
ecs_manager_print_archeype(struct arena *arena, component_t archetype)
{
	b8 first = true;

	struct str8_buf str_buf = str_buf_begin(arena);

	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		component_t component = archetype & i;
		if (component)
		{
			u32 component_index = fast_log2_64(component);
			if (first)
			{
				// str8_printf(str8_from("({s}"), ctable_components[component_index].name);
				str_buf_append_char(arena, &str_buf, '(');
				str_buf_append(arena, &str_buf, ctable_components[component_index].name);
				first = false;
			}
			else
			{
				str_buf_append_char(arena, &str_buf, '|');
				str_buf_append(arena, &str_buf, ctable_components[component_index].name);
			}
		}
	}
	str_buf_append_char(arena, &str_buf, ')');

	str8 output = str_buf_end(arena, str_buf);

	log_trace(output);

	arena_free(arena, output.data);
}

struct system_iter
system_iter_begin(struct component_pool *comp_pool)
{
	struct system_iter result;

	result.index = 0;
	result.comp_pool_ref = comp_pool;

	return (result);
}

b8
system_iter_next(struct system_iter *iter)
{
	while (iter->index < iter->comp_pool_ref->handle_pool.len)
	{
		handle_t handle = handle_at(&iter->comp_pool_ref->handle_pool, iter->index++);
		if (handle_valid(&iter->comp_pool_ref->handle_pool, handle)) { return (true); }
	}

	return (false);
}

void *
system_iter_get_component(struct system_iter *iter, component_t component)
{
	void *result;
	assert(iter->comp_pool_ref->archetype & component);

	u32 comp_index = fast_log2_64(component);
	assert(comp_index < 64);

	struct component_view *v = &iter->comp_pool_ref->view[comp_index];
	assert(component == v->id);

	assert(iter->index > 0);
	u32 index = iter->index - 1;

	// If the view has the specified component, return a pointer to its data
	result = (u8 *)iter->comp_pool_ref->data + (index * iter->comp_pool_ref->size) + v->offset;

	return (result);
}

void
transform_update_tree(transform_component *self)
{
	// Compute local transform
	self->matrix_local = trs_to_m4(self->transform_local);

	// Compute world transform
	if (self->parent_transform)
	{
		glm_mat4_mul(self->parent_transform->matrix.data, self->matrix_local.data, self->matrix.data);
	}
	else { glm_mat4_copy(self->matrix_local.data, self->matrix.data); }

	for (u32 i = 0; i < array_len(self->chidren_transform); ++i)
	{
		transform_update_tree(self->chidren_transform[i]);
	}
}

b8
transform_is_descendant_of(transform_component *self, transform_component *transform)
{
	if (!self->parent_transform) { return (false); }

	if (self->parent_transform == transform) { return (true); }

	for (u32 i = 0; i < array_len(transform->chidren_transform); ++i)
	{
		if (transform_is_descendant_of(self, transform->chidren_transform[i])) { return (true); }
	}

	return (false);
}
