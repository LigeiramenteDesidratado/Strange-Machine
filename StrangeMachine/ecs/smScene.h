#ifndef SM_ECS_SCENE
#define SM_ECS_SCENE

#include "ecs/smECS.h"

struct scene;
typedef b32 (*system_f)(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);

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

struct node
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

typedef void (*scene_pipeline_attach_f)(struct arena *arena, struct scene *scene, struct ctx *ctx);
typedef void (*scene_pipeline_update_f)(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
typedef void (*scene_pipeline_draw_f)(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
typedef void (*scene_pipeline_detach_f)(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);

struct scene
{
	struct arena *arena;
	struct handle_pool nodes_handle_pool;
	u32 nodes_cap;
	struct node *nodes;

	entity_t main_camera;

	array(struct system_info) sys_info;
	array(struct component_pool) component_handle_pool;

	void *user_data;
	scene_pipeline_attach_f attach;
	scene_pipeline_update_f update;
	scene_pipeline_draw_f draw;
	scene_pipeline_detach_f detach;
};

void scene_make(struct arena *arena, struct scene *scene);
void scene_release(struct arena *arena, struct scene *scene);
void scene_mount_pipeline(struct scene *scene, scene_pipeline_attach_f attach, scene_pipeline_update_f update,
    scene_pipeline_draw_f draw, scene_pipeline_detach_f detach);

void scene_on_attach(struct arena *arena, struct scene *scene, struct ctx *ctx);
void scene_on_detach(struct arena *arena, struct scene *scene, struct ctx *ctx);
void scene_on_update(struct arena *arena, struct scene *scene, struct ctx *ctx);
void scene_on_draw(struct arena *arena, struct scene *scene, struct ctx *ctx);

void scene_set_main_camera(struct scene *scene, entity_t entity);
entity_t scene_get_main_camera(struct scene *scene);
camera_component *scene_get_main_camera_data(struct scene *scene);

void scene_load(struct arena *arena, struct scene *scene, str8 name);

// Loop through all valid components and decrement the reference counter.
// Useful when you want to clear the arena but don't want to waste CPU cycles freeing each component individually
void scene_unmake_refs(struct scene *scene);

entity_t scene_entity_new(struct arena *arena, struct scene *scene, component_t archetype);
void scene_entity_remove(struct scene *scene, entity_t entity);
b32 scene_entity_is_valid(struct scene *scene, entity_t entity);
b32 scene_entity_has_components(struct scene *scene, entity_t entity, component_t components);
void scene_entity_add_component(struct arena *arena, struct scene *scene, entity_t entity, component_t components);
void *scene_component_get_data(struct scene *scene, entity_t entity, component_t component);

void scene_entity_set_dirty(struct scene *scene, entity_t entity, b32 dirty);
b32 scene_entity_is_dirty(struct scene *scene, entity_t entity);
void scene_entity_update_hierarchy(struct scene *scene, entity_t self);
b32 scene_entity_is_descendant_of(struct scene *scene, entity_t self, entity_t entity);
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
void scene_system_run(struct arena *arena, struct scene *scene, struct ctx *ctx);

struct scene_iter
{
	REF(const struct scene) scene_ref;
	b32 first_iter;
	component_t constraint;
	u32 comp_pool_index;

	u32 index;
	REF(const struct component_pool) comp_pool_ref;
};

struct scene_iter scene_iter_begin(struct scene *scene, component_t constraint);
b32 scene_iter_next(struct scene *scene, struct scene_iter *iter);
void *scene_iter_get_component(struct scene_iter *iter, component_t component);
entity_t scene_iter_get_entity(struct scene_iter *iter);

void scene_print_archeype(struct arena *arena, struct scene *scene, entity_t entity);

#endif // SM_ECS_SCENE
