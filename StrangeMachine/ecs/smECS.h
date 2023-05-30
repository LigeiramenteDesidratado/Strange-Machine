#ifndef SM_ECS_H
#define SM_ECS_H

#include "cglm/vec3.h"
#include "core/smArray.h"
#include "core/smBase.h"
#include "core/smHandlePool.h"
#include "core/smResource.h"
#include "core/smString.h"
#include "math/smMath.h"

typedef struct entity
{
	handle_t handle;
} entity_t;

typedef u64 component_t;

struct component_pool;

struct component_view
{
	component_t id;

	u32 size;
	u32 offset;
};

struct component_pool
{
	component_t archetype;

	struct handle_pool handle_pool;
	struct component_view view[64];

	u32 size; // size of each element
	u32 cap;
	u8 *data;
};

void component_pool_make(struct arena *arena, struct component_pool *comp_pool, u32 capacity, component_t archetype);
void component_pool_release(struct arena *arena, struct component_pool *comp_pool);

// Loop through all valid components and decrement the reference counter.
// Useful when you want to clear the arena but don't want to waste CPU cycles freeing each component individually
void component_pool_unmake_refs(struct component_pool *comp_pool);

handle_t component_pool_handle_new(struct arena *arena, struct component_pool *comp_pool);
void component_pool_handle_remove(struct component_pool *comp_pool, handle_t handl);
void *component_pool_get_data(struct component_pool *comp_pool, handle_t handle, component_t component);
b8 component_pool_handle_is_valid(struct component_pool *comp_pool, handle_t handle);

b8 component_has_ref_counter(component_t component);

// void ecs_manager_print_archeype(entity_t entity);
// const i8 *ecs_managr_get_archetype_string(component_t component);

struct system_iter
{
	u32 index;
	struct component_pool *comp_pool_ref;
};

struct system_iter system_iter_begin(struct component_pool *comp_pool);
b8 system_iter_next(struct system_iter *iter);
void *system_iter_get_component(struct system_iter *iter, component_t component);

extern struct components_info
{
	str8 name;
	component_t id;
	u32 size;
	b8 has_ref_counter;
} ctable_components[64];

#define TRANSFORM	      BIT64(0)
#define WORLD		      BIT64(1)
#define MATERIAL	      BIT64(2)
#define CAMERA		      BIT64(3)
#define MESH		      BIT64(4)
#define HIERARCHY	      BIT64(5)
#define RIGID_BODY	      BIT64(6)
#define STATIC_BODY	      BIT64(7)
#define ARMATURE	      BIT64(8)
#define POSE		      BIT64(9)
#define CLIP		      BIT64(10)
#define CROSS_FADE_CONTROLLER BIT64(11)

#define PLAYER BIT64(12) // TODO

typedef struct transform
{
	v4 position;
	v4 rotation;
	v3 scale;

#define TRANSFORM_FLAG_NONE  0
#define TRANSFORM_FLAG_DIRTY BIT(0)
	u32 flags;
} transform_component;

#define transform_identity() \
	((struct transform){.position = v4_zero(), .rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f), .scale = v3_one()})
#define transform_print(T)                                                                                           \
	(printf("%s\n\tposition: %f, %f, %f\n\trotation: %f, %f, %f, %f\n\tscale: %f, %f, %f\n", #T, (T).position.x, \
	    (T).position.y, (T).position.z, (T).rotation.x, (T).rotation.y, (T).rotation.z, (T).rotation.w,          \
	    (T).scale.x, (T).scale.y, (T).scale.z))

sm__force_inline b8
transform_is_dirty(const transform_component *transform)
{
	return (transform->flags & TRANSFORM_FLAG_DIRTY);
}

sm__force_inline void
transform_set_dirty(transform_component *transform, b8 dirty)
{
	if (dirty) transform->flags |= TRANSFORM_FLAG_DIRTY;
	else transform->flags &= ~(u32)TRANSFORM_FLAG_DIRTY;
}

