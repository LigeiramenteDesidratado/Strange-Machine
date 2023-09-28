#ifndef SM_ECS_H
#define SM_ECS_H

#include "core/smArray.h"
#include "core/smBase.h"
#include "core/smHandlePool.h"
#include "core/smLog.h"
#include "core/smResource.h"
#include "core/smString.h"
#include "math/smMath.h"
#include "particle/smParticle.h"

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

void ecs_manager_print_archeype(struct arena *arena, component_t archetype);

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
#define MATERIAL	      BIT64(1)
#define CAMERA		      BIT64(2)
#define MESH		      BIT64(3)
#define RIGID_BODY	      BIT64(4)
#define STATIC_BODY	      BIT64(5)
#define ARMATURE	      BIT64(6)
#define POSE		      BIT64(7)
#define CLIP		      BIT64(8)
#define CROSS_FADE_CONTROLLER BIT64(9)
#define PLAYER		      BIT64(10) // TODO
#define PARTICLE_EMITTER      BIT64(11)

typedef struct transform
{
	// Local
	trs transform_local;
	m4 matrix_local;

	// Global
	m4 matrix;
	m4 last_matrix;

	// Hierarchy
	REF(struct transform) parent_transform;
	array(REF(struct transform)) chidren_transform;

	component_t archetype;
	entity_t self;

	enum
	{
		TRANSFORM_FLAG_NONE = 0,
		TRANSFORM_FLAG_DIRTY = BIT(0),

		// enforce 32-bit size enum
		SM__TRANSFORM_FLAG_ENFORCE_ENUM_SIZE = 0x7fffffff
	} flags;

} transform_component;

void transform_update_tree(transform_component *self);
b8 transform_is_descendant_of(transform_component *transform, transform_component *desc);

sm__force_inline void
transform_init(transform_component *transform, entity_t self, component_t archetype)
{
	transform->matrix_local = m4_identity();

	transform->matrix = m4_identity();
	transform->last_matrix = m4_identity();

	transform->parent_transform = 0;
	transform->chidren_transform = 0;

	transform->archetype = archetype;
	transform->self = self;

	transform->transform_local = trs_identity();

	transform_update_tree(transform);

	transform->flags = TRANSFORM_FLAG_DIRTY;
}

sm__force_inline void
transform_set_dirty(transform_component *transform, b8 dirty)
{
	if (dirty) { transform->flags |= TRANSFORM_FLAG_DIRTY; }
	else { transform->flags &= ~(u32)TRANSFORM_FLAG_DIRTY; }
}

sm__force_inline b8
transform_is_dirty(transform_component *transform)
{
	b8 result;

	result = (transform->flags & TRANSFORM_FLAG_DIRTY);

	return (result);
}

sm__force_inline void
transform_set_parent(struct arena *arena, transform_component *self, transform_component *new_parent)
{
	if (self->parent_transform == new_parent) { return; }
	if (self == new_parent)
	{
		log_warn(str8_from("adding self as parent"));
		return;
	}

	if (new_parent && transform_is_descendant_of(new_parent, self))
	{
		for (u32 i = 0; i < array_len(self->chidren_transform); ++i)
		{
			self->chidren_transform[i]->parent_transform = self->parent_transform;
		}

		array_set_len(arena, self->chidren_transform, 0);
	}

	if (self->parent_transform)
	{
		transform_component *m_parent = self->parent_transform;
		i32 self_index = -1;
		for (u32 i = 0; i < array_len(m_parent->chidren_transform); ++i)
		{
			if (self == m_parent->chidren_transform[i]) { self_index = i; }
		}
		assert(self_index != -1);

		array_del(m_parent->chidren_transform, self_index, 1);
	}

	if (new_parent)
	{
		b8 is_child = false;
		for (u32 i = 0; i < array_len(new_parent->chidren_transform); ++i)
		{
			if (new_parent->chidren_transform[i] == self)
			{
				is_child = true;
				break;
			}
		}
		if (!is_child) { array_push(arena, new_parent->chidren_transform, self); }
	}

	self->parent_transform = new_parent;

	new_parent->flags |= TRANSFORM_FLAG_DIRTY;
	self->parent_transform->flags |= TRANSFORM_FLAG_DIRTY;
}

