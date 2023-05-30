#include "animation/smAnimation.h"
#include "core/smBase.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "core/smMM.h"
#include "core/smResource.h"

#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf/cgltf.h"

#include "vendor/physfs/src/physfs.h"

static struct arena *Garena;

#define STBI_MALLOC(sz)	       arena_reserve(Garena, sz)
#define STBI_REALLOC(p, newsz) arena_resize(Garena, p, newsz)
#define STBI_FREE(p)	       arena_free(Garena, p)
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

static void
sm__physfs_log_last_error(str8 message)
{
	PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
	const char *err_str = PHYSFS_getErrorByCode(err);

	log_error(str8_from("{s} PHYSFS_Error: {s}"), message, (str8){.cidata = err_str, .size = strlen(err_str)});
}

static void *
sm__gltf_malloc(sm__maybe_unused void *user_data, u64 size)
{
	void *result;

	result = arena_reserve(Garena, size);

	return (result);
}

static void
sm__gltf_free(sm__maybe_unused void *user_data, void *ptr)
{
	arena_free(Garena, ptr);
}

static cgltf_result
sm__gltf_file_read(sm__maybe_unused const struct cgltf_memory_options *memory_options,
    sm__maybe_unused const struct cgltf_file_options *file_options, sm__maybe_unused const char *path,
    sm__maybe_unused cgltf_size *size, sm__maybe_unused void **data)
{
	struct fs_file file = fs_file_open_read_cstr(path);
	if (!file.ok) { return (cgltf_result_file_not_found); }

	cgltf_size file_size = size ? *size : 0;

	if (file_size == 0) { file_size = (cgltf_size)file.status.filesize; }

	i8 *file_data = (i8 *)sm__gltf_malloc(memory_options->user_data, file_size);
	if (!file_data)
	{
		fs_file_close(&file);
		return cgltf_result_out_of_memory;
	}
	u32 read_size = PHYSFS_readBytes(file.fsfile, file_data, file_size);

	fs_file_close(&file);

	if (read_size != file_size)
	{
		sm__gltf_free(memory_options->user_data, file_data);
		return cgltf_result_io_error;
	}

	if (size) { *size = file_size; }
	if (data) { *data = file_data; }

	return cgltf_result_success;
}

void
sm__gltf_file_free(sm__maybe_unused const struct cgltf_memory_options *memory_options,
    sm__maybe_unused const struct cgltf_file_options *file_options, sm__maybe_unused void *data)
{
	sm__gltf_free(memory_options->user_data, data);
}

static i32
sm__gltf_get_node_index(cgltf_node *target, cgltf_node *all_nodes, u32 num_nodes)
{
	if (target == 0) { return (-1); }
	assert(all_nodes);

	for (u32 i = 0; i < num_nodes; ++i)
	{
		if (target == &all_nodes[i]) { return (i32)i; }
	}

	return (-1);
}

static struct trs
sm__gltf_get_local_transform(const cgltf_node *node)
{
	struct trs result = trs_identity();
	if (node->has_matrix)
	{
		m4 mat;
		memcpy(&mat, node->matrix, 16 * sizeof(f32));
		result = trs_from_m4(mat);
	}

	if (node->has_translation) { memcpy(&result.position, node->translation, 3 * sizeof(f32)); }
	if (node->has_rotation) { memcpy(&result.rotation, node->rotation, 4 * sizeof(f32)); }
	if (node->has_scale) { memcpy(&result.scale, node->scale, 3 * sizeof(f32)); }

	return (result);
}

