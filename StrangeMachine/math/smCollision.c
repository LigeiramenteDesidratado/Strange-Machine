#include "core/smBase.h"

#include "math/smCollision.h"
#include "math/smMath.h"
#include "math/smShape.h"

#include "core/smCore.h"

void
sm__colision_closest_point_on_line_segment(vec3 a, vec3 b, vec3 point, vec3 out)
{
	v3 ab;
	glm_vec3_sub(b, a, ab.data);

	v3 ap;
	glm_vec3_sub(point, a, ap.data);

	f32 t = glm_vec3_dot(ap.data, ab.data) / glm_vec3_dot(ab.data, ab.data);

	t = fminf(fmaxf(t, 0.0f), 1.0f);

	vec3 closest_point;
	glm_vec3_scale(ab.data, t, closest_point);
	glm_vec3_add(a, closest_point, out);
}

void
collision_capsules(capsule a, capsule b, intersect_result *result)
{
	// capsule A:
	vec3 a_norm;
	glm_vec3_sub(a.tip.data, a.base.data, a_norm);
	glm_vec3_normalize(a_norm);

	vec3 a_line_end_offset;
	glm_vec3_scale(a_norm, a.radius, a_line_end_offset);

	vec3 a_a, a_b;
	glm_vec3_add(a.base.data, a_line_end_offset, a_a);
	glm_vec3_sub(a.tip.data, a_line_end_offset, a_b);

	// capsule B:
	vec3 b_norm;
	glm_vec3_sub(b.tip.data, b.base.data, b_norm);
	glm_vec3_normalize(b_norm);

	vec3 b_line_end_offset;
	glm_vec3_scale(b_norm, b.radius, b_line_end_offset);

	vec3 b_a, b_b;
	glm_vec3_add(b.base.data, b_line_end_offset, b_a);
	glm_vec3_sub(b.tip.data, b_line_end_offset, b_b);

	// vectors between line endpoints:
	vec3 v_0, v_1, v_2, v_3;
	glm_vec3_sub(b_a, a_a, v_0);
	glm_vec3_sub(b_b, a_a, v_1);
	glm_vec3_sub(b_a, a_b, v_2);
	glm_vec3_sub(b_b, a_b, v_3);

	// squared distances
	f32 d0 = glm_vec3_norm2(v_0);
	f32 d1 = glm_vec3_norm2(v_1);
	f32 d2 = glm_vec3_norm2(v_2);
	f32 d3 = glm_vec3_norm2(v_3);

	// select best potential endpoint on capsule A:
	vec3 a_best_point;
	if (d2 < d0 || d2 < d1 || d3 < d0 || d3 < d1) { glm_vec3_copy(a_b, a_best_point); }
	else { glm_vec3_copy(a_a, a_best_point); }

	// select point on capsule B line segment nearest to best potential endpoint
	// on A capsule:
	vec3 b_best_point;
	sm__colision_closest_point_on_line_segment(b_a, b_b, a_best_point, b_best_point);

	// now do the same for capsule A segment:
	sm__colision_closest_point_on_line_segment(a_a, a_b, b_best_point, a_best_point);

	// Finally, sphere collision:
	vec3 v;
	glm_vec3_sub(a_best_point, b_best_point, v);
	f32 d = glm_vec3_norm(v); // vector length

	/* vec3 N = vec3_sub(bestA, bestB); */
	/* f32 len = vec3_len(N); */

	glm_vec3_divs(v, d, v);
	vec3 position;
	glm_vec3_scale(v, a.radius, position);
	glm_vec3_sub(a_best_point, position, position);

	f32 depth = a.radius + b.radius - d;

	glm_vec3_copy(v, result->normal.data);
	glm_vec3_copy(position, result->position.data);

	result->valid = depth > 0.0f;
	result->depth = depth;
}

