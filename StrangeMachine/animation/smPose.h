#ifndef SM_ANIMATION_POSE_H
#define SM_ANIMATION_POSE_H

#include "core/smCore.h"
#include "math/smMath.h"

struct pose
{
	array(trs) joints;
	array(i32) parents;
};

void pose_resize(struct arena *arena, struct pose *pose, u32 size);
trs pose_get_local_transform(struct pose *pose, u32 index);
trs pose_get_global_transform(const struct pose *pose, u32 index);
void pose_get_matrix_palette(const struct pose *pose, struct arena *arena, array(m4 *) out);
b32 pose_is_in_hierarchy(struct pose *pose, u32 root, u32 search);
void pose_blend(struct pose *output, struct pose *a, struct pose *b, f32 t, i32 root);
b32 pose_is_equal(struct pose *a, struct pose *b);
void pose_copy(struct arena *arena, struct pose *dest, struct pose *src);

#endif // SM_ANIMATION_POSE_H