static void
sm__gltf_load_images(cgltf_data *data)
{
	log_trace(str8_from(" * loading images"));
	for (u32 i = 0; i < data->images_count; ++i)
	{
		cgltf_image *gltf_image = &data->images[i];
		struct image_resource image = {0};

		str8 image_name = str8_from_cstr(Garena, gltf_image->name);

		i32 channels;
		i8 buf[128] = "assets/exported/";
		strcat(buf, gltf_image->uri);
		stbi_set_flip_vertically_on_load(true);
		image.data = stbi_load(buf, (i32 *)&image.width, (i32 *)&image.height, &channels, 0);
		if (!image.data)
		{
			log_error(str8_from("{s} error loading image data"),
			    (str8){.idata = gltf_image->uri, .size = strlen(gltf_image->uri)});
			exit(1);
		}
		assert(image.width != 0);
		assert(image.height != 0);
		assert(channels != 0);

		if (channels == 1) { image.pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE; }
		else if (channels == 2) { image.pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA; }
		else if (channels == 3) { image.pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8; }
		else { image.pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; }

		struct image_resource *image_ptr = sm___resource_mock_push_image(&image);
		struct resource resource = resource_make(image_name, RESOURCE_IMAGE, image_ptr);
		resource_push(&resource);
	}
}

static void
sm__gltf_load_materials(cgltf_data *data)
{
	log_trace(str8_from(" * loading materials"));
	for (u32 i = 0; i < data->materials_count; ++i)
	{
		// if (i == 1) { break; }
		const cgltf_material *gltf_material = &data->materials[i];
		struct material_resource material = {0};
		material.color = WHITE;

		str8 material_name = str8_from_cstr(Garena, gltf_material->name);

		if (gltf_material->has_pbr_metallic_roughness)
		{
			const cgltf_float *base_color = gltf_material->pbr_metallic_roughness.base_color_factor;
			v4 c = v4_new(base_color[0], base_color[1], base_color[2], base_color[3]);
			if (glm_vec4_eq_eps(c.data, 0.0f)) { c = v4_one(); }
			material.color = color_from_v4(c);

			if (gltf_material->pbr_metallic_roughness.base_color_texture.texture)
			{
				i8 *image_name =
				    gltf_material->pbr_metallic_roughness.base_color_texture.texture->image->name;
				str8 str_image_name = str8_from_cstr(Garena, image_name);
				struct resource *image_resource = resource_get_by_name(str_image_name);

				assert(image_resource->name.size != 0);
				material.image = image_resource->name;
				// material.image_ref = resource_make_reference(resource);
			}
		}
		material.double_sided = gltf_material->double_sided;

		struct material_resource *material_ptr = sm___resource_mock_push_material(&material);
		struct resource resource = resource_make(material_name, RESOURCE_MATERIAL, material_ptr);
		resource_push(&resource);
	}
}

