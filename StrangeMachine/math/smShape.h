#ifndef SM_MATH_SHAPES_H
#define SM_MATH_SHAPES_H

#include "core/smArray.h"
#include "core/smCore.h"

#include "math/smMath.h"

typedef struct sm__sphere_s
{
	v3 center;
	f32 radius;
} sphere;

typedef struct sm__triangle_s
{
	v3 p0;
	v3 p1;
	v3 p2;
} triangle;

typedef struct sm__capsule_s
{
	v3 base;
	v3 tip;
	f32 radius;
} capsule;

typedef struct sm__bounding_box_s
{
	v3 min;
	v3 max;
} aabb;

// cube
typedef struct sm__cube_s
{
	v3 center;
	v3 size;
} cube;

typedef union sm__shape_u
{
	cube cube;
	capsule capsule;
	sphere sphere;
	triangle triangle;
	void *mesh_ref;
} shape;

capsule shape_capsule_new(sphere s, f32 height);
aabb shape_get_aabb_sphere(sphere s);
aabb shape_get_aabb_capsule(capsule c);
aabb shape_get_aabb_triangle(triangle t);
aabb shape_get_positions_aabb(array(v3) positions);

#endif // SM_SHAPES_H
