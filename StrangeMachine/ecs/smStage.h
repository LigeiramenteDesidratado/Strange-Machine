#ifndef SM_ECS_STAGE_H
#define SM_ECS_STAGE_H

#include "core/smBase.h"
#include "core/smCore.h"
#include "ecs/smScene.h"

b8 stage_init(struct buf base_memory);
void stage_teardown(void);

void stage_do(struct ctx *ctx);

void stage_set_main_camera(entity_t camera_entity);
camera_component *stage_get_main_camera(void);
entity_t stage_get_main_camera_entity(void);

void stage_set_gravity_force(v3 gravity_force);
v3 stage_get_gravity_force(void);

void stage_scene_new(str8 name);
void stage_set_current_by_name(str8 name);
b8 stage_is_current_scene(str8 name);
entity_t stage_scene_asset_load(str8 name);
struct arena *stage_scene_get_arena(void);
entity_t stage_animated_asset_load(str8 name);
entity_t stage_entity_new(component_t archetype);
void stage_entity_remove(entity_t entity);
b8 stage_entity_is_valid(entity_t entity);
b8 stage_entity_has_components(entity_t entity, component_t components);
void stage_entity_add_component(entity_t entity, component_t components);
void *stage_component_get_data(entity_t entity, component_t component);
void stage_system_register(str8 name, system_f system, void *user_data);

struct scene_iter stage_iter_begin(component_t constraint);
b8 stage_iter_next(struct scene_iter *iter);
void *stage_iter_get_component(struct scene_iter *iter, component_t component);

#endif // SM_ECS_stage_H