sm__force_inline m4
transform_to_m4(transform_component transform)
{
	m4 result;

	/* first, extract the rotation basis of the transform */
	versor q = {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w};

	v4 x, y, z;
	glm_quat_rotatev(q, v3_right().data, x.data);
	glm_quat_rotatev(q, v3_up().data, y.data);
	glm_quat_rotatev(q, v3_forward().data, z.data);

	/* next, scale the basis vectors */
	glm_vec4_scale(x.data, transform.scale.x, x.data);
	glm_vec4_scale(y.data, transform.scale.y, y.data);
	glm_vec4_scale(z.data, transform.scale.z, z.data);

	result = m4_new(x.x, x.y, x.z, 0.0f,					   // X basis (and scale)
	    y.x, y.y, y.z, 0.0f,						   // Y basis (and scale)
	    z.x, z.y, z.z, 0.0f,						   // Z basis (and scale)
	    transform.position.x, transform.position.y, transform.position.z, 1.0f // Position
	);

	return (result);
}

sm__force_inline transform_component
transform_from_m4(m4 m)
{
	struct transform result = {0};
	glm_vec3_copy(m.vec3.position.data, result.position.data); // rotation = first column of mat
	result.rotation = m4_to_quat(m);

	/* get the rotate scale matrix, then estimate the scale from that */
	m4 rot_scale_mat;
	glm_mat4_identity(rot_scale_mat.data);

	rot_scale_mat = m4_new(m.float16[0], m.float16[1], m.float16[2], 0, m.float16[4], m.float16[5], m.float16[6], 0,
	    m.float16[8], m.float16[9], m.float16[10], 0, 0, 0, 0, 1);

	versor q;
	glm_quat_inv(result.rotation.data, q);
	m4 inv_rot_mat;
	glm_quat_mat4(q, inv_rot_mat.data);
	m4 scale_mat;
	glm_mat4_mul(rot_scale_mat.data, inv_rot_mat.data, scale_mat.data);

	/* the diagonal of the scale matrix is the scale */
	result.scale.data[0] = scale_mat.c0r0;
	result.scale.data[1] = scale_mat.c1r1;
	result.scale.data[2] = scale_mat.c2r2;

	/* out.position.data[3] = 1.0f; */
	/* out.scale.data[3] = 1.0f; */

	return (result);
}

sm__force_inline transform_component
transform_combine(transform_component a, transform_component b)
{
	transform_component result;

	glm_vec3_mul(a.scale.data, b.scale.data, result.scale.data);
	glm_quat_mul(a.rotation.data, b.rotation.data, result.rotation.data);

	v3 pos;
	glm_vec3_mul(a.scale.data, b.position.v3.data, pos.data);
	glm_quat_rotatev(a.rotation.data, pos.data, result.position.data);
	glm_vec3_add(a.position.data, result.position.data, result.position.data);

	transform_set_dirty(&result, true);

	return (result);
}

sm__force_inline transform_component
transform_mix(transform_component a, transform_component b, f32 t)
{
	transform_component result;

	glm_vec3_lerp(a.position.v3.data, b.position.v3.data, t, result.position.v3.data);
	glm_quat_nlerp(a.rotation.data, b.rotation.data, t, result.rotation.data);
	glm_vec3_lerp(a.scale.data, b.scale.data, t, result.scale.data);

	return (result);
}

sm__force_inline transform_component
transform_inverse(transform_component t)
{
	transform_component result;

	glm_quat_inv(t.rotation.data, result.rotation.data);

	result.scale.x = fabsf(t.scale.x) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.x;
	result.scale.y = fabsf(t.scale.y) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.y;
	result.scale.z = fabsf(t.scale.z) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.z;

	v3 inv_trans;
	glm_vec3_scale(t.position.data, -1.0f, inv_trans.data);
	glm_vec3_mul(result.scale.data, inv_trans.data, inv_trans.data);
	glm_quat_rotatev(result.rotation.data, inv_trans.data, result.position.v3.data);

	return (result);
}

