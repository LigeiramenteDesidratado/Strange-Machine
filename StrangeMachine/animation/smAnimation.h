#ifndef SM_ANIMATION_H
#define SM_ANIMATION_H

#include "core/smArray.h"
#include "core/smBase.h"
#include "core/smString.h"
#include "ecs/smECS.h"
#include "math/smMath.h"

struct frame_scalar
{
	f32 value;
	f32 in;
	f32 out;
	f32 t;
};

struct frame_v3
{
	v3 value;
	v3 in;
	v3 out;
	f32 t;
};

struct frame_v4
{
	v4 value;
	v4 in;
	v4 out;
	f32 t;
};

struct track
{
	enum
	{
		ANIM_INTERPOLATION_CONSTANT = 0x1, // or step
		ANIM_INTERPOLATION_LINEAR,
		ANIM_INTERPOLATION_CUBIC,

		// enforce 32-bit size enum
		SM__ANIM__ENFORCE_ENUM_SIZE = 0x7fffffff
	} interpolation;

	enum
	{
		ANIM_TRACK_TYPE_SCALAR = 0x1,
		ANIM_TRACK_TYPE_V3,
		ANIM_TRACK_TYPE_V4,

		// enforce 32-bit size enum
		SM__ANIM_TRACK_TYPE_ENFORCE_ENUM_SIZE = 0x7fffffff
	} track_type;

	union
	{
		array(struct frame_scalar) frames_scalar;
		array(struct frame_v3) frames_v3;
		array(struct frame_v4) frames_v4;
	};

	i32 *sampled_frames;
};

f32 track_get_start_time(const struct track *track);
f32 track_get_end_time(const struct track *track);

i32 track_frame_index(struct track *track, f32 time, b32 looping);
void track_index_look_up_table(struct arena *arena, struct track *track);

struct transform_track
{
	u32 id;
	struct track position;
	struct track rotation;
	struct track scale;
};

b32 transform_track_is_valid(struct transform_track *transform_track);
f32 transform_track_get_start_time(const struct transform_track *transform_track);
f32 transform_track_get_end_time(const struct transform_track *transform_track);
trs transform_track_sample(struct transform_track *transform_track, trs *transform_ref, f32 time, b32 looping);

#endif // SM_ANIMATION_H
