#ifndef SM_MATH_COLLISION_H
#define SM_MATH_COLLISION_H

#include "core/smCore.h"

#include "ecs/smECS.h"
#include "math/smShape.h"

struct intersect_result
{
	b8 valid;
	v3 position, normal, velocity;
	f32 depth;
};

void collision_capsules(struct capsule a, struct capsule b, struct intersect_result *result);
void collision_sphere_triangle(
    struct sphere s, struct triangle t, transform_component *transform, struct intersect_result *result);
void collision_capsule_triangle(
    struct capsule c, struct triangle t, transform_component *transform, struct intersect_result *result);
void collision_spheres(struct sphere a, struct sphere b, struct intersect_result *result);
struct intersect_result collision_capsule_mesh(
    struct capsule c, const mesh_component *mesh, transform_component *transform);
struct intersect_result collision_sphere_mesh(struct sphere s, mesh_component *mesh, transform_component *transform);
void collision_sphere_cube(struct sphere s, struct cube c, struct intersect_result *result);

struct intersect_result collision_ray_triangle(struct ray ray, struct triangle triangle);
struct intersect_result collision_ray_mesh(struct ray ray, mesh_component *mesh, transform_component *transform);
struct intersect_result collision_ray_aabb(struct ray ray, struct aabb aabb);

#endif // SM_MATH_COLLISION_H
