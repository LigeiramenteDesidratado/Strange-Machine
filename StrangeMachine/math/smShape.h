#ifndef SM_MATH_SHAPES_H
#define SM_MATH_SHAPES_H

#include "core/smCore.h"

#include "math/smMath.h"

struct triangle
{
	v3 p0;
	v3 p1;
	v3 p2;
};

struct sphere
{
	v3 center;
	f32 radius;
};

struct capsule
{
	v3 base;
	v3 tip;
	f32 radius;
};

struct aabb
{
	union
	{
		struct
		{
			v3 min;
			v3 max;
		};

		vec3 data[2];
	};
};

struct cube
{
	v3 center;
	v3 size;
};

struct ray
{
	v3 position; // origin
	v3 direction;
};

typedef union sm__shape_u
{
	struct triangle triangle;
	struct sphere sphere;
	struct capsule capsule;
	struct cube cube;
	struct ray ray;
	void *mesh_ref;
} shape;

struct capsule shape_capsule_new(struct sphere s, f32 height);
struct aabb shape_get_aabb_sphere(struct sphere s);
struct aabb shape_get_aabb_capsule(struct capsule c);
struct aabb shape_get_aabb_triangle(struct triangle t);
struct aabb shape_get_positions_aabb(array(v3) positions);

#endif // SM_SHAPES_H
