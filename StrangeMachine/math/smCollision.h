#ifndef SM_MATH_COLLISION_H
#define SM_MATH_COLLISION_H

#include "core/smCore.h"

// #include "resource/smResource.h"
#include "ecs/smECS.h"
#include "math/smShape.h"

typedef struct sm__intersect_result_s
{
	b8 valid;
	v3 position, normal, velocity;
	f32 depth;
} intersect_result;

void collision_capsules(capsule a, capsule b, intersect_result *result);
void collision_sphere_triangle(sphere s, triangle t, world_component *world, intersect_result *result);
/* void sm_collision_capsule_triangle(capsule c, triangle t, sm_intersect_result_s *result); */
void collision_capsule_triangle(capsule c, triangle t, world_component *world, intersect_result *result);
void collision_spheres(sphere a, sphere b, intersect_result *result);
void sm_collision_capsule_mesh(capsule c, const mesh_component *mesh, intersect_result *result);
void collision_sphere_mesh(sphere s, mesh_component *mesh, intersect_result *result);
b8 collision_aabbs(aabb bb1, aabb bb2);

#endif // SM_MATH_COLLISION_H
