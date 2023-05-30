#include "animation/smAnimation.h"
#include "core/smBase.h"
#include "core/smLog.h"
#include "ecs/smECS.h"

static f32 sm__track_sample_constant_scalar(struct track *track, f32 t, b8 looping);
static f32 sm__track_sample_linear_scalar(struct track *track, f32 time, b8 looping);
static f32 sm__track_sample_cubic_scalar(struct track *track, f32 time, b8 looping);

static void sm__track_sample_constant_v3(struct track *track, f32 t, b8 looping, vec3 out);
static void sm__track_sample_linear_v3(struct track *track, f32 time, b8 looping, vec3 out);
static void sm__track_sample_cubic_v3(struct track *track, f32 time, b8 looping, vec3 out);

static void sm__track_sample_constant_v4(struct track *track, f32 t, b8 looping, versor out);
static void sm__track_sample_linear_v4(struct track *track, f32 time, b8 looping, versor out);
static void sm__track_sample_cubic_v4(struct track *track, f32 time, b8 looping, versor out);

static f32 sm__track_hermite_scalar(f32 t, f32 p1, f32 s1, f32 _p2, f32 s2);
static void sm__track_hermite_v3(f32 t, vec3 p1, vec3 s1, vec3 _p2, vec3 s2, vec3 out);
static void sm__track_hermite_v4(f32 t, versor p1, versor s1, versor _p2, versor s2, versor out);

f32
track_sample_scalar(struct track *track, f32 time, bool looping)
{
	assert(track);

	switch (track->interpolation)
	{
	case INTERPOLATION_CONSTANT: return sm__track_sample_constant_scalar(track, time, looping);
	case INTERPOLATION_LINEAR: return sm__track_sample_linear_scalar(track, time, looping);
	case INTERPOLATION_CUBIC: return sm__track_sample_cubic_scalar(track, time, looping);
	default: sm__unreachable();
	}

	return (0.0f);
}

void
track_sample_vec3(struct track *track, f32 time, b8 looping, vec3 out)
{
	assert(track);

	switch (track->interpolation)
	{
	case INTERPOLATION_CONSTANT: return sm__track_sample_constant_v3(track, time, looping, out);
	case INTERPOLATION_LINEAR: return sm__track_sample_linear_v3(track, time, looping, out);
	case INTERPOLATION_CUBIC: return sm__track_sample_cubic_v3(track, time, looping, out);
	default: sm__unreachable();
	}
}

void
track_sample_v4(struct track *track, f32 time, b8 looping, versor out)
{
	switch (track->interpolation)
	{
	case INTERPOLATION_CONSTANT: return sm__track_sample_constant_v4(track, time, looping, out);
	case INTERPOLATION_LINEAR: return sm__track_sample_linear_v4(track, time, looping, out);
	case INTERPOLATION_CUBIC: return sm__track_sample_cubic_v4(track, time, looping, out);
	default: sm__unreachable();
	}
}

f32
track_get_start_time(const struct track *track)
{
	f32 result;

	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR: result = track->frames_scalar[0].t; break;
	case TRACK_TYPE_V3: result = track->frames_v3[0].t; break;
	case TRACK_TYPE_V4: result = track->frames_v4[0].t; break;
	default: sm__unreachable();
	}

	return (result);
}

f32
track_get_end_time(const struct track *track)
{
	f32 result;

	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
		{
			u32 index = array_len(track->frames_scalar) - 1;
			result = track->frames_scalar[index].t;
		}
		break;
	case TRACK_TYPE_V3:
		{
			u32 index = array_len(track->frames_v3) - 1;
			result = track->frames_v3[index].t;
		}
		break;
	case TRACK_TYPE_V4:
		{
			u32 index = array_len(track->frames_v4) - 1;
			result = track->frames_v4[index].t;
		}
		break;
	default: sm__unreachable();
	}

	return (result);
}

