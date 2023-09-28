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
#define INTERPOLATION_CONSTANT 0x00000001 // or step
#define INTERPOLATION_LINEAR   0x00000002
#define INTERPOLATION_CUBIC    0x00000003
	const u32 interpolation;

#define TRACK_TYPE_SCALAR 0x00000001
#define TRACK_TYPE_V3	  0x00000002
#define TRACK_TYPE_V4	  0x00000003
	u32 track_type;

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

i32 track_frame_index(struct track *track, f32 time, b8 looping);
void track_index_look_up_table(struct arena *arena, struct track *track);

struct transform_track
{
	u32 id;
	struct track position;
	struct track rotation;
	struct track scale;
};

b8 transform_track_is_valid(struct transform_track *transform_track);
f32 transform_track_get_start_time(const struct transform_track *transform_track);
f32 transform_track_get_end_time(const struct transform_track *transform_track);
trs transform_track_sample(struct transform_track *transform_track, trs *transform_ref, f32 time, b8 looping);

struct transform_track *clip_get_transform_track_from_joint(struct arena *arena, struct clip_resource *clip, u32 joint);
void clip_recalculate_duration(struct clip_resource *clip);
f32 clip_get_duration(struct clip_resource *clip);
f32 clip_adjust_time(struct clip_resource *const clip, f32 t);
f32 clip_sample(struct clip_resource *clip, struct pose *pose, f32 t);

#endif // SM_ANIMATION_H