void
collision_sphere_triangle(sphere s, triangle t, world_component *world, intersect_result *result)
{
	/* vec3 center = s.center; */
	f32 radius = s.radius;

	vec3 v_1, v_2;
	glm_vec3_sub(t.p1.data, t.p0.data, v_1);
	glm_vec3_sub(t.p2.data, t.p0.data, v_2);

	vec3 n;
	glm_vec3_cross(v_1, v_2, n);
	glm_vec3_normalize(n);

	/* assert that the triangle is not degenerate. */
	assert(glm_vec3_norm2(n) > 0.0f);

	/* Find the nearest feature on the triangle to the sphere. */
	vec3 sub;
	glm_vec3_sub(s.center.data, t.p0.data, sub);
	f32 d = glm_vec3_dot(sub, n);

	/* If the center of the sphere is farther from the plane of the triangle */
	/* than the radius of the sphere, then there cannot be an intersection. */
	b8 no_intersection = (d < -radius || d > radius);

	/* Project the center of the sphere onto the plane of the triangle. */
	vec3 point0;
	glm_vec3_scale(n, d, point0);
	glm_vec3_sub(s.center.data, point0, point0); /* projected sphere center on triangle plane */

	/* Compute the cross products of the vector from the base of each edge to */
	/* the point with each edge vector. */
	vec3 edge0, edge1, edge2;
	glm_vec3_sub(t.p1.data, t.p0.data, edge0);
	glm_vec3_sub(t.p2.data, t.p1.data, edge1);
	glm_vec3_sub(t.p0.data, t.p2.data, edge2);

	vec3 p0, p1, p2;
	glm_vec3_sub(point0, t.p0.data, p0);
	glm_vec3_sub(point0, t.p1.data, p1);
	glm_vec3_sub(point0, t.p2.data, p2);

	vec3 c0, c1, c2;
	glm_vec3_cross(p0, edge0, c0);
	glm_vec3_cross(p1, edge1, c1);
	glm_vec3_cross(p2, edge2, c2);

	// If the cross product points in the same direction as the normal the the
	// point is inside the edge (it is zero if is on the edge).
	b8 intersection =
	    ((glm_vec3_dot(c0, n) <= 0.0f) && (glm_vec3_dot(c1, n) <= 0.0f)) && (glm_vec3_dot(c2, n) <= 0.0f);

	b8 inside = intersection && !no_intersection;

	f32 radiussq = radius * radius; // sphere radius squared

	// Find the nearest point on each edge.

	// Edge 0,1
	vec3 point1;
	sm__colision_closest_point_on_line_segment(t.p0.data, t.p1.data, s.center.data, point1);

	// If the distance to the center of the sphere to the point is less than
	// the radius of the sphere then it must intersect.
	vec3 res1;
	glm_vec3_sub(s.center.data, point1, res1);
	f32 distsq = glm_vec3_norm2(res1);
	intersection |= distsq <= radiussq;

	// Edge 1,2
	vec3 point2;
	sm__colision_closest_point_on_line_segment(t.p1.data, t.p2.data, s.center.data, point2);

	// If the distance to the center of the sphere to the point is less than
	// the radius of the sphere then it must intersect.
	glm_vec3_sub(s.center.data, point2, res1);
	distsq = glm_vec3_norm2(res1);
	intersection |= distsq <= radiussq;

	// Edge 2,0
	vec3 point3;
	sm__colision_closest_point_on_line_segment(t.p2.data, t.p0.data, s.center.data, point3);

	// If the distance to the center of the sphere to the point is less than
	// the radius of the sphere then it must intersect.
	glm_vec3_sub(s.center.data, point3, res1);
	distsq = glm_vec3_norm2(res1);
	intersection |= distsq <= radiussq;

	b8 intersects = intersection && !no_intersection;

	if (intersects)
	{
		vec3 best_point;
		glm_vec3_copy(point0, best_point);

		if (!inside)
		{
			// If the sphere center's projection on the triangle plane is not
			// within the triangle,
			// determine the closest point on triangle to the sphere center
			glm_vec3_sub(point1, s.center.data, res1);
			f32 best_dist = glm_vec3_norm2(res1);
			glm_vec3_copy(point1, best_point);

			glm_vec3_sub(point2, s.center.data, res1);
			f32 dist = glm_vec3_norm2(res1);

			if (dist < best_dist)
			{
				glm_vec3_copy(point2, best_point);
				best_dist = dist;
			}

			glm_vec3_sub(point3, s.center.data, res1);
			dist = glm_vec3_norm2(res1);
			if (dist < best_dist)
			{
				glm_vec3_copy(point3, best_point);
				best_dist = dist;
			}
		}

		vec3 intersection_vector;
		glm_vec3_sub(s.center.data, best_point, intersection_vector);
		f32 intersection_length = glm_vec3_norm(intersection_vector);

		result->valid = true;
		result->depth = radius - intersection_length;
		glm_vec3_copy(best_point, result->position.data);
		glm_vec3_divs(intersection_vector, intersection_length, result->normal.data);

		m4 inv;
		glm_mat4_inv(world->matrix.data, inv.data);

		v3 vel;
		glm_mat4_mulv3(inv.data, best_point, 1.0f, vel.data);
		glm_mat4_mulv3(world->last_matrix.data, vel.data, 1.0f, vel.data);
		glm_vec3_sub(best_point, vel.data, vel.data);

		glm_vec3_copy(vel.data, result->velocity.data);
	}
}