f32
track_adjust_time(struct track *track, f32 t, b8 looping)
{
	f32 start_time;
	f32 end_time;

	// If a track has less than one frame, the track is invalid. If an invalid
	// track is used, retun 0
	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
		{
			u32 len = array_len(track->frames_scalar);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_scalar[0].t;
			end_time = track->frames_scalar[len - 1].t;
		}
		break;
	case TRACK_TYPE_V3:
		{
			u32 len = array_len(track->frames_v3);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_v3[0].t;
			end_time = track->frames_v3[len - 1].t;
		}
		break;
	case TRACK_TYPE_V4:
		{
			u32 len = array_len(track->frames_v4);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_v4[0].t;
			end_time = track->frames_v4[len - 1].t;
		}
		break;
	default: sm__unreachable();
	}

	f32 duration = end_time - start_time;
	if (duration <= 0.0f) { return 0.0f; }

	if (looping)
	{
		// If the track loops, adjust the time by the duration of the track
		t = fmodf(t - start_time, duration);
		if (t < 0.0f) { t += duration; }
		t += start_time;
	}
	else
	{
		// If the track does not loop, clamp the time to the first or last frame.
		// Return the adjusted time
		if (t <= start_time) t = start_time;
		if (t >= end_time) t = end_time;
	}

	return (t);
}

