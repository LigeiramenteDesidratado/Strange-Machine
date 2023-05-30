#include "core/smBase.h"

#include "math/smMath.h"
#include "math/smShape.h"

// helper
capsule
shape_capsule_new(sphere s, f32 height)
{
	capsule result;

	glm_vec3_copy((vec3){s.center.x, s.center.y - s.radius, s.center.z}, result.base.data);
	glm_vec3_copy((vec3){s.center.x, (s.center.y - s.radius) + height, s.center.z}, result.tip.data);

	result.radius = s.radius;

	return (result);
}

aabb
shape_get_aabb_sphere(sphere s)
{
	aabb result;

	v3 smin;
	smin.x = s.center.x - s.radius;
	smin.y = s.center.y - s.radius;
	smin.z = s.center.z - s.radius;

	v3 smax;
	smax.x = s.center.x + s.radius;
	smax.y = s.center.y + s.radius;
	smax.z = s.center.z + s.radius;

	glm_vec3_copy(smin.data, result.min.data);
	glm_vec3_copy(smax.data, result.max.data);

	return (result);
}

aabb
shape_get_aabb_capsule(capsule c)
{
	aabb result;

	v3 bmin;
	bmin.x = c.base.x - c.radius;
	bmin.y = c.base.y - c.radius;
	bmin.z = c.base.z - c.radius;

	v3 bmax;
	bmax.x = c.base.x + c.radius;
	bmax.y = c.base.y + c.radius;
	bmax.z = c.base.z + c.radius;

	v3 tmin;
	tmin.x = c.tip.x - c.radius;
	tmin.y = c.tip.y - c.radius;
	tmin.z = c.tip.z - c.radius;

	v3 tmax;
	tmax.x = c.tip.x + c.radius;
	tmax.y = c.tip.y + c.radius;
	tmax.z = c.tip.z + c.radius;

	v3 cmin, cmax;
	glm_vec3_minv(bmin.data, tmin.data, cmin.data);
	glm_vec3_maxv(bmax.data, tmax.data, cmax.data);

	glm_vec3_copy(cmin.data, result.min.data);
	glm_vec3_copy(cmax.data, result.max.data);

	return (result);
}

aabb
shape_get_aabb_triangle(triangle t)
{
	aabb result;

	v3 tmin;
	glm_vec3_minv(t.p2.data, t.p1.data, tmin.data);
	glm_vec3_minv(t.p0.data, tmin.data, tmin.data);

	v3 tmax;
	glm_vec3_maxv(t.p2.data, t.p1.data, tmax.data);
	glm_vec3_maxv(t.p0.data, tmax.data, tmax.data);

	glm_vec3_copy(tmin.data, result.min.data);
	glm_vec3_copy(tmax.data, result.max.data);

	return (result);
}

aabb
shape_get_positions_aabb(array(v3) positions)
{
	// Create the bounding box
	aabb result;

	v3 min_vert;
	v3 max_vert;

	if (positions != 0)
	{
		glm_vec3_copy(positions[0].data, min_vert.data);
		glm_vec3_copy(positions[0].data, max_vert.data);

		for (u32 i = 1; i < array_len(positions); ++i)
		{
			glm_vec3_minv(min_vert.data, positions[i].data, min_vert.data);
			glm_vec3_maxv(max_vert.data, positions[i].data, max_vert.data);
		}
	}

	glm_vec3_copy(min_vert.data, result.min.data);
	glm_vec3_copy(max_vert.data, result.max.data);

	return (result);
}