static void
sm__gltf_load_skinned_mesh(
    mesh_resource *mesh, cgltf_attribute *attribute, cgltf_skin *skin, cgltf_node *nodes, u32 node_count)
{
	cgltf_attribute_type attr_type = attribute->type;
	cgltf_accessor *accessor = attribute->data;

	u32 component_count = 0;
	if (accessor->type == cgltf_type_vec2) { component_count = 2; }
	else if (accessor->type == cgltf_type_vec3) { component_count = 3; }
	else if (accessor->type == cgltf_type_vec4) { component_count = 4; }

	f32 *values = 0;
	array_set_len(Garena, values, accessor->count * component_count);
	for (cgltf_size i = 0; i < accessor->count; ++i)
	{
		cgltf_accessor_read_float(accessor, i, &values[i * component_count], component_count);
	}

	cgltf_size accessor_count = accessor->count;

	// clang-format off
	for (u32 i = 0; i < accessor_count; ++i)
	{
		u32 idx = i * component_count;
		switch (attr_type)
		{
		case cgltf_attribute_type_position:
		{
			v3 position = v3_new(values[idx + 0], values[idx + 1], values[idx + 2]);
			array_push(Garena, mesh->positions, position);
		} break;

		case cgltf_attribute_type_texcoord:
		{
			v2 tex_coord = v2_new(values[idx + 0], (1.0f - values[idx + 1]));
			array_push(Garena, mesh->uvs, tex_coord);
		} break;

		case cgltf_attribute_type_normal:
		{
			v3 normal = v3_new(values[idx + 0], values[idx + 1], values[idx + 2]);
			if (glm_vec3_eq_eps(normal.data, 0.0f)) { normal = v3_up(); }
			glm_vec3_normalize(normal.data);
			array_push(Garena, mesh->normals, normal);
		} break;

		case cgltf_attribute_type_color:
		{
			v4 c = v4_new(values[idx + 0], values[idx + 1], values[idx + 2], values[idx + 3]);
			array_push(Garena, mesh->colors, c);
		} break;

		case cgltf_attribute_type_weights:
		{
			assert(skin);
			v4 weight = v4_new(values[idx + 0], values[idx + 1], values[idx + 2], values[idx + 3]);
			array_push(Garena, mesh->skin_data.weights, weight);
		} break;

		case cgltf_attribute_type_joints:
		{
			assert(skin);
			iv4 joints;

			joints.data[0] = (i32)(values[idx + 0] + 0.5f);
			joints.data[1] = (i32)(values[idx + 1] + 0.5f);
			joints.data[2] = (i32)(values[idx + 2] + 0.5f);
			joints.data[3] = (i32)(values[idx + 3] + 0.5f);

			joints.data[0] = sm__gltf_get_node_index(skin->joints[joints.data[0]], nodes, node_count);
			joints.data[1] = sm__gltf_get_node_index(skin->joints[joints.data[1]], nodes, node_count);
			joints.data[2] = sm__gltf_get_node_index(skin->joints[joints.data[2]], nodes, node_count);
			joints.data[3] = sm__gltf_get_node_index(skin->joints[joints.data[3]], nodes, node_count);

			joints.data[0] = glm_max(0, joints.data[0]);
			joints.data[1] = glm_max(0, joints.data[1]);
			joints.data[2] = glm_max(0, joints.data[2]);
			joints.data[3] = glm_max(0, joints.data[3]);

			array_push(Garena, mesh->skin_data.influences, joints);
		} break;

		default: break;
		}
	}

	// clang-format on

	array_release(Garena, values);
}

static void
sm__gltf_load_meshes(cgltf_data *data)
{
	log_trace(str8_from("* loading meshes"));

	for (u32 i = 0; i < data->meshes_count; ++i)
	{
		cgltf_mesh *gltf_mesh = &data->meshes[i];
		cgltf_skin *gltf_skin = data->skins;

		u32 num_primi = data->meshes[i].primitives_count;

		assert(num_primi == 1); // TODO: handle more than one mesh per node
		for (u32 j = 0; j < num_primi; ++j)
		{
			mesh_resource mesh = {0};

			str8 mesh_name = str8_from_cstr(Garena, gltf_mesh->name);

			cgltf_primitive *primitive = &data->meshes[i].primitives[j];
			u32 ac = primitive->attributes_count;

			mesh.skin_data.is_skinned = !!gltf_skin;
			for (u32 k = 0; k < ac; ++k)
			{
				cgltf_attribute *attribute = &primitive->attributes[k];
				sm__gltf_load_skinned_mesh(&mesh, attribute, gltf_skin, data->nodes, data->nodes_count);
			}

			// check whether the primitive contains indices. If it does, the index
			// buffer of the mesh needs to be filled out as well
			if (primitive->indices != 0)
			{
				u32 ic = primitive->indices->count;
				array_set_len(Garena, mesh.indices, ic);
				for (u32 k = 0; k < ic; ++k)
				{
					mesh.indices[k] = cgltf_accessor_read_index(primitive->indices, k);
				}
			}
			assert(primitive->indices); // TODO

			if (!mesh.colors)
			{
				u32 len = array_len(mesh.positions);
				array_set_len(Garena, mesh.colors, len);
				for (u32 c = 0; c < len; ++c) mesh.colors[c] = v4_one();
			}

			mesh.flags = MESH_FLAG_DIRTY;

			struct mesh_resource *mesh_ptr = sm___resource_mock_push_mesh(&mesh);
			struct resource resource = resource_make(mesh_name, RESOURCE_MESH, mesh_ptr);
			resource_push(&resource);
		}
	}
}