void
collision_capsule_triangle(capsule c, triangle t, world_component *world, intersect_result *result)
{
	vec3 base, tip;
	glm_vec3_copy(c.base.data, base);
	glm_vec3_copy(c.tip.data, tip);
	f32 radius = c.radius;

	vec3 line_end_offset;
	glm_vec3_sub(tip, base, line_end_offset);
	glm_vec3_normalize(line_end_offset);
	glm_vec3_scale(line_end_offset, radius, line_end_offset);

	vec3 a, b;
	glm_vec3_add(base, line_end_offset, a);
	glm_vec3_sub(tip, line_end_offset, b);

	// Compute the plane of the triangle (has to be normalized).
	vec3 n, v_1, v_2;
	glm_vec3_sub(t.p1.data, t.p0.data, v_1);
	glm_vec3_sub(t.p2.data, t.p0.data, v_2);
	glm_vec3_cross(v_1, v_2, n);
	glm_vec3_normalize(n);

	// assert that the triangle is not degenerate.
	assert(glm_vec3_norm2(n) > 0.0f);

	vec3 reference_point;
	vec3 capsule_normal;
	glm_vec3_sub(b, a, capsule_normal);
	glm_vec3_normalize(capsule_normal);

	if (fabsf(glm_vec3_dot(n, capsule_normal)) < GLM_FLT_EPSILON)
	{
		// Capsule line cannot be intersected with triangle plane (they are  parallel)
		// In this case, just take a point from triangle
		glm_vec3_copy(t.p0.data, reference_point);
	}
	else
	{
		/* Intersect capsule line with triangle plane: */
		vec3 left_top;
		glm_vec3_sub(base, t.p0.data, left_top);
		f32 right_top = fabsf(glm_vec3_dot(n, capsule_normal));
		glm_vec3_divs(left_top, right_top, left_top);
		f32 r = glm_vec3_dot(n, left_top);

		vec3 line_plane_intersection;
		glm_vec3_scale(capsule_normal, r, line_plane_intersection);
		glm_vec3_add(line_plane_intersection, base, line_plane_intersection);

		// Compute the cross products of the vector from the base of each edge
		// to the point with each edge vector.
		vec3 e0, e1, e2;
		glm_vec3_sub(t.p1.data, t.p0.data, e0);
		glm_vec3_sub(t.p2.data, t.p1.data, e1);
		glm_vec3_sub(t.p0.data, t.p2.data, e2);

		vec3 p0, p1, p2;
		glm_vec3_sub(line_plane_intersection, t.p0.data, p0);
		glm_vec3_sub(line_plane_intersection, t.p1.data, p1);
		glm_vec3_sub(line_plane_intersection, t.p2.data, p2);

		vec3 c0, c1, c2;
		glm_vec3_cross(p0, e0, c0);
		glm_vec3_cross(p1, e1, c1);
		glm_vec3_cross(p2, e2, c2);

		// If the cross product points in the same direction as the normal the
		// the point is inside the edge (it is zero if is on the edge).
		b8 inside = glm_vec3_dot(c0, n) <= 0 && glm_vec3_dot(c1, n) <= 0 && glm_vec3_dot(c2, n) <= 0;

		if (inside) { glm_vec3_copy(line_plane_intersection, reference_point); }
		else
		{
			vec3 point1, point2, point3;
			// Edge 1:
			sm__colision_closest_point_on_line_segment(
			    t.p0.data, t.p1.data, line_plane_intersection, point1);

			// Edge 2:
			sm__colision_closest_point_on_line_segment(
			    t.p1.data, t.p2.data, line_plane_intersection, point2);

			// Edge 3:
			sm__colision_closest_point_on_line_segment(
			    t.p2.data, t.p0.data, line_plane_intersection, point3);

			glm_vec3_copy(point1, reference_point);

			vec3 best_dist_vec;
			glm_vec3_sub(point1, line_plane_intersection, best_dist_vec);
			f32 best_dist = glm_vec3_norm2(best_dist_vec);

			vec3 dist_vec;
			f32 dist;

			glm_vec3_sub(point2, line_plane_intersection, dist_vec);
			dist = fabsf(glm_vec3_norm2(dist_vec));

			if (dist < best_dist)
			{
				best_dist = dist;
				glm_vec3_copy(point2, reference_point);
			}

			glm_vec3_sub(point3, line_plane_intersection, dist_vec);
			dist = fabsf(glm_vec3_norm2(dist_vec));

			if (dist < best_dist)
			{
				best_dist = dist;
				glm_vec3_copy(point3, reference_point);
			}
		}
	}

	// Place a sphere on closest point on line segment to intersection:
	v3 center;
	sm__colision_closest_point_on_line_segment(a, b, reference_point, center.data);

	sphere sph = {.center = center, .radius = radius};

	collision_sphere_triangle(sph, t, world, result);
}

