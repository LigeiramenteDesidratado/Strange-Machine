#include "animation/smPose.h"

void
pose_resize(struct arena *arena, struct pose *pose, u32 size)
{
	assert(pose != 0);

	u32 joint_old_size = array_len(pose->joints);
	array_set_len(arena, pose->joints, size);
	u32 joint_new_size = array_len(pose->joints);

	for (u32 i = 0; i < (joint_new_size - joint_old_size); ++i)
	{
		pose->joints[joint_old_size + i] = trs_identity();
	}

	u32 parent_old_size = array_len(pose->parents);
	array_set_len(arena, pose->parents, size);
	u32 parent_new_size = array_len(pose->parents);

	for (u32 i = 0; i < (parent_new_size - parent_old_size); ++i) { pose->parents[parent_old_size + i] = 0; }
}

trs
pose_get_local_transform(struct pose *pose, u32 index)
{
	assert(pose != 0);
	assert(index < array_len(pose->joints));

	return pose->joints[index];
}

trs
pose_get_global_transform(const struct pose *pose, u32 index)
{
	assert(pose != 0);
	assert(index < array_len(pose->joints));

	trs result = pose->joints[index];
	for (i32 p = pose->parents[index]; p >= 0; p = pose->parents[p])
	{
		result = trs_combine(pose->joints[p], result);
	}

	return (result);
}

#if 0
void
pose_get_matrix_palette(struct pose *pose, array(m4 *) out)
{
	u32 joints_len = array_len(pose->joints);
	u32 palette_len = array_len((*out));

	if (palette_len != joints_len)
	{
		array_set_len((*out), joints_len);
		for (u32 i = 0; i < joints_len; ++i) { (*out)[i] = m4_identity(); }
	}

	for (u32 i = 0; i < joints_len; ++i)
	{
		struct transform t = pose_get_global_transform(pose, i);
		(*out)[i] = transform_to_m4(t);
	}
}
#else
void
pose_get_matrix_palette(const struct pose *pose, struct arena *arena, array(m4 *) out)
{
	u32 joints_len = array_len(pose->joints);
	u32 palette_len = array_len((*out));

	if (palette_len != joints_len)
	{
		array_set_len(arena, *out, joints_len);
		for (u32 i = 0; i < joints_len; ++i) { (*out)[i] = m4_identity(); }
	}

	u32 i = 0;
	// for (; i < joints_len; ++i)
	// {
	// 	i32 parent = pose->parents[i];
	// 	// this breaks ascending order, so the previous calculated results cannot be used
	// 	if (parent > i) { break; }
	//
	// 	m4 global = transform_to_m4(pose->joints[i]);
	// 	if (parent >= 0) { glm_mat4_mul((*out)[parent].data, global.data, global.data); }
	// 	(*out)[i] = global;
	// }

	for (; i < joints_len; ++i)
	{
		trs t = pose_get_global_transform(pose, i);
		(*out)[i] = trs_to_m4(t);
	}
}
#endif

// returns true if the search node is a descendant of the given root node
b8
pose_is_in_hierarchy(struct pose *pose, u32 root, u32 search)
{
	assert(search < array_len(pose->parents));

	if (search == root) { return (true); }

	i32 p = pose->parents[search];
	while (p >= 0)
	{
		if (p == (i32)root) { return (true); }
		p = pose->parents[p];
	}

	return (false);
}

void
pose_blend(struct pose *output, struct pose *a, struct pose *b, f32 t, i32 root)
{
	u32 num_joints = array_len(output->joints);
	for (u32 i = 0; i < num_joints; ++i)
	{
		if (root >= 0)
		{
			// don't blend if they aren't within the same hierarchy
			if (!pose_is_in_hierarchy(output, (u32)root, i)) { continue; }
		}
		trs mix = trs_mix(pose_get_local_transform(a, i), pose_get_local_transform(b, i), t);
		output->joints[i] = mix;
	}
}

b8
pose_is_equal(struct pose *a, struct pose *b)
{
	if (a == 0 || b == 0) { return (false); }

	if (array_len(a->joints) != array_len(b->joints)) { return (false); }

	u32 size = array_len(a->joints);
	for (u32 i = 0; i < size; ++i)
	{
		trs a_local = a->joints[i];
		trs b_local = b->joints[i];

		i32 a_parent = a->parents[i];
		i32 b_parent = b->parents[i];

		if (a_parent != b_parent) { return (false); }

		if (!glm_vec3_eqv(a_local.translation.data, b_local.translation.data)) { return (false); }

		b8 is_eq = glm_vec4_eqv_eps(a_local.rotation.data, b_local.rotation.data);
		if (!is_eq) { return (false); }

		if (glm_vec3_eqv(a_local.scale.data, b_local.scale.data)) { return (false); }
	}

	return true;
}

void
pose_copy(struct arena *arena, struct pose *dest, struct pose *src)
{
	if (dest == src) { return; }

	u32 len = array_len(src->parents);
	pose_resize(arena, dest, len);

	if (len != 0)
	{
		memcpy(dest->parents, src->parents, len * sizeof(i32));
		memcpy(dest->joints, src->joints, len * sizeof(trs));
	}
}