static u32
sm__gltf_count_chidren_nodes(cgltf_node *node)
{
	u32 result = node->children_count;

	for (u32 i = 0; i < node->children_count; ++i) { result += sm__gltf_count_chidren_nodes(node->children[i]); }

	return (result);
};

static void
sm__gltf_load_chidren_nodes(struct scene_resource *scene, i32 parent_index, cgltf_node *gltf_node)
{
	array_push(Garena, scene->nodes, (struct scene_node){0});
	struct scene_node *node = array_last_item(scene->nodes);
	u32 child_index = array_len(scene->nodes) - 1;

	node->name = str8_from_cstr(Garena, gltf_node->name);
	node->parent_index = parent_index;

	struct trs transform = sm__gltf_get_local_transform(gltf_node);
	glm_vec3_copy(transform.position.data, node->position.data);
	glm_vec3_copy(transform.scale.data, node->scale.data);
	glm_vec4_copy(transform.rotation.data, node->rotation.data);

	if (gltf_node->mesh)
	{
		str8 mesh_name = str8_from_cstr(Garena, gltf_node->mesh->name);

		struct resource *resource = resource_get_by_name(mesh_name);
		assert(resource && resource->type == RESOURCE_MESH);

		node->mesh = mesh_name;

		if (gltf_node->mesh->primitives->material)
		{
			cgltf_material *target = gltf_node->mesh->primitives->material;
			str8 matererial_name = str8_from_cstr(Garena, target->name);

			struct resource *material_resource = resource_get_by_name(matererial_name);
			assert(material_resource && material_resource->type == RESOURCE_MATERIAL);
			node->material = matererial_name;
		}
		// if (!n->material_ref)
		// {
		// 	n->material_ref = resource_material_make_reference(resource_material_get_default());
		// }
	}

	for (u32 i = 0; i < gltf_node->children_count; ++i)
	{
		sm__gltf_load_chidren_nodes(scene, child_index, gltf_node->children[i]);
	}
};

static void
sm__gltf_load_scenes(cgltf_data *data)
{
	cgltf_node *gltf_nodes = data->nodes;
	u32 gltf_nodes_count = data->nodes_count;

	for (u32 i = 0; i < data->scenes_count; ++i)
	{
		cgltf_scene *gltf_scene = &data->scenes[i];

		struct scene_resource scene = {0};
		str8 scene_name = str8_from_cstr(Garena, gltf_scene->name);

		u32 nodes_count = gltf_scene->nodes_count;
		for (u32 j = 0; j < gltf_scene->nodes_count; ++j)
		{
			nodes_count += sm__gltf_count_chidren_nodes(gltf_scene->nodes[j]);
		}

		array_set_cap(Garena, scene.nodes, nodes_count);
		// memset(scene.nodes, 0x0, sizeof(struct scene_node) * array_len(scene.nodes));

		for (u32 k = 0; k < gltf_scene->nodes_count; ++k)
		{
			cgltf_node *gltf_root_node = gltf_scene->nodes[k];
			struct scene_node root_node = {0};

			sm__gltf_load_chidren_nodes(&scene, -1, gltf_root_node);
		}
		assert(array_len(scene.nodes) == nodes_count);

		struct scene_resource *scene_ptr = sm___resource_mock_push_scene(&scene);
		struct resource resource = resource_make(scene_name, RESOURCE_SCENE, scene_ptr);
		resource_push(&resource);
	}
}

static void
sm__gltf_update_armature_inverse_bind_pose(struct armature_resource *armature)
{
	assert(armature != 0);

	u32 size = array_len(armature->bind.joints);
	array_set_len(Garena, armature->inverse_bind, size);

	for (u32 i = 0; i < size; ++i)
	{
		struct trs world = pose_get_global_transform(&armature->bind, i);

		m4 inv = trs_to_m4(world);
		glm_mat4_inv(inv.data, inv.data);

		armature->inverse_bind[i] = inv;
	}
}

