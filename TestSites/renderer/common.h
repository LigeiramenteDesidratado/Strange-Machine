#ifndef SM_COMMON_UPDATE_H
#define SM_COMMON_UPDATE_H

#include "core/smBase.h"
#include "core/smCore.h"
#include "ecs/smScene.h"

b32 common_rigid_body_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_particle_emitter_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_pe_sort_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_mesh_calculate_aabb_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_camera_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_transform_clear_dirty(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_cfc_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_fade_to_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_m4_palette_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
b32 common_hierarchy_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);

#endif //
