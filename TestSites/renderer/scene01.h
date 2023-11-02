#ifndef SM_SCENE01_H
#define SM_SCENE01_H

#include "core/smBase.h"
#include "core/smCore.h"
#include "ecs/smScene.h"

void scene01_on_attach(struct arena *arena, sm__maybe_unused struct scene *scene, struct ctx *ctx);
void scene01_on_update(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);
void scene01_on_draw(struct arena *arena, struct scene *scene, struct ctx *ctx, void *user_data);

#endif // SM_SCENE01_H
