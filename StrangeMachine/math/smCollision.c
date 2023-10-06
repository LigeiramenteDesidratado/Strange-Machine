#include "core/smBase.h"

#include "math/smCollision.h"
#include "math/smMath.h"
#include "math/smShape.h"

#include "core/smCore.h"

static void
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
collision_capsules(struct capsule a, struct capsule b, struct intersect_result *result)
{
	// struct capsule A:
	vec3 a_norm;
	glm_vec3_sub(a.tip.data, a.base.data, a_norm);
	glm_vec3_normalize(a_norm);

	vec3 a_line_end_offset;
	glm_vec3_scale(a_norm, a.radius, a_line_end_offset);

	vec3 a_a, a_b;
	glm_vec3_add(a.base.data, a_line_end_offset, a_a);
	glm_vec3_sub(a.tip.data, a_line_end_offset, a_b);

	// struct capsule B:
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

	// select best potential endpoint on struct capsule A:
	vec3 a_best_point;
	if (d2 < d0 || d2 < d1 || d3 < d0 || d3 < d1) { glm_vec3_copy(a_b, a_best_point); }
	else { glm_vec3_copy(a_a, a_best_point); }

	// select point on struct capsule B line segment nearest to best potential endpoint
	// on A struct capsule:
	vec3 b_best_point;
	sm__colision_closest_point_on_line_segment(b_a, b_b, a_best_point, b_best_point);

	// now do the same for struct capsule A segment:
	sm__colision_closest_point_on_line_segment(a_a, a_b, b_best_point, a_best_point);

	// Finally, struct sphere collision:
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
collision_sphere_triangle(
    struct sphere s, struct triangle t, transform_component *transform, struct intersect_result *result)
{
	/* vec3 center = s.center; */
	f32 radius = s.radius;

	vec3 v_1, v_2;
	glm_vec3_sub(t.p1.data, t.p0.data, v_1);
	glm_vec3_sub(t.p2.data, t.p0.data, v_2);

	vec3 n;
	glm_vec3_cross(v_1, v_2, n);
	glm_vec3_normalize(n);

	/* sm__assert that the struct triangle is not degenerate. */
	sm__assert(glm_vec3_norm2(n) > 0.0f);

	/* Find the nearest feature on the struct triangle to the struct sphere. */
	vec3 sub;
	glm_vec3_sub(s.center.data, t.p0.data, sub);
	f32 d = glm_vec3_dot(sub, n);

	/* If the center of the struct sphere is farther from the plane of the struct triangle */
	/* than the radius of the struct sphere, then there cannot be an intersection. */
	b8 no_intersection = (d < -radius || d > radius);

	/* Project the center of the struct sphere onto the plane of the struct triangle. */
	vec3 point0;
	glm_vec3_scale(n, d, point0);
	glm_vec3_sub(s.center.data, point0, point0); /* projected struct sphere center on struct triangle plane */

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

	f32 radiussq = radius * radius; // struct sphere radius squared

	// Find the nearest point on each edge.

	// Edge 0,1
	vec3 point1;
	sm__colision_closest_point_on_line_segment(t.p0.data, t.p1.data, s.center.data, point1);

	// If the distance to the center of the struct sphere to the point is less than
	// the radius of the struct sphere then it must intersect.
	vec3 res1;
	glm_vec3_sub(s.center.data, point1, res1);
	f32 distsq = glm_vec3_norm2(res1);
	intersection |= distsq <= radiussq;

	// Edge 1,2
	vec3 point2;
	sm__colision_closest_point_on_line_segment(t.p1.data, t.p2.data, s.center.data, point2);

	// If the distance to the center of the struct sphere to the point is less than
	// the radius of the struct sphere then it must intersect.
	glm_vec3_sub(s.center.data, point2, res1);
	distsq = glm_vec3_norm2(res1);
	intersection |= distsq <= radiussq;

	// Edge 2,0
	vec3 point3;
	sm__colision_closest_point_on_line_segment(t.p2.data, t.p0.data, s.center.data, point3);

	// If the distance to the center of the struct sphere to the point is less than
	// the radius of the struct sphere then it must intersect.
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
			// If the struct sphere center's projection on the struct triangle plane is not
			// within the struct triangle,
			// determine the closest point on struct triangle to the struct sphere center
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
		glm_mat4_inv(transform->matrix.data, inv.data);

		v3 vel;
		glm_mat4_mulv3(inv.data, best_point, 1.0f, vel.data);
		glm_mat4_mulv3(transform->last_matrix.data, vel.data, 1.0f, vel.data);
		glm_vec3_sub(best_point, vel.data, vel.data);

		glm_vec3_copy(vel.data, result->velocity.data);
	}
}