i32
track_frame_index(struct track *track, f32 time, b8 looping)
{
	u32 len;
	f32 start_time;
	f32 end_time;
	f32 duration;

	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
		{
			len = array_len(track->frames_scalar);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_scalar[0].t;
			end_time = track->frames_scalar[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	case TRACK_TYPE_V3:
		{
			len = array_len(track->frames_v3);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_v3[0].t;
			end_time = track->frames_v3[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	case TRACK_TYPE_V4:
		{
			len = array_len(track->frames_v4);
			if (len == 0) { return 0.0f; }
			start_time = track->frames_v4[0].t;
			end_time = track->frames_v4[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	default: sm__unreachable();
	}

	if (looping)
	{
		time = fmodf(time - start_time, duration);
		if (time < 0.0f) { time += duration; }
		time += start_time;
	}
	else
	{
		if (time <= start_time) { return 0; }
		if (time >= end_time) { return (i32)len - 1; }
	}

	f32 t = time / duration;
	f32 num_samples = (duration * 60.0f);
	u32 index = (u32)(t * num_samples);

	if (index >= array_len(track->sampled_frames))
	{
		return (track->sampled_frames[array_len(track->sampled_frames) - 1]);
	}

	return ((i32)track->sampled_frames[index]);
}

void
track_index_look_up_table(struct arena *arena, struct track *track)
{
	i32 len;
	f32 start_time;
	f32 end_time;
	f32 duration;

	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
		{
			len = (i32)array_len(track->frames_scalar);
			if (len == 0) { return; }
			start_time = track->frames_scalar[0].t;
			end_time = track->frames_scalar[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	case TRACK_TYPE_V3:
		{
			len = (i32)array_len(track->frames_v3);
			if (len == 0) { return; }
			start_time = track->frames_v3[0].t;
			end_time = track->frames_v3[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	case TRACK_TYPE_V4:
		{
			len = (i32)array_len(track->frames_v4);
			if (len == 0) { return; }
			start_time = track->frames_v4[0].t;
			end_time = track->frames_v4[len - 1].t;
			duration = end_time - start_time;
		}
		break;
	default: sm__unreachable();
	}
	u32 num_samples = (u32)(duration * 60.0f);

	u32 old_length = array_len(track->sampled_frames);
	array_set_len(arena, track->sampled_frames, num_samples);
	u32 new_length = array_len(track->sampled_frames);
	for (u32 i = 0; i < (new_length - old_length); ++i) { track->sampled_frames[old_length + i] = 0; }

	for (u32 i = 0; i < num_samples; ++i)
	{
		f32 t = (f32)i / (f32)(num_samples - 1);
		f32 time = t * duration + start_time;

		i32 frame_index = 0;
		for (i32 j = len - 1; j >= 0; --j)
		{
			if (track->track_type == TRACK_TYPE_SCALAR)
			{
				if (time >= track->frames_scalar[j].t)
				{
					frame_index = j;
					if (frame_index >= len - 2) { frame_index = (i32)len - 2; }
					break;
				}
			}
			else if (track->track_type == TRACK_TYPE_V3)
			{
				if (time >= track->frames_v3[j].t)
				{
					frame_index = j;
					if (frame_index >= len - 2) { frame_index = (i32)len - 2; }
					break;
				}
			}
			else if (track->track_type == TRACK_TYPE_V4)
			{
				if (time >= track->frames_v4[j].t)
				{
					frame_index = j;
					if (frame_index >= len - 2) { frame_index = (i32)len - 2; }
					break;
				}
			}
			else { sm__unreachable(); }
		}

		track->sampled_frames[i] = frame_index;
	}
}

static f32
sm__track_sample_constant_scalar(struct track *track, f32 t, b8 looping)
{
	// To do a constant (step) sample, find the frame based on the time with the
	// track_frame_index helper. Make sure the frame is valid, then cast the value
	// of that frame to the correct data type and return it
	i32 len = (i32)array_len(track->frames_scalar);
	i32 frame = track_frame_index(track, t, looping);
	if (frame < 0 || frame >= len) { return (0.0f); }
	return (track->frames_scalar[frame].value);
}

static f32
sm__track_sample_linear_scalar(struct track *track, f32 t, b8 looping)
{
	i32 len = (i32)array_len(track->frames_scalar);
	i32 this_frame = track_frame_index(track, t, looping);
	if (this_frame < 0 || this_frame >= (len - 1)) return (0.0f);

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, t, looping);
	f32 this_time = track->frames_scalar[this_frame].t;
	f32 frame_delta = track->frames_scalar[next_frame].t - this_time;

	if (frame_delta <= 0.0f) return 0.0f;

	f32 time = (track_time - this_time) / frame_delta;

	f32 start = track->frames_scalar[this_frame].value;
	f32 end = track->frames_scalar[next_frame].value;

	return glm_lerp(start, end, time);
}

// Cubic track sampling The final type of sampling, cubic sampling, finds the
// frames to sample and the interpolation time in the same way that linear
// sampling did. This function calls the Hermite helper function to do its
// interpolation.
static f32
sm__track_sample_cubic_scalar(struct track *track, f32 time, b8 looping)
{
	i32 len = (i32)array_len(track->frames_scalar);
	i32 this_frame = track_frame_index(track, time, looping);
	if (this_frame < 0 || this_frame >= (len - 1)) { return (0.0f); }

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, time, looping);
	f32 this_time = track->frames_scalar[this_frame].t;
	f32 frame_delta = track->frames_scalar[next_frame].t - this_time;

	if (frame_delta <= 0.0f) return 0.0f;

	f32 t = (track_time - this_time) / frame_delta;

	f32 point1 = track->frames_scalar[this_frame].value;
	f32 slope1 = track->frames_scalar[this_frame].out * frame_delta;

	f32 point2 = track->frames_scalar[next_frame].value;
	f32 slope2 = track->frames_scalar[next_frame].in * frame_delta;

	return sm__track_hermite_scalar(t, point1, slope1, point2, slope2);
}

static f32
sm__track_hermite_scalar(f32 t, f32 p1, f32 s1, f32 _p2, f32 s2)
{
	f32 result;

	f32 tt = t * t;
	f32 ttt = tt * t;

	f32 p2 = _p2;

	f32 h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
	f32 h2 = -2.0f * ttt + 3.0f * tt;
	f32 h3 = ttt - 2.0f * tt + t;
	f32 h4 = ttt - tt;

	result = p1 * h1 + p2 * h2 + s1 * h3 + s2 * h4;

	return (result);
}

static void
sm__track_sample_constant_v3(struct track *track, f32 t, b8 looping, vec3 out)
{
	// To do a constant (step) sample, find the frame based on the time with the
	// track_frame_index helper. Make sure the frame is valid, then cast the value
	// of that frame to the correct data type and return it
	i32 len = (i32)array_len(track->frames_v3);
	i32 frame = track_frame_index(track, t, looping);
	if (frame < 0 || frame >= len)
	{
		glm_vec3_zero(out);
		return;
	}

	v3 value = track->frames_v3[frame].value;

	glm_vec3_copy(value.data, out);
}

static void
sm__track_sample_linear_v3(struct track *track, f32 time, b8 looping, vec3 out)
{
	i32 len = (i32)array_len(track->frames_v3);
	i32 this_frame = track_frame_index(track, time, looping);
	if (this_frame < 0 || this_frame >= (len - 1))
	{
		glm_vec3_zero(out);
		return;
	}

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, time, looping);
	f32 this_time = track->frames_v3[this_frame].t;
	f32 frame_delta = track->frames_v3[next_frame].t - this_time;

	if (frame_delta <= 0.0f)
	{
		glm_vec3_zero(out);
		return;
	}

	f32 t = (track_time - this_time) / frame_delta;

	v3 start = track->frames_v3[this_frame].value;
	v3 end = track->frames_v3[next_frame].value;

	glm_vec3_lerp(start.data, end.data, t, out);
}

static void
sm__track_sample_cubic_v3(struct track *track, f32 time, b8 looping, vec3 out)
{
	i32 len = (i32)array_len(track->frames_v3);
	i32 this_frame = track_frame_index(track, time, looping);
	if (this_frame < 0 || this_frame >= (len - 1))
	{
		glm_vec3_zero(out);
		return;
	}

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, time, looping);
	f32 this_time = track->frames_v3[this_frame].t;
	f32 frame_delta = track->frames_v3[next_frame].t - this_time;

	if (frame_delta <= 0.0f)
	{
		glm_vec3_zero(out);
		return;
	}

	f32 t = (track_time - this_time) / frame_delta;

	v3 point1 = track->frames_v3[this_frame].value;
	v3 slope1 = track->frames_v3[this_frame].out;
	glm_vec3_scale(slope1.data, frame_delta, slope1.data);

	v3 point2 = track->frames_v3[next_frame].value;
	v3 slope2 = track->frames_v3[next_frame].in;
	glm_vec3_scale(slope2.data, frame_delta, slope2.data);

	sm__track_hermite_v3(t, point1.data, slope1.data, point2.data, slope2.data, out);
}

static void
sm__track_hermite_v3(f32 t, vec3 p1, vec3 s1, vec3 _p2, vec3 s2, vec3 out)
{
	f32 tt = t * t;
	f32 ttt = tt * t;

	vec3 p2;
	glm_vec3_copy(_p2, p2);

	f32 h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
	f32 h2 = -2.0f * ttt + 3.0f * tt;
	f32 h3 = ttt - 2.0f * tt + t;
	f32 h4 = ttt - tt;

	/* vec3_add(vec3_add(vec3_add(vec3_scale(p1, h1), vec3_scale(p2, h2)), vec3_scale(s1, h3)), vec3_scale(s2, h4));
	 */
	vec3 tmp1, tmp2, tmp3, tmp4;
	glm_vec3_scale(p1, h1, tmp1);
	glm_vec3_scale(p2, h2, tmp2);
	glm_vec3_scale(s1, h3, tmp3);
	glm_vec3_scale(s2, h4, tmp4);
	glm_vec3_add(tmp1, tmp2, tmp1);
	glm_vec3_add(tmp1, tmp3, tmp2);
	glm_vec3_add(tmp2, tmp4, out);
}

static void
sm__track_sample_constant_v4(struct track *track, f32 t, b8 looping, versor out)
{
	// To do a constant (step) sample, find the frame based on the time with the
	// track_frame_index helper. Make sure the frame is valid, then cast the value
	// of that frame to the correct data type and return it
	i32 len = (i32)array_len(track->frames_v4);
	i32 frame = track_frame_index(track, t, looping);
	if (frame < 0 || frame >= len)
	{
		glm_vec4_copy(v4_new(0.0f, 0.0f, 0.0f, 1.0f).data, out);
		return;
	}

	v4 value = track->frames_v4[frame].value;

	glm_quat_copy(value.data, out);
	glm_quat_normalize(out);
}

// static void
// sm__quat_mix(versor from, versor to, f32 t, versor result)
// {
// 	v4 left, right;
// 	glm_vec4_scale(from, (1.0f - t), left.data);
// 	glm_vec4_scale(to, t, right.data);
//
// 	// glm_quat_lerp();
//
// 	glm_quat_add(left.data, right.data, result);
// }

static void
sm__quat_interpolate(versor a, versor b, f32 t, versor result)
{
	// sm__quat_mix(a, b, t, result);
	glm_vec4_mix(a, b, t, result);
	if (glm_quat_dot(a, b) < 0)
	{
		v4 negate;
		glm_vec4_negate_to(b, negate.data);
		glm_vec4_mix(a, negate.data, t, result);
	}
	glm_quat_normalize(result);
}

static v4
sm__track_cast_quat(v4 quat)
{
	v4 result;
	glm_quat_copy(quat.data, result.data);
	glm_quat_normalize(result.data);
	return (result);
}

static void
sm__track_sample_linear_v4(struct track *track, f32 time, b8 looping, versor out)
{
	i32 len = (i32)array_len(track->frames_v4);
	i32 this_frame = track_frame_index(track, time, looping);
	if (this_frame < 0 || this_frame >= (len - 1))
	{
		glm_quat_copy(v4_new(0.0f, 0.0f, 0.0f, 1.0f).data, out);
		return;
	}

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, time, looping);
	f32 this_time = track->frames_v4[this_frame].t;
	f32 frame_delta = track->frames_v4[next_frame].t - this_time;

	if (frame_delta <= 0.0f)
	{
		glm_quat_copy(v4_new(0.0f, 0.0f, 0.0f, 1.0f).data, out);
		return;
	}

	f32 t = (track_time - this_time) / frame_delta;

	v4 start = sm__track_cast_quat(track->frames_v4[this_frame].value);
	v4 end = sm__track_cast_quat(track->frames_v4[next_frame].value);

	// glm_vec4_lerp(start.data, end.data, t, out);
	sm__quat_interpolate(start.data, end.data, t, out);
	// glm_vec4_lerp(start.data, end.data, t, out);
}

static void
sm__track_sample_cubic_v4(struct track *track, f32 time, b8 looping, versor out)
{
	i32 len = (i32)array_len(track->frames_v4);
	i32 this_frame = track_frame_index(track, time, looping);
	if (this_frame < 0 || this_frame >= (len - 1))
	{
		glm_quat_copy(v4_new(0.0f, 0.0f, 0.0f, 1.0f).data, out);
		return;
	}

	i32 next_frame = this_frame + 1;

	f32 track_time = track_adjust_time(track, time, looping);
	f32 this_time = track->frames_v4[this_frame].t;
	f32 frame_delta = track->frames_v4[next_frame].t - this_time;

	if (frame_delta <= 0.0f)
	{
		glm_quat_copy(v4_new(0.0f, 0.0f, 0.0f, 1.0f).data, out);
		return;
	}

	f32 t = (track_time - this_time) / frame_delta;

	v4 point1 = sm__track_cast_quat(track->frames_v4[next_frame].value);
	v4 slope1;
	glm_quat_copy(track->frames_v4[this_frame].out.data, slope1.data);
	glm_vec4_scale(slope1.data, frame_delta, slope1.data);

	v4 point2 = sm__track_cast_quat(track->frames_v4[next_frame].value);
	v4 slope2;
	glm_quat_copy(track->frames_v4[next_frame].in.data, slope2.data);
	glm_vec4_scale(slope2.data, frame_delta, slope2.data);

	sm__track_hermite_v4(t, point1.data, slope1.data, point2.data, slope2.data, out);
}

static void
sm__track_neighborhood(versor a, versor b)
{
	if (glm_quat_dot(a, b) < 0.0f) { glm_vec4_negate(b); }
}

static void
sm__track_hermite_v4(f32 t, versor p1, versor s1, versor _p2, versor s2, versor out)
{
	f32 tt = t * t;
	f32 ttt = tt * t;

	versor p2;
	glm_quat_copy(_p2, p2);
	sm__track_neighborhood(p1, p2);

	f32 h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
	f32 h2 = -2.0f * ttt + 3.0f * tt;
	f32 h3 = ttt - 2.0f * tt + t;
	f32 h4 = ttt - tt;

	versor result;

	versor tmp1, tmp2, tmp3, tmp4;
	glm_vec4_scale(p1, h1, tmp1);
	glm_vec4_scale(p2, h2, tmp2);
	glm_vec4_scale(s1, h3, tmp3);
	glm_vec4_scale(s2, h4, tmp4);

	glm_quat_add(tmp1, tmp2, tmp1);
	glm_quat_add(tmp1, tmp3, tmp2);
	glm_quat_add(tmp2, tmp4, result);

	glm_quat_normalize_to(result, out);
}

// The transform_track_is_valid helper function should only return true if at
// least one of the component tracks stored in the transform_track_s struct is
// valid. For a track to be valid, it needs to have two or more frames.
b8
transform_track_is_valid(struct transform_track *transform_track)
{
	assert(transform_track);

	return (array_len(transform_track->position.frames_v3) > 1) ||
	       (array_len(transform_track->rotation.frames_v4) > 1) ||
	       (array_len(transform_track->scale.frames_v3) > 1);
}

f32
transform_track_get_start_time(const struct transform_track *transform_track)
{
	assert(transform_track);

	f32 result = 0.0f;
	b8 is_set = false;

	if (array_len(transform_track->position.frames_v3) > 1)
	{
		result = track_get_start_time(&transform_track->position);
		is_set = true;
	}

	if (array_len(transform_track->rotation.frames_v4) > 1)
	{
		f32 rotation_start = track_get_start_time(&transform_track->rotation);
		if (rotation_start < result || !is_set)
		{
			result = rotation_start;
			is_set = true;
		}
	}

	if (array_len(transform_track->scale.frames_v3) > 1)
	{
		f32 scale_start = track_get_start_time(&transform_track->scale);
		if (scale_start < result || !is_set)
		{
			result = scale_start;
			is_set = true;
		}
	}

	return (result);
}

// The GetEndTime function is similar to the GetStartTime function. The only
// difference is that this function looks for the greatest trak end time.
f32
transform_track_get_end_time(const struct transform_track *transform_track)
{
	assert(transform_track);

	f32 result = 0.0f;
	bool is_set = false;

	if (array_len(transform_track->position.frames_v3) > 1)
	{
		result = track_get_end_time(&transform_track->position);
		is_set = true;
	}

	if (array_len(transform_track->rotation.frames_v4) > 1)
	{
		f32 rotation_end = track_get_end_time(&transform_track->rotation);
		if (rotation_end > result || !is_set)
		{
			result = rotation_end;
			is_set = true;
		}
	}

	if (array_len(transform_track->scale.frames_v3) > 1)
	{
		f32 scale_end = track_get_end_time(&transform_track->scale);
		if (scale_end > result || !is_set)
		{
			result = scale_end;
			is_set = true;
		}
	}

	return (result);
}

struct trs
transform_track_sample(struct transform_track *transform_track, struct trs *transform_ref, f32 time, b8 looping)
{
	assert(transform_track);

	struct trs result = *transform_ref; // Assign default values

	if (array_len(transform_track->position.frames_v3) > 1)
	{
		track_sample_vec3(&transform_track->position, time, looping, result.position.v3.data);
	}

	if (array_len(transform_track->rotation.frames_v4) > 1)
	{
		track_sample_v4(&transform_track->rotation, time, looping, result.rotation.data);
	}

	if (array_len(transform_track->scale.frames_v3) > 1)
	{
		track_sample_vec3(&transform_track->scale, time, looping, result.scale.data);
	}

	return (result);
}

f32
clip_get_duration(struct clip_resource *clip)
{
	assert(clip);
	return (clip->end_time - clip->start_time);
}

// The Sample function takes a  posere ference and a time and returns a float
// value that is also a time. This function samples the animation clip at the
// provided time into the pose reference.
f32
clip_sample(struct clip_resource *clip, struct pose *pose, f32 t)
{
	assert(clip);
	assert(pose);

	if (clip_get_duration(clip) == 0.0f) { return (0.0f); }

	t = clip_adjust_time(clip, t);

	u32 size = array_len(clip->tracks);
	for (u32 i = 0; i < size; ++i)
	{
		u32 j = clip->tracks[i].id; // joint
		struct trs local = pose_get_local_transform(pose, j);
		struct trs animated = transform_track_sample(&clip->tracks[i], &local, t, clip->looping);

		pose->joints[j] = animated;
	}

	return (t);
}

f32
clip_adjust_time(struct clip_resource *clip, f32 t)
{
	assert(clip);

	if (clip->looping)
	{
		f32 duration = clip->end_time - clip->start_time;
		if (duration <= 0.0f) { return (0.0f); }

		t = fmodf(t - clip->start_time, clip->end_time - clip->start_time);

		if (t < 0.0f) { t += clip->end_time - clip->start_time; }

		t += clip->start_time;
	}
	else
	{
		if (t < clip->start_time) t = clip->start_time;
		if (t > clip->end_time) t = clip->end_time;
	}

	return (t);
}

// The RecalculateDuration function sets mStartTime and mEndTime to default
// values of 0. Next, these functions loop through every TransformTrack object
// in the animation clip. If the track is valid, the start and end times of the
// track are retrieved. The smallest start time and the largest end time are
// stored. The start time of a clip might not be 0; it's possible to have a clip
// that starts at an arbitrary point in time.
void
clip_recalculate_duration(struct clip_resource *clip)
{
	assert(clip);

	clip->start_time = 0.0f;
	clip->end_time = 0.0f;

	b8 start_set = false;
	b8 end_set = false;

	u32 track_size = array_len(clip->tracks);
	for (u32 i = 0; i < track_size; ++i)
	{
		if (!transform_track_is_valid(&clip->tracks[i])) continue;

		f32 start_time = transform_track_get_start_time(&clip->tracks[i]);
		f32 end_time = transform_track_get_end_time(&clip->tracks[i]);

		if (start_time < clip->start_time || !start_set)
		{
			clip->start_time = start_time;
			start_set = true;
		}

		if (end_time > clip->end_time || !end_set)
		{
			clip->end_time = end_time;
			end_set = true;
		}
	}
}

// clip_get_transform_track_from_joint is meant to retrieve the
// transform_track_t object for a specific joint in the clip. This function is
// mainly used by whatever code loads the animation clip from a file. The
// function performs a linear search through all of the tracks to see whether
// any of them targets the specified joint. If a qualifying track is found, a
// reference to it is returned. If no qualifying track is found, a new one is
// created and returned:
// similar [] operator
struct transform_track *
clip_get_transform_track_from_joint(struct arena *arena, struct clip_resource *clip, u32 joint)
{
	assert(clip);

	for (u32 i = 0; i < array_len(clip->tracks); ++i)
	{
		if (clip->tracks[i].id == joint) { return (&clip->tracks[i]); }
	}

	struct transform_track tranform_track = {
	    .id = joint,
	    .scale = (struct track){.track_type = TRACK_TYPE_V3},
	    .position = (struct track){.track_type = TRACK_TYPE_V3},
	    .rotation = (struct track){.track_type = TRACK_TYPE_V4},
	};

	array_push(arena, clip->tracks, tranform_track);

	return (&clip->tracks[array_len(clip->tracks) - 1]);
}