sm__force_inline void
transform_add_child(struct arena *arena, transform_component *self, transform_component *child)
{
	transform_set_parent(arena, child, self);
}

sm__force_inline void
transform_set_position_local(transform_component *transform, v3 position)
{
	if (glm_vec3_eqv(transform->transform_local.translation.data, position.data)) { return; }

	glm_vec3_copy(position.data, transform->transform_local.translation.data);

	transform_update_tree(transform);
}

sm__force_inline void
transform_set_position(transform_component *transform, v3 position)
{
	if (!transform->parent_transform) { transform_set_position_local(transform, position); }
	else
	{
		m4 inv;
		glm_mat4_inv(transform->parent_transform->matrix.data, inv.data);
		position = m4_v3(inv, position);
		// glm_mat4_mulv3(inv.data, position.data, 1.0f, position.data);

		transform_set_position_local(transform, position);
	}
}

sm__force_inline void
transform_set_rotation_local(transform_component *transform, v4 rotation)
{
	if (glm_vec4_eqv(transform->transform_local.rotation.data, rotation.data)) { return; }

	glm_vec4_copy(rotation.data, transform->transform_local.rotation.data);

	transform_update_tree(transform);
}

sm__force_inline void
transform_set_rotation(transform_component *transform, v4 rotation)
{
	if (!transform->parent_transform) { transform_set_rotation_local(transform, rotation); }
	else
	{
		v4 q;
		glm_mat4_quat(transform->parent_transform->matrix.data, q.data);
		glm_quat_inv(q.data, q.data);

		glm_quat_mul(rotation.data, q.data, rotation.data);

		transform_set_rotation_local(transform, rotation);
	}
}

sm__force_inline void
transform_set_scale_local(transform_component *transform, v3 scale)
{
	if (glm_vec3_eqv(transform->transform_local.scale.data, scale.data)) { return; }

	scale.x = (scale.x == 0.0f) ? GLM_FLT_EPSILON : scale.x;
	scale.y = (scale.y == 0.0f) ? GLM_FLT_EPSILON : scale.y;
	scale.z = (scale.z == 0.0f) ? GLM_FLT_EPSILON : scale.z;

	glm_vec3_copy(scale.data, transform->transform_local.scale.data);

	transform_update_tree(transform);
}

sm__force_inline void
transform_translate(transform_component *transform, v3 delta)
{
	if (!transform->parent_transform)
	{
		glm_vec3_add(transform->transform_local.translation.data, delta.data, delta.data);
		transform_set_position_local(transform, delta);
	}
	else
	{
		m4 inv;
		glm_mat4_inv(transform->parent_transform->matrix.data, inv.data);
		delta = m4_v3(inv, delta);
		glm_vec3_add(transform->transform_local.translation.data, delta.data, delta.data);

		transform_set_position_local(transform, delta);
	}
}

sm__force_inline void
transform_rotate(transform_component *transform, v4 delta)
{
	if (!transform->parent_transform)
	{
		v4 q;
		glm_quat_mul(transform->transform_local.rotation.data, delta.data, q.data);
		glm_quat_normalize(q.data);

		transform_set_rotation_local(transform, q);
	}
	else
	{
		v4 inv, q;

		m4 rotation_matrix;
		v3 discard;
		glm_decompose_rs(transform->matrix.data, rotation_matrix.data, discard.data);
		glm_mat4_quat(rotation_matrix.data, q.data);

		glm_quat_inv(q.data, inv.data);
		glm_quat_mul(transform->transform_local.rotation.data, inv.data, inv.data);
		glm_quat_mul(inv.data, delta.data, delta.data);
		glm_quat_mul(delta.data, q.data, q.data);

		transform_set_rotation_local(transform, q);
	}
}