typedef struct world
{
	m4 matrix;
	m4 last_matrix;
} world_component;

sm__force_inline void
world_store_matrix(world_component *world, m4 new_world)
{
	glm_mat4_ucopy(world->matrix.data, world->last_matrix.data);
	glm_mat4_ucopy(new_world.data, world->matrix.data);
}

typedef struct material
{
	material_resource *material_ref;
	struct resource *resource_ref;

} material_component;

typedef struct camera
{
	v3 position;
	f32 target_distance;
	v3 target;
	v3 front;

	v3 angle;

	f32 fov;
	f32 aspect_ratio;
	f32 move_speed;
	f32 sensitive;

#define CAMERA_FLAG_PERSPECTIVE	 BIT(0)
#define CAMERA_FLAG_ORTHOGONAL	 BIT(1)
#define CAMERA_FLAG_FREE	 BIT(2)
#define CAMERA_FLAG_THIRD_PERSON BIT(3)
#define CAMERA_FLAG_CUSTOM	 BIT(4)
	u32 flags;

} camera_component;

sm__force_inline m4
camera_get_projection(const camera_component *camera)
{
	m4 result;
	glm_perspective(glm_rad(camera->fov), camera->aspect_ratio, 0.005f, 100.0f, result.data);

	return (result);
}

sm__force_inline m4
camera_get_view(camera_component *camera)
{
	m4 result;

	glm_lookat(camera->position.data, camera->target.data, v3_up().data, result.data);

	return (result);
}

typedef struct mesh
{
	struct resource *resource_ref;
	mesh_resource *mesh_ref;
} mesh_component;

sm__force_inline void
mesh_calculate_aabb(mesh_component *mesh)
{
	// get min and max vertex to construct bounds (AABB)
	v3 min_vert;
	v3 max_vert;

	if (mesh->mesh_ref->positions != 0)
	{
		glm_vec3_copy(mesh->mesh_ref->positions[0].data, min_vert.data);
		glm_vec3_copy(mesh->mesh_ref->positions[0].data, max_vert.data);

		for (u32 i = 1; i < array_len(mesh->mesh_ref->positions); ++i)
		{
			glm_vec3_minv(min_vert.data, mesh->mesh_ref->positions[i].data, min_vert.data);
			glm_vec3_maxv(max_vert.data, mesh->mesh_ref->positions[i].data, max_vert.data);
		}
	}

	// create the bounding box
	aabb axis_aligned_bb;
	glm_vec3_copy(min_vert.data, axis_aligned_bb.min.data);
	glm_vec3_copy(max_vert.data, axis_aligned_bb.max.data);

	mesh->mesh_ref->aabb = axis_aligned_bb;
}

typedef struct hierarchy
{
	component_t archetype;
	entity_t parent;
} hierarchy_component;

typedef struct rigid_body
{
#define RIGID_BODY_COLLISION_SHAPE_SPHEPE  BIT(0)
#define RIGID_BODY_COLLISION_SHAPE_CAPSULE BIT(1)

	u32 collision_shape;

	union
	{
		sphere sphere;
		capsule capsule;
	};

	v3 velocity;
	b8 has_gravity;
	v3 gravity;

} rigid_body_component;

typedef struct static_body
{
	b8 enabled;
} static_body_component;

typedef struct armature
{
	armature_resource *armature_ref;
	struct resource *resource_ref;
} armature_component;

typedef struct pose pose_component;

typedef struct clip
{
	clip_resource *current_clip_ref;
	clip_resource *next_clip_ref;
	f32 time;
} clip_component;

struct cross_fade_target
{
	clip_resource *clip_ref;
	pose_component *pose_ref;
	f32 time;
	f32 duration;
	f32 elapsed;
};

typedef struct cross_fade_controller
{
	array(struct cross_fade_target) targets;

} cross_fade_controller_component;

typedef struct player
{
	u32 state;
} player_component;

#endif // SM_ECS_H