void
collision_capsule_triangle(
    struct capsule c, struct triangle t, transform_component *transform, struct intersect_result *result)
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

	// Compute the plane of the struct triangle (has to be normalized).
	vec3 n, v_1, v_2;
	glm_vec3_sub(t.p1.data, t.p0.data, v_1);
	glm_vec3_sub(t.p2.data, t.p0.data, v_2);
	glm_vec3_cross(v_1, v_2, n);
	glm_vec3_normalize(n);

	// sm__assert that the struct triangle is not degenerate.
	sm__assert(glm_vec3_norm2(n) > 0.0f);

	vec3 reference_point;
	vec3 capsule_normal;
	glm_vec3_sub(b, a, capsule_normal);
	glm_vec3_normalize(capsule_normal);

	if (fabsf(glm_vec3_dot(n, capsule_normal)) < GLM_FLT_EPSILON)
	{
		// struct Capsule line cannot be intersected with struct triangle plane (they are  parallel)
		// In this case, just take a point from struct triangle
		glm_vec3_copy(t.p0.data, reference_point);
	}
	else
	{
		/* Intersect struct capsule line with struct triangle plane: */
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

	// Place a struct sphere on closest point on line segment to intersection:
	v3 center;
	sm__colision_closest_point_on_line_segment(a, b, reference_point, center.data);

	struct sphere sph = {.center = center, .radius = radius};

	collision_sphere_triangle(sph, t, transform, result);
}

// collision_check_spheres - Check if two spheres are colliding.
void
collision_spheres(struct sphere s1, struct sphere s2, struct intersect_result *result)
{
	vec3 d;
	glm_vec3_sub(s1.center.data, s2.center.data, d);

	f32 dist = glm_vec3_norm(d);

	if (dist < s1.radius + s2.radius)
	{
		// calculate the depth of the collision
		// and the normal of the collision
		// (the normal is the direction of the collision)
		// (the depth is the distance from the center of the struct sphere to the collision)
		f32 depth = s1.radius + s2.radius - dist;
		glm_vec3_normalize(d);

		result->valid = true;
		result->depth = depth;
		glm_vec3_copy(d, result->normal.data);
	}
}

void
collision_sphere_cube(struct sphere s, struct cube c, struct intersect_result *result)
{
	v3 d;
	glm_vec3_sub(s.center.data, c.center.data, d.data);

	// check if the struct sphere is inside the struct cube
	if (fabsf(d.x) <= c.size.x / 2.0f && fabsf(d.y) <= c.size.y / 2.0f && fabsf(d.z) <= c.size.z / 2.0f)
	{
		// calculate the depth of the collision
		// and the normal of the collision
		// (the normal is the direction of the collision)
		// (the depth is the distance from the center of the struct sphere to the collision)
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

struct intersect_result
collision_capsule_mesh(struct capsule c, const mesh_component *mesh, transform_component *transform)
{
	struct intersect_result best_result = {0};

	mesh_resource *mesh_ref = mesh->mesh_ref;

	struct aabb c_aabb = shape_get_aabb_capsule(c);

#if SM_DEBUG
	sm__assert(glm_aabb_isvalid(mesh_ref->aabb.data));
	sm__assert(glm_vec3_isvalid(mesh_ref->aabb.min.data));
	sm__assert(glm_vec3_isvalid(mesh_ref->aabb.max.data));
#endif

	struct aabb mesh_aabb;
	glm_aabb_transform(mesh_ref->aabb.data, transform->matrix.data, mesh_aabb.data);

	if (!glm_aabb_aabb(c_aabb.data, mesh_aabb.data)) { return (best_result); }

	// TODO: support for vertex array without indices
	sm__assert(array_len(mesh_ref->indices));
	for (u32 index = 0; index < array_len(mesh_ref->indices); index += 3)
	{
		struct triangle triangle;

		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 0]].data, triangle.p0.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 1]].data, triangle.p1.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 2]].data, triangle.p2.data);

		glm_mat4_mulv3(transform->matrix.data, triangle.p0.data, 1.0f, triangle.p0.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p1.data, 1.0f, triangle.p1.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p2.data, 1.0f, triangle.p2.data);

		struct aabb triangle_aabb = shape_get_aabb_triangle(triangle);
		if (!glm_aabb_aabb(c_aabb.data, triangle_aabb.data)) { continue; }

		struct intersect_result result;
		collision_capsule_triangle(c, triangle, transform, &result);

		if (result.valid)
		{
			if ((!best_result.valid) || (result.depth > best_result.depth)) { best_result = result; }
		}
	}

	return (best_result);
}