static void
sm__gltf_load_armatures(cgltf_data *data)
{
	assert(data->skins_count == 1);

	log_trace(str8_from("* loading armatures"));

	cgltf_skin *gltf_skin = &data->skins[0];
	struct pose rest = {0};
	u32 num_bones = data->nodes_count;
	pose_resize(Garena, &rest, num_bones);

	for (u32 i = 0; i < num_bones; ++i)
	{
		cgltf_node *n = &data->nodes[i];

		struct trs local_transform = sm__gltf_get_local_transform(n);
		i32 node_parent_index = sm__gltf_get_node_index(n->parent, data->nodes, data->nodes_count);

		rest.joints[i] = local_transform;
		rest.parents[i] = node_parent_index;
	}

	array(struct trs) world_bind_pose = 0;
	array_set_len(Garena, world_bind_pose, num_bones);

	for (u32 i = 0; i < num_bones; ++i) { world_bind_pose[i] = pose_get_global_transform(&rest, i); }

	if (gltf_skin->inverse_bind_matrices->count != 0)
	{
		assert(gltf_skin->inverse_bind_matrices->count == gltf_skin->joints_count);
		array(m4) inv_bind_accessor = 0;
		array_set_len(Garena, inv_bind_accessor, gltf_skin->inverse_bind_matrices->count);

		for (cgltf_size i = 0; i < gltf_skin->inverse_bind_matrices->count; ++i)
		{
			cgltf_accessor_read_float(
			    gltf_skin->inverse_bind_matrices, i, inv_bind_accessor[i].float16, 16);
		}

		u32 num_joints = gltf_skin->joints_count;
		for (u32 i = 0; i < num_joints; ++i)
		{
			// read the inverse bind matrix of the joint
			m4 inverse_bind_matrix = inv_bind_accessor[i];
			m4 bind_matrix;
			glm_mat4_inv(inverse_bind_matrix.data, bind_matrix.data);

			struct trs bind_transform = trs_from_m4(bind_matrix);

			// set that transform in the world_nind_pose
			cgltf_node *joint_node = gltf_skin->joints[i];
			i32 joint_index = sm__gltf_get_node_index(joint_node, data->nodes, num_bones);
			assert(joint_index != -1);
			world_bind_pose[joint_index] = bind_transform;
		}

		array_release(Garena, inv_bind_accessor);
	}

	struct pose bind = {0};
	pose_resize(Garena, &bind, num_bones);
	pose_copy(Garena, &bind, &rest);

	for (u32 i = 0; i < num_bones; ++i)
	{
		struct trs current = world_bind_pose[i];
		i32 p = bind.parents[i];

		// bring into parent space
		if (p >= 0)
		{
			struct trs parent = world_bind_pose[p];
			current = trs_combine(trs_inverse(parent), current);
		}
		bind.joints[i] = current;
	}

	array_release(Garena, world_bind_pose);

	str8 armature_name = str8_from_cstr(Garena, gltf_skin->name);

	struct armature_resource armature = {.rest = rest, .bind = bind};
	sm__gltf_update_armature_inverse_bind_pose(&armature);

	struct armature_resource *armature_ptr = sm___resource_mock_push_armature(&armature);
	struct resource resource = resource_make(armature_name, RESOURCE_ARMATURE, armature_ptr);
	resource_push(&resource);
}