sm__force_inline v4
transform_get_rotation(transform_component *transform)
{
	v4 result;

	m4 am;
	v3 discard;
	glm_decompose_rs(transform->matrix.data, am.data, discard.data);
	glm_mat4_quat(am.data, result.data);

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
	f32 z_near, z_far;
	f32 fovx;	  // vertical field of view in radians
	f32 aspect_ratio; // width/height

	m4 view;
	m4 projection;
	m4 view_projection;

	struct
	{
		f32 movement_scroll_accumulator;
		v3 speed;

		v2 mouse_smoothed;
		v2 first_person_rotation;

		b8 is_controlled_by_keyboard_mouse;
		v2 mouse_last_position;

		entity_t focus_entity;
		v3 lerp_to_target_position;
		v4 lerp_to_target_rotation;
		f32 lerp_to_target_distance;
		f32 lerp_to_target_alpha;

		b8 lerp_to_target_p;
		b8 lerp_to_target_r;
	} free;

	struct
	{
		v2 mouse_smoothed;
		v2 rotation;

		f32 target_distance;
		v3 target;
	} third_person;

	enum
	{
		CAMERA_FLAG_PERSPECTIVE = BIT(0),
		CAMERA_FLAG_ORTHOGONAL = BIT(1),
		CAMERA_FLAG_FREE = BIT(2),
		CAMERA_FLAG_THIRD_PERSON = BIT(3),
		CAMERA_FLAG_CUSTOM = BIT(4),
		//
		// enforce 32-bit size enum
		SM2__CAMERA_FLAG_ENFORCE_ENUM_SIZE = 0x7fffffff
	} flags;

} camera_component;

sm__force_inline m4
camera_get_projection2(const camera_component *camera)
{
	m4 result = camera->projection;

	return (result);
}

sm__force_inline m4
camera_get_view2(camera_component *camera)
{
	m4 result = camera->view;

	return (result);
}

sm__force_inline f32
camera_get_fov_x(camera_component *camera)
{
	return camera->fovx;
}

sm__force_inline f32
camera_get_fov_y(camera_component *camera)
{
	f32 result;

	result = 2.0f * atanf(tanf(camera->fovx * 0.5f) *
			      ((f32)core_get_framebuffer_height() / (f32)core_get_framebuffer_width()));

	return (result);
}