struct intersect_result
collision_sphere_mesh(struct sphere s, mesh_component *mesh, transform_component *transform)
{
	struct intersect_result best_result = {0};

	mesh_resource *mesh_ref = mesh->mesh_ref;

#if SM_DEBUG
	sm__assert(glm_aabb_isvalid(mesh_ref->aabb.data));
	sm__assert(glm_vec3_isvalid(mesh_ref->aabb.min.data));
	sm__assert(glm_vec3_isvalid(mesh_ref->aabb.max.data));
#endif

	struct aabb mesh_aabb;
	glm_aabb_transform(mesh_ref->aabb.data, transform->matrix.data, mesh_aabb.data);

	struct aabb s_aabb = shape_get_aabb_sphere(s);
	if (!glm_aabb_aabb(s_aabb.data, mesh_aabb.data)) { return (best_result); }

	// TODO: support for vertex array without indices
	sm__assert(array_len(mesh_ref->indices));
	for (u32 index = 0; index < array_len(mesh_ref->indices); index += 3)
	{
		struct triangle triangle;

		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 0]].data, triangle.p0.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 1]].data, triangle.p1.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 2]].data, triangle.p2.data);

		glm_mat4_mulv3(transform->matrix.data, triangle.p0.data, 1.0f, triangle.p0.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p1.data, 1.0f, triangle.p1.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p2.data, 1.0f, triangle.p2.data);

		struct aabb triangle_aabb = shape_get_aabb_triangle(triangle);
		if (!glm_aabb_aabb(s_aabb.data, triangle_aabb.data)) { continue; }

		struct intersect_result result;
		collision_sphere_triangle(s, triangle, transform, &result);

		if (result.valid && result.depth > best_result.depth) { best_result = result; }
	}

	return (best_result);
}

struct intersect_result
collision_ray_triangle(struct ray ray, struct triangle triangle)
{
	struct intersect_result result = {0};

	v3 edge1 = {0};
	v3 edge2 = {0};
	v3 p, q, tv;
	f32 det, inv_det, u, v, t;

	// Find vectors for two edges sharing V0
	glm_vec3_sub(triangle.p1.data, triangle.p0.data, edge1.data);
	glm_vec3_sub(triangle.p2.data, triangle.p0.data, edge2.data);

	// Begin calculating determinant - also used to calculate u parameter
	glm_vec3_cross(ray.direction.data, edge2.data, p.data);

	// If determinant is near zero, ray lies in plane of triangle or ray is parallel to plane of triangle
	det = glm_vec3_dot(edge1.data, p.data);

	// Avoid culling!
	if ((det > -GLM_FLT_EPSILON) && (det < GLM_FLT_EPSILON)) { return (result); }

	inv_det = 1.0f / det;

	// Calculate distance from V1 to ray origin
	glm_vec3_sub(ray.position.data, triangle.p0.data, tv.data);

	// Calculate u parameter and test bound
	u = glm_vec3_dot(tv.data, p.data) * inv_det;

	// The intersection lies outside the triangle
	if ((u < 0.0f) || (u > 1.0f)) { return (result); }

	// Prepare to test v parameter
	glm_vec3_cross(tv.data, edge1.data, q.data);

	// Calculate V parameter and test bound
	v = glm_vec3_dot(ray.direction.data, q.data) * inv_det;

	// The intersection lies outside the triangle
	if ((v < 0.0f) || ((u + v) > 1.0f)) { return (result); }

	t = glm_vec3_dot(edge2.data, q.data) * inv_det;

	if (t > GLM_FLT_EPSILON)
	{
		// Ray hit, get hit point and normal
		result.valid = true;
		result.depth = t;

		v3 normal;
		glm_vec3_cross(edge1.data, edge2.data, normal.data);
		glm_vec3_normalize(normal.data);
		glm_vec3_copy(normal.data, result.normal.data);

		v3 pos;
		glm_vec3_scale(ray.direction.data, t, pos.data);
		glm_vec3_add(ray.position.data, pos.data, pos.data);
		glm_vec3_copy(pos.data, result.position.data);
	}

	return (result);
}