// collision_check_spheres - Check if two spheres are colliding.
void
collision_spheres(sphere s1, sphere s2, intersect_result *result)
{
	vec3 d;
	glm_vec3_sub(s1.center.data, s2.center.data, d);

	f32 dist = glm_vec3_norm(d);

	if (dist < s1.radius + s2.radius)
	{
		// calculate the depth of the collision
		// and the normal of the collision
		// (the normal is the direction of the collision)
		// (the depth is the distance from the center of the sphere to the collision)
		f32 depth = s1.radius + s2.radius - dist;
		glm_vec3_normalize(d);

		result->valid = true;
		result->depth = depth;
		glm_vec3_copy(d, result->normal.data);
	}
}

void
collision_sphere_cube(sphere s, cube c, intersect_result *result)
{
	v3 d;
	glm_vec3_sub(s.center.data, c.center.data, d.data);

	// check if the sphere is inside the cube
	if (fabsf(d.x) <= c.size.x / 2.0f && fabsf(d.y) <= c.size.y / 2.0f && fabsf(d.z) <= c.size.z / 2.0f)
	{
		// calculate the depth of the collision
		// and the normal of the collision
		// (the normal is the direction of the collision)
		// (the depth is the distance from the center of the sphere to the collision)
		f32 depth = c.size.x / 2.0f - fabsf(d.x);
		v3 normal;
		normal.x = d.x > 0.0f ? -1.0f : 1.0f;
		normal.y = 0.0f;
		normal.z = 0.0f;

		result->valid = true;
		result->depth = depth;
		glm_vec3_copy(normal.data, result->normal.data);
	}
}

b8
collision_aabbs(aabb bb1, aabb bb2)
{
	b8 collision = true;

	if ((bb1.max.x >= bb2.min.x) && (bb1.min.x <= bb2.max.x))
	{
		if ((bb1.max.y < bb2.min.y) || (bb1.min.y > bb2.max.y)) { collision = false; }
		if ((bb1.max.z < bb2.min.z) || (bb1.min.z > bb2.max.z)) { collision = false; }
	}
	else { collision = false; }

	return (collision);
}