sm__force_inline v3
camera_screen_to_world(camera_component *camera, v2 position_window, v4 viewport)
{
	v3 result;
#if 0

	v3 position = v3_new(position_window.x, viewport.w - (position_window.y), 1.0f);
	glm_unproject(position.data, camera->view_projection.data, viewport.data, result.data);

#elif 1

	// A non reverse-z projection matrix is need, we create it
	m4 projection;
	glm_perspective(camera_get_fov_x(camera), camera->aspect_ratio, camera->z_near, camera->z_far, projection.data);

	// Convert screen space position to clip space position
	v4 position_clip;
	position_clip.x = (position_window.x / viewport.z) * 2.0f - 1.0f;
	position_clip.y = (position_window.y / viewport.w) * -2.0f + 1.0f;
	position_clip.z = -1.0f;
	position_clip.w = 1.0f;
	// position_clip.x = 2 * (position_screen.x + 0.5f) / viewport.z - 1.0f;
	// position_clip.y = 1.0f - 2.0f * (position_screen.y + 0.5f) / viewport.w;
	// position_clip.z = 1.0f;
	// position_clip.w = 1.0f;

	// Compute world space position
	m4 view_projection_inverted;
	glm_mat4_mul(projection.data, camera->view.data, view_projection_inverted.data);
	glm_mat4_inv(view_projection_inverted.data, view_projection_inverted.data);
	// glm_mat4_inv(camera->view_projection.data, view_projection_inverted.data);

	glm_mat4_mulv(view_projection_inverted.data, position_clip.data, position_clip.data);

	glm_vec3_divs(position_clip.v3.data, position_clip.w, result.data);

#else

	v3 position = v3_new(position_screen.x, viewport.w - position_screen.y, 1.0f);
	m4 projection = CreatePerspectiveFieldOfViewLH(
	    camera_get_fov_y(camera), camera->aspect_ratio, camera->z_near, camera->z_far); // reverse-z
	v4 position_clip;
	position_clip.x = (position.x / viewport.z) * 2.0f - 1.0f;
	position_clip.y = (position.y / viewport.w) * -2.0f + 1.0f;
	position_clip.z = -1.0f;
	position_clip.w = 1.0f;

	// Compute world space position
	m4 view_projection_inverted;
	glm_mat4_mul(camera->view.data, projection.data, view_projection_inverted.data);
	glm_mat4_inv(view_projection_inverted.data, view_projection_inverted.data);

	glm_mat4_mulv(view_projection_inverted.data, position_clip.data, position_clip.data);

	glm_vec3_divs(position_clip.v3.data, position_clip.w, result.data);

#endif
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
	// get min and max vertex to construct bounds (struct AABB)
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
	struct aabb axis_aligned_bb;
	glm_vec3_copy(min_vert.data, axis_aligned_bb.min.data);
	glm_vec3_copy(max_vert.data, axis_aligned_bb.max.data);

	mesh->mesh_ref->aabb = axis_aligned_bb;
}

typedef struct rigid_body
{
	enum
	{
		RB_SHAPE_NONE = 0,
		RB_SHAPE_SPHERE = BIT(0),
		RB_SHAPE_CAPSULE = BIT(1),

		// enforce 32-bit size enum
		SM__RB_SHAPE_ENFORCE_ENUM_SIZE = 0x7fffffff
	} collision_shape;

	union
	{
		struct sphere sphere;
		struct capsule capsule;
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

enum emission_shape
{
	EMISSION_SHAPE_NONE = 0,
	EMISSION_SHAPE_AABB,
	EMISSION_SHAPE_CUBE,

	// enforce 32-bit size enum
	SM__EMISSION_SHAPE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

typedef struct particle_emitter
{
	struct particle free_sentinel;
	struct particle active_sentinel;

	u32 pool_size;
	struct particle *particles_pool;

	b8 enable;
	u32 emission_rate;

	enum emission_shape shape_type;

	union
	{
		struct aabb box;
		trs cube;
	};

	enum
	{
		EMITT_FROM_VOLUME,
		EMITT_FROM_SHELL,
	} emitt_from;

} particle_emitter_component;

sm__force_inline void
particle_emitter_init(struct arena *arena, struct particle_emitter *emitter, u32 n_particles)
{
	emitter->pool_size = n_particles;
	emitter->particles_pool = arena_reserve(arena, sizeof(struct particle) * n_particles);
	emitter->enable = true;
	memset(emitter->particles_pool, 0x0, sizeof(struct particle) * n_particles);

	dll_init_sentinel(&emitter->active_sentinel);
	dll_init_sentinel(&emitter->free_sentinel);

	for (u32 i = 0; i < n_particles; ++i) { dll_insert_back(&emitter->free_sentinel, emitter->particles_pool + i); }
}

sm__force_inline void
particle_emitter_set_shape_box(struct particle_emitter *emitter, struct aabb box)
{
	emitter->shape_type = EMISSION_SHAPE_AABB;
	;
	emitter->box = box;
}

sm__force_inline void
particle_emitter_set_shape_cube(struct particle_emitter *emitter, trs cube)
{
	emitter->shape_type = EMISSION_SHAPE_CUBE;
	emitter->cube = cube;
}

#endif // SM_ECS_H