struct intersect_result
collision_ray_aabb(struct ray ray, struct aabb aabb)
{
	struct intersect_result result;

	b8 inside_aabb = glm_aabb_point(aabb.data, ray.position.data);
	if (inside_aabb) { glm_vec3_inv(ray.direction.data); }

	float t[11] = {0};

	t[8] = 1.0f / ray.direction.x;
	t[9] = 1.0f / ray.direction.y;
	t[10] = 1.0f / ray.direction.z;

	t[0] = (aabb.min.x - ray.position.x) * t[8];
	t[1] = (aabb.max.x - ray.position.x) * t[8];
	t[2] = (aabb.min.y - ray.position.y) * t[9];
	t[3] = (aabb.max.y - ray.position.y) * t[9];
	t[4] = (aabb.min.z - ray.position.z) * t[10];
	t[5] = (aabb.max.z - ray.position.z) * t[10];
	t[6] = (f32)fmaxf(fmaxf(fminf(t[0], t[1]), fminf(t[2], t[3])), fminf(t[4], t[5]));
	t[7] = (f32)fminf(fminf(fmaxf(t[0], t[1]), fmaxf(t[2], t[3])), fmaxf(t[4], t[5]));

	result.valid = !((t[7] < 0) || (t[6] > t[7]));
	result.depth = t[6];
	v3 pos;
	glm_vec3_scale(ray.direction.data, result.depth, pos.data);
	glm_vec3_add(ray.position.data, pos.data, result.position.data);

	// Get box center point
	glm_vec3_lerp(aabb.min.data, aabb.max.data, 0.5f, result.normal.data);
	// Get vector center point->hit point
	glm_vec3_sub(result.position.data, result.normal.data, result.normal.data);
	// Scale vector to unit cube
	// NOTE: We use an additional .01 to fix numerical errors
	glm_vec3_scale(result.normal.data, 2.01f, result.normal.data);
	v3 diff;
	glm_vec3_sub(aabb.max.data, aabb.min.data, diff.data);
	glm_vec3_div(result.normal.data, diff.data, result.normal.data);
	// The relevant elements of the vector are now slightly larger than 1.0f (or smaller than -1.0f)
	// and the others are somewhere between -1.0 and 1.0 casting to int is exactly our wanted normal!
	result.normal.x = (f32)((i32)result.normal.x);
	result.normal.y = (f32)((i32)result.normal.y);
	result.normal.z = (f32)((i32)result.normal.z);

	glm_vec3_normalize(result.normal.data);

	if (inside_aabb)
	{
		result.depth *= -1.0f;
		glm_vec3_inv(result.normal.data);
	}

	return (result);
}

struct intersect_result
collision_ray_mesh(struct ray ray, mesh_component *mesh, transform_component *transform)
{
	struct intersect_result best_result = {0};

	const mesh_resource *mesh_ref = mesh->mesh_ref;
	struct aabb mesh_aabb = mesh_ref->aabb;

	glm_aabb_transform(mesh_aabb.data, transform->matrix.data, mesh_aabb.data);

	struct intersect_result ray_aabb_result = collision_ray_aabb(ray, mesh_aabb);
	if (!ray_aabb_result.valid) { return (best_result); }

	sm__assert(array_len(mesh_ref->indices));
	for (u32 index = 0; index < array_len(mesh_ref->indices); index += 3)
	{
		struct triangle triangle;

		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 0]].data, triangle.p0.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 1]].data, triangle.p1.data);
		glm_vec3_copy(mesh_ref->positions[mesh_ref->indices[index + 2]].data, triangle.p2.data);

		glm_mat4_mulv3(transform->matrix.data, triangle.p0.data, 1.0f, triangle.p0.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p1.data, 1.0f, triangle.p1.data);
		glm_mat4_mulv3(transform->matrix.data, triangle.p2.data, 1.0f, triangle.p2.data);

		// TODO: check against the triangle AABB?
		// struct aabb triangle_aabb = shape_get_aabb_triangle(triangle);
		// if (!collision_aabbs(s_aabb, triangle_aabb)) { continue; }

		struct intersect_result result = collision_ray_triangle(ray, triangle);

		if (result.valid)
		{
			// save the closest hit triangle
			if (!best_result.valid || (best_result.depth > result.depth)) { best_result = result; }
		}
	}

	return (best_result);
}