static void
sm__gltf_load_track_from_channel(struct track *result, u32 stride, const cgltf_animation_channel *channel)
{
	cgltf_animation_sampler *sampler = channel->sampler;
	u32 interp = INTERPOLATION_CONSTANT;
	if (sampler->interpolation == cgltf_interpolation_type_linear) { interp = INTERPOLATION_LINEAR; }
	else if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) { interp = INTERPOLATION_CUBIC; }

	b8 is_sampler_cubic = interp == INTERPOLATION_CUBIC;

	result->interpolation = interp;

	f32 *time = 0;
	array_set_len(Garena, time, sampler->input->count * 1);
	for (u32 i = 0; i < sampler->input->count; ++i) { cgltf_accessor_read_float(sampler->input, i, &time[i], 1); }

	f32 *val = 0;
	array_set_len(Garena, val, sampler->output->count * stride);
	for (u32 i = 0; i < sampler->output->count; ++i)
	{
		cgltf_accessor_read_float(sampler->output, i, &val[i * stride], stride);
	}

	result->track_type = 0;
	u32 num_frames = sampler->input->count;
	u32 comp_count = array_len(val) / array_len(time);

	if (stride == 1) { result->track_type = TRACK_TYPE_SCALAR; }
	else if (stride == 3) { result->track_type = TRACK_TYPE_V3; }
	else if (stride == 4) { result->track_type = TRACK_TYPE_V4; }
	assert(result->track_type == TRACK_TYPE_SCALAR || result->track_type == TRACK_TYPE_V3 ||
	       result->track_type == TRACK_TYPE_V4);

	// clang-format off
	switch (result->track_type)
	{
	case TRACK_TYPE_SCALAR:
	{
		array_set_len(Garena, result->frames_scalar, num_frames);
		memset(result->frames_scalar, 0x0, num_frames * sizeof(*result->frames_scalar));
	} break;

	case TRACK_TYPE_V3:
	{
		array_set_len(Garena, result->frames_v3, num_frames);
		memset(result->frames_v3, 0x0, num_frames * sizeof(*result->frames_v3));
	} break;

	case TRACK_TYPE_V4:
	{
		array_set_len(Garena, result->frames_v4, num_frames);
		memset(result->frames_v4, 0x0, num_frames * sizeof(*result->frames_v4));
	} break;

	default: sm__unreachable();
	}

	for (u32 i = 0; i < num_frames; ++i)
	{
		switch (result->track_type)
		{

		case TRACK_TYPE_SCALAR:
		{
			u32 base_index = i * comp_count;
			struct frame_scalar *frame = &result->frames_scalar[i];
			i32 offset = 0;
			frame->t = time[i];

			frame->in = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
			frame->value = val[base_index + offset++];
			frame->out = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
		} break;

		case TRACK_TYPE_V3:
		{
			u32 base_index = i * comp_count;
			struct frame_v3 *frame = &result->frames_v3[i];
			i32 offset = 0;
			frame->t = time[i];

			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->in.data[comp] = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
			}
			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->value.data[comp] = val[base_index + offset++];
			}

			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->out.data[comp] = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
			}
		} break;

		case TRACK_TYPE_V4:
		{
			u32 base_index = i * comp_count;
			struct frame_v4 *frame = &result->frames_v4[i];
			u32 offset = 0;
			frame->t = time[i];

			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->in.data[comp] = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
			}
			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->value.data[comp] = val[base_index + offset++];
			}

			for (u32 comp = 0; comp < stride; ++comp)
			{
				frame->out.data[comp] = is_sampler_cubic ? val[base_index + offset++] : 0.0f;
			}
		} break;

		default: sm__unreachable();
		}
	}
	// clang-format off

	array_release(Garena, time);
	array_release(Garena, val);
}

