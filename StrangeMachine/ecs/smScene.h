#ifndef SM_ECS_SCENE
#define SM_ECS_SCENE

#include "ecs/smECS.h"

struct scene;
typedef b8 (*system_f)(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);

struct system_info
{
	str8 name;
	void *user_data;

	system_f system;
};

typedef struct entity
{
	handle_t handle;
} entity_t;

struct indirect_access
{
	entity_t self;

	entity_t parent;
	array(entity_t) children;

	enum
	{
		HIERARCHY_FLAG_NONE = 0,
		HIERARCHY_FLAG_DIRTY = BIT(0),

		// enforce 32-bit size enum
		SM__HIERARCHY_FLAG_ENFORCE_ENUM_SIZE = 0x7fffffff
	} flags;

	component_t archetype;
	handle_t handle;
	u32 component_pool_index;
};

struct scene
{
	struct arena *arena;
	struct handle_pool indirect_handle_pool;
	array(struct indirect_access) indirect_access;

	array(struct system_info) sys_info;
	array(struct component_pool) component_handle_pool;

	entity_t main_camera;
	v3 gravity_force;
};

void scene_make(struct arena *arena, struct scene *scene);
void scene_release(struct arena *arena, struct scene *scene);

entity_t scene_load(struct arena *arena, struct scene *scene, str8 name);
entity_t scene_load_animated(struct arena *arena, struct scene *scene, str8 name);

entity_t scene_set_main_camera(struct scene *scene, entity_t camera_entity);
entity_t scene_get_main_camera(struct scene *scene);
v3 scene_set_gravity_force(struct scene *scene, v3 gravity);

void scene_copy(struct scene *dest, struct scene *src);

// Loop through all valid components and decrement the reference counter.
// Useful when you want to clear the arena but don't want to waste CPU cycles freeing each component individually
void scene_unmake_refs(struct scene *scene);

entity_t scene_entity_new(struct arena *arena, struct scene *scene, component_t archetype);
void scene_entity_remove(struct scene *scene, entity_t entity);
b8 scene_entity_is_valid(struct scene *scene, entity_t entity);
b8 scene_entity_has_components(struct scene *scene, entity_t entity, component_t components);
void scene_entity_add_component(struct arena *arena, struct scene *scene, entity_t entity, component_t components);
void *scene_component_get_data(struct scene *scene, entity_t entity, component_t component);

void scene_entity_update_hierarchy(struct scene *scene, entity_t self);
b8 scene_entity_is_descendant_of(struct scene *scene, entity_t self, entity_t entity);
void scene_entity_set_parent(struct scene *scene, entity_t self, entity_t new_parent);
void scene_entity_add_child(struct scene *scene, entity_t self, entity_t child);
void scene_entity_set_position_local(struct scene *scene, entity_t self, v3 position);
void scene_entity_set_position(struct scene *scene, entity_t self, v3 position);
void scene_entity_set_rotation_local(struct scene *scene, entity_t self, v4 rotation);
void scene_entity_set_rotation(struct scene *scene, entity_t self, v4 rotation);
void scene_entity_set_scale_local(struct scene *scene, entity_t self, v3 scale);
void scene_entity_translate(struct scene *scene, entity_t self, v3 delta);
void scene_entity_rotate(struct scene *scene, entity_t self, v4 delta);

void scene_system_register(struct arena *arena, struct scene *scene, str8 name, system_f system, void *user_data);
struct scene_iter scene_iter_begin(struct scene *scene, component_t constraint);
b8 scene_iter_next(struct scene *scene, struct scene_iter *iter);
void *scene_iter_get_component(struct scene_iter *iter, component_t component);
void scene_system_run(struct arena *arena, struct scene *scene, struct ctx *ctx);

struct scene_iter
{
	b8 first_iter;
	component_t constraint;
	u32 comp_pool_index;

	u32 index;
	const struct component_pool *comp_pool_ref;
};

struct scene_iter scene_iter_begin(struct scene *scene, component_t constraint);
b8 scene_iter_next(struct scene *scene, struct scene_iter *iter);
void *scene_iter_get_component(struct scene_iter *iter, component_t component);

void scene_print_archeype(struct arena *arena, struct scene *scene, entity_t entity);

#endif // SM_ECS_SCENE