static void
sm__gltf_load_anim_clips(cgltf_data *data)
{
	u32 num_clips = data->animations_count;
	u32 num_nodes = data->nodes_count;

	for (u32 i = 0; i < num_clips; ++i)
	{
		u32 num_channels = data->animations[i].channels_count;

		struct clip_resource clip = {0};
		clip.looping = true;

		str8 clip_name = str8_from_cstr(Garena, data->animations[i].name);

		for (u32 j = 0; j < num_channels; ++j)
		{
			cgltf_animation_channel *channel = &data->animations[i].channels[j];
			cgltf_node *target = channel->target_node;
			i32 node_id = sm__gltf_get_node_index(target, data->nodes, num_nodes);

			if (channel->target_path == cgltf_animation_path_type_translation)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(Garena, &clip, node_id);
				sm__gltf_load_track_from_channel(&track->position, 3, channel);
			}
			else if (channel->target_path == cgltf_animation_path_type_scale)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(Garena, &clip, node_id);
				sm__gltf_load_track_from_channel(&track->scale, 3, channel);
			}
			else if (channel->target_path == cgltf_animation_path_type_rotation)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(Garena, &clip, node_id);
				sm__gltf_load_track_from_channel(&track->rotation, 4, channel);
			}
		}

		clip_recalculate_duration(&clip);

		for (u32 j = 0; j < array_len(clip.tracks); ++j)
		{
			u32 joint = clip.tracks[j].id;

			struct transform_track *ttrack = clip_get_transform_track_from_joint(Garena, &clip, joint);

			track_index_look_up_table(Garena, &ttrack->position);
			track_index_look_up_table(Garena, &ttrack->rotation);
			track_index_look_up_table(Garena, &ttrack->scale);
		}

		struct clip_resource *clip_ptr = sm___resource_mock_push_clip(&clip);
		struct resource resource = resource_make(clip_name, RESOURCE_CLIP, clip_ptr);
		resource_push(&resource);
	}
}

void
parse_gltf_file(str8 gltf_file)
{
	cgltf_options options = {
	    .memory = {.alloc_func = sm__gltf_malloc, .free_func = sm__gltf_free	},
	    .file = {.read = sm__gltf_file_read,	 .release = sm__gltf_file_free},
	};

	cgltf_data *data = 0;
	cgltf_result gltf_result = cgltf_parse_file(&options, gltf_file.idata, &data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not parse gltf file"), gltf_file);
		return;
	}

	gltf_result = cgltf_load_buffers(&options, data, "exported/");
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not load buffers"), gltf_file);

		cgltf_free(data);
		return;
	}

	gltf_result = cgltf_validate(data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] invalid gltf file"), gltf_file);
		cgltf_free(data);
		return;
	}

	sm__gltf_load_images(data);
	sm__gltf_load_materials(data);
	sm__gltf_load_meshes(data);
	if (data->skins_count == 0) { sm__gltf_load_scenes(data); }
	else
	{
		sm__gltf_load_armatures(data);
		sm__gltf_load_anim_clips(data);
	}

	cgltf_free(data);
}

b8
print_resource_cb(str8 name, struct resource *resource, void *user_data)
{
	resource_print(resource);

	return (true);
}

b8
dump_resource_cb(str8 name, struct resource *resource, void *user_data)
{
	resource_write(resource);

	return (true);
}

i32
main(i32 argc, i8 *argv[])
{
	// init log system
	void *_mm_log = mm_malloc(KB(10));
	struct buf base_memory_log = {.data = _mm_log, .size = KB(10)};
	log_init(base_memory_log);

	// init general memory
	void *_mm = mm_malloc(MB(30));
	struct buf base_memory = {.data = _mm, .size = MB(30)};

	str8 path = str8_from("assets/");
	sm___resource_mock_init(base_memory, argv, path);
	Garena = resource_get_arena();

	for (u32 i = 1; i < argc; ++i)
	{
		str8 file = str8_from_cstr(Garena, argv[i]);
		log_trace(str8_from(" * Parsing : {s}"), file);

		parse_gltf_file(file);
	}
	resource_for_each(print_resource_cb, 0);
	resource_for_each(dump_resource_cb, 0);

	sm___resource_mock_teardown();

	sm___resource_mock_init(base_memory, argv, path);
	Garena = resource_get_arena();

	sm___resource_mock_read();
	resource_for_each(print_resource_cb, 0);

	mm_free(_mm_log);
	mm_free(_mm);

	return (0);
}
