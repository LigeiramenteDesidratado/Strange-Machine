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

#define STBI_NO_STDIO
#define STBI_MALLOC(sz)	       arena_reserve(Garena, sz)
#define STBI_REALLOC(p, newsz) arena_resize(Garena, p, newsz)
#define STBI_FREE(p)	       arena_free(Garena, p)
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

static void sm__load_image(str8 uri, str8 image_name);

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

static trs
sm__gltf_get_local_transform(const cgltf_node *node)
{
	trs result = trs_identity();
	if (node->has_matrix)
	{
		m4 mat;
		memcpy(&mat, node->matrix, 16 * sizeof(f32));
		result = trs_from_m4(mat);
	}

	if (node->has_translation) { memcpy(&result.translation, node->translation, 3 * sizeof(f32)); }
	if (node->has_rotation) { memcpy(&result.rotation, node->rotation, 4 * sizeof(f32)); }
	if (node->has_scale) { memcpy(&result.scale, node->scale, 3 * sizeof(f32)); }

	return (result);
}

enum extra_prop
{

	EXTRA_PROP_NONE = 0,
	EXTRA_PROP_STATIC_BODY = BIT(0),
	EXTRA_PROP_RIGID_BODY = BIT(1),
	EXTRA_PROP_PLAYER = BIT(2),

	EXTRA_PROP_BLEND = BIT(3),
	EXTRA_PROP_DOUBLE_SIDED = BIT(4),

	// enforce 32-bit size enum
	SM__EXTRA_PROP_ENFORCE_ENUM_SIZE = 0x7fffffff
};

static enum extra_prop
sm__gltf_parse_extra_props(const str8 json_obj)
{
	enum extra_prop result = 0;

	i8 *buf = json_obj.idata;
	while (*buf != '{') { buf++; }
	buf++;

	// Parse the key-value pairs
	while (*buf != '}')
	{
		// Skip leading white spaces and find the key name
		while (*buf == ' ' || *buf == '\n') { buf++; }

		if (*buf == '\"')
		{
			const char *key_start = ++buf;

			while (*buf != '\"') { buf++; }
			const char *key_end = buf;
			while (*buf != ':') { buf++; }

			str8 token = (str8){.cidata = key_start, .size = key_end - key_start};
			if (str8_eq(token, str8_from("STATIC_BODY")))
			{
				buf++;
				while (*buf != '\"') { buf++; }
				buf++;

				if (*buf == 't' || *buf == 'T') { result |= EXTRA_PROP_STATIC_BODY; }
				else if (*buf == 'f' || *buf == 'F') { result &= ~(u32)EXTRA_PROP_STATIC_BODY; }
				else { log_warn(str8_from("[{s}] invalid boolean value"), str8_from("RIGID_BODY")); }
			}
			else if (str8_eq(token, str8_from("RIGID_BODY")))
			{
				buf++;
				while (*buf != '\"') { buf++; }
				buf++;

				if (*buf == 't' || *buf == 'T') { result |= EXTRA_PROP_RIGID_BODY; }
				else if (*buf == 'f' || *buf == 'F') { result &= ~(u32)EXTRA_PROP_RIGID_BODY; }
				else { log_warn(str8_from("[{s}] invalid boolean value"), token); }
			}
			else if (str8_eq(token, str8_from("PLAYER")))
			{
				buf++;
				while (*buf != '\"') { buf++; }
				buf++;

				if (*buf == 't' || *buf == 'T') { result |= EXTRA_PROP_PLAYER; }
				else if (*buf == 'f' || *buf == 'F') { result &= ~(u32)EXTRA_PROP_PLAYER; }
				else { log_warn(str8_from("[{s}] invalid boolean value"), token); }
			}
			else if (str8_eq(token, str8_from("BLEND")))
			{
				buf++;
				while (*buf != '\"') { buf++; }
				buf++;

				if (*buf == 't' || *buf == 'T') { result |= EXTRA_PROP_BLEND; }
				else if (*buf == 'f' || *buf == 'F') { result &= ~(u32)EXTRA_PROP_BLEND; }
				else { log_warn(str8_from("[{s}] invalid boolean value"), token); }
			}
			else if (str8_eq(token, str8_from("DOUBLE_SIDED")))
			{
				buf++;
				while (*buf != '\"') { buf++; }
				buf++;

				if (*buf == 't' || *buf == 'T') { result |= EXTRA_PROP_DOUBLE_SIDED; }
				else if (*buf == 'f' || *buf == 'F') { result &= ~(u32)EXTRA_PROP_DOUBLE_SIDED; }
				else { log_warn(str8_from("[{s}] invalid boolean value"), token); }
			}
			else { log_warn(str8_from("[{s}] invalid boolean value"), token); }

			while (*buf != ',' && *buf != '}') { buf++; }
			if (*buf != '}') { buf++; }
		}
	}

	return (result);
}

static enum extra_prop
sm__gltf_get_extra_props(cgltf_data *data, const cgltf_extras gltf_extras)
{
	enum extra_prop result = 0;

	cgltf_size extra_size = gltf_extras.end_offset - gltf_extras.start_offset;
	printf("size: %zu\n", extra_size);
	if (extra_size != 0)
	{
		str8 extras;
		b8 dealloc = false;

		if (gltf_extras.data) { str8 extras = str8_from_cstr_stack(gltf_extras.data); }
		else
		{
			dealloc = true;

			extra_size++;
			extras = (str8){.data = arena_reserve(Garena, extra_size), .size = extra_size};
			cgltf_copy_extras_json(data, &gltf_extras, extras.idata, &extra_size);
		}

		result = sm__gltf_parse_extra_props(extras);

		if (dealloc) { arena_free(Garena, extras.data); }
	}

	return (result);
}

static void
sm__gltf_load_images(cgltf_data *data)
{
	log_trace(str8_from(" * loading images"));
	for (u32 i = 0; i < data->images_count; ++i)
	{
		cgltf_image *gltf_image = &data->images[i];

		i8 buf[128] = "exported/";
		strcat(buf, gltf_image->uri);

		str8 uri = str8_from_cstr_stack(buf);
		str8 image_name = str8_from_cstr(Garena, gltf_image->name);
		sm__load_image(uri, image_name);
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
		material.color = cWHITE;

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

			enum extra_prop props = sm__gltf_get_extra_props(data, gltf_mesh->extras);

			if (props & EXTRA_PROP_BLEND) { mesh.flags |= MESH_FLAG_BLEND; }
			if (props & EXTRA_PROP_DOUBLE_SIDED) { mesh.flags |= MESH_FLAG_DOUBLE_SIDED; }

			mesh.flags |= MESH_FLAG_DIRTY | MESH_FLAG_RENDERABLE;

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
sm__gltf_load_chidren_nodes(cgltf_data *data, struct scene_resource *scene, i32 parent_index, cgltf_node *gltf_node)
{
	array_push(Garena, scene->nodes, (struct scene_node){0});
	struct scene_node *node = array_last_item(scene->nodes);
	u32 child_index = array_len(scene->nodes) - 1;

	node->name = str8_from_cstr(Garena, gltf_node->name);
	node->parent_index = parent_index;

	trs transform = sm__gltf_get_local_transform(gltf_node);
	glm_vec3_copy(transform.translation.data, node->position.data);
	glm_vec3_copy(transform.scale.data, node->scale.data);
	glm_vec4_ucopy(transform.rotation.data, node->rotation.data);

	enum extra_prop props = sm__gltf_get_extra_props(data, gltf_node->extras);
	if (props & EXTRA_PROP_STATIC_BODY) { node->prop |= NODE_PROP_STATIC_BODY; }
	if (props & EXTRA_PROP_RIGID_BODY) { node->prop |= NODE_PROP_RIGID_BODY; }
	if (props & EXTRA_PROP_PLAYER) { node->prop |= NODE_PROP_PLAYER; }

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

	if (gltf_node->skin)
	{
		str8 armature_name = str8_from_cstr(Garena, gltf_node->skin->name);
		struct resource *resource = resource_get_by_name(armature_name);

		assert(resource && resource->type == RESOURCE_ARMATURE);
		node->armature = armature_name;
	}

	for (u32 i = 0; i < gltf_node->children_count; ++i)
	{
		sm__gltf_load_chidren_nodes(data, scene, child_index, gltf_node->children[i]);
	}
};

static u32
sm__gltf_count_chidren_nodes_constraint(cgltf_node *node, cgltf_data *data, array(b8) ignore)
{
	i32 idx = sm__gltf_get_node_index(node, data->nodes, data->nodes_count);
	assert(idx != -1);

	u32 result = 0;
	if (!ignore[idx]) { result += 1; }

	for (u32 i = 0; i < node->children_count; ++i)
	{
		result += sm__gltf_count_chidren_nodes_constraint(node->children[i], data, ignore);
	}

	return (result);
};

static void
sm__gltf_load_chidren_nodes_constraint(
    struct scene_resource *scene, i32 parent_index, cgltf_node *gltf_node, cgltf_data *data, array(b8) ignore)
{
	i32 idx = sm__gltf_get_node_index(gltf_node, data->nodes, data->nodes_count);
	assert(idx != -1);
	if (!ignore[idx])
	{
		array_push(Garena, scene->nodes, (struct scene_node){0});
		struct scene_node *node = array_last_item(scene->nodes);

		node->name = str8_from_cstr(Garena, gltf_node->name);
		node->parent_index = parent_index;

		trs transform = sm__gltf_get_local_transform(gltf_node);
		glm_vec3_copy(transform.translation.data, node->position.data);
		glm_vec3_copy(transform.scale.data, node->scale.data);
		glm_vec4_ucopy(transform.rotation.data, node->rotation.data);

		enum extra_prop props = sm__gltf_get_extra_props(data, gltf_node->extras);
		if (props & EXTRA_PROP_STATIC_BODY) { node->prop |= NODE_PROP_STATIC_BODY; }
		if (props & EXTRA_PROP_RIGID_BODY) { node->prop |= NODE_PROP_RIGID_BODY; }
		if (props & EXTRA_PROP_PLAYER) { node->prop |= NODE_PROP_PLAYER; }

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
		if (gltf_node->skin)
		{
			str8 armature_name = str8_from_cstr(Garena, gltf_node->skin->name);
			struct resource *resource = resource_get_by_name(armature_name);

			assert(resource && resource->type == RESOURCE_ARMATURE);
			node->armature = armature_name;
		}
	}

	for (u32 i = 0; i < gltf_node->children_count; ++i)
	{
		sm__gltf_load_chidren_nodes_constraint(
		    scene, ((i32)array_len(scene->nodes)) - 1, gltf_node->children[i], data, ignore);
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

		if (data->skins_count == 0)
		{
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
				sm__gltf_load_chidren_nodes(data, &scene, -1, gltf_root_node);
			}
			assert(array_len(scene.nodes) == nodes_count);
		}
		else if (data->skins_count == 1)
		{
			array(b8) ignore = 0;
			array_set_len(Garena, ignore, gltf_nodes_count);
			memset(ignore, 0x0, sizeof(b8) * gltf_nodes_count);

			cgltf_skin *gltf_skin = &data->skins[0];
			for (u32 j = 0; j < gltf_skin->joints_count; ++j)
			{
				cgltf_node *n = gltf_skin->joints[j];

				i32 node_index = sm__gltf_get_node_index(n, gltf_nodes, gltf_nodes_count);
				assert(node_index != -1);
				ignore[node_index] = true;
			}

			for (u32 i = 0; i < array_len(ignore); ++i)
			{
				str8_printf(Garena, str8_from("{u3d}: {b}\n"), i, ignore[i]);
			}

			u32 nodes_count = 0;
			for (u32 j = 0; j < gltf_scene->nodes_count; ++j)
			{
				nodes_count +=
				    sm__gltf_count_chidren_nodes_constraint(gltf_scene->nodes[j], data, ignore);
			}
			array_set_cap(Garena, scene.nodes, nodes_count);

			for (u32 k = 0; k < gltf_scene->nodes_count; ++k)
			{
				cgltf_node *gltf_root_node = gltf_scene->nodes[k];
				sm__gltf_load_chidren_nodes_constraint(&scene, -1, gltf_root_node, data, ignore);
			}
			assert(array_len(scene.nodes) == nodes_count);
			array_release(Garena, ignore);
		}

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
		trs world = pose_get_global_transform(&armature->bind, i);

		m4 inv = trs_to_m4(world);
		glm_mat4_inv(inv.data, inv.data);

		armature->inverse_bind[i] = inv;
	}
}

static void
sm__gltf_load_armatures(cgltf_data *data)
{
	if (data->skins_count == 0) { return; }
	assert(data->skins_count == 1);

	log_trace(str8_from("* loading armatures"));

	cgltf_skin *gltf_skin = &data->skins[0];
	struct pose rest = {0};
	u32 num_bones = data->nodes_count;
	pose_resize(Garena, &rest, num_bones);

	for (u32 i = 0; i < num_bones; ++i)
	{
		cgltf_node *n = &data->nodes[i];

		trs local_transform = sm__gltf_get_local_transform(n);
		i32 node_parent_index = sm__gltf_get_node_index(n->parent, data->nodes, data->nodes_count);

		rest.joints[i] = local_transform;
		rest.parents[i] = node_parent_index;
	}

	array(trs) world_bind_pose = 0;
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

			trs bind_transform = trs_from_m4(bind_matrix);

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
		trs current = world_bind_pose[i];
		i32 p = bind.parents[i];

		// bring into parent space
		if (p >= 0)
		{
			trs parent = world_bind_pose[p];
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

	memcpy((u32 *)&result->interpolation, &interp, sizeof(u32));
	// result->interpolation = interp;

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
	// clang-format on

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
	sm__gltf_load_armatures(data);
	sm__gltf_load_anim_clips(data);
	sm__gltf_load_scenes(data);

	// if (data->skins_count == 0) {  }
	// else
	// {
	// }

	cgltf_free(data);
}

static void
sm__load_image(str8 uri, str8 image_name)
{
	log_trace(str8_from(" * loading image"));

	struct image_resource image = {0};

	struct fs_file file = fs_file_open(uri, true);
	if (!file.ok)
	{
		log_error(str8_from("error opening file {s}"), uri);
		exit(1);
	}
	void *image_buffer = arena_reserve(Garena, file.status.filesize);
	i64 br = PHYSFS_readBytes(file.fsfile, image_buffer, file.status.filesize);
	if (br != file.status.filesize)
	{
		sm__physfs_log_last_error(str8_from("error while reading image file"));
		exit(1);
	}

	i32 channels;
	image.data = stbi_load_from_memory(
	    image_buffer, file.status.filesize, (i32 *)&image.width, (i32 *)&image.height, &channels, 0);
	if (!image.data)
	{
		log_error(str8_from("{s} error loading image data"), uri);
		exit(1);
	}

	arena_free(Garena, image_buffer);
	fs_file_close(&file);

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

enum sm__file_type
{
	SM__FILE_TYPE_NONE,
	SM__FILE_TYPE_GLTF,
	SM__FILE_TYPE_IMAGE,
};

static enum sm__file_type
detect_file_type(str8 name)
{
	enum sm__file_type result = SM__FILE_TYPE_NONE;

	assert(name.size > 4); // 1 or plus in the name, the '.' and the 3 or 4 characters of the extension
	assert(name.idata[0] != '.');

	i8 *buf = name.idata + name.size;
	if (*(buf - 4) == '.')
	{
		str8 ext = (str8){.idata = buf - 3, .size = 3};
		if (str8_eq(str8_from("png"), ext)) { result = SM__FILE_TYPE_IMAGE; }
	}
	else if (*(buf - 5) == '.')
	{
		str8 ext = (str8){.idata = buf - 4, .size = 4};
		if (str8_eq(str8_from("gltf"), ext)) { result = SM__FILE_TYPE_GLTF; }
	}
	else
	{
		log_error(str8_from("invalid file name: {s}"), name);
		assert(0);
	}

	return (result);
}

i32
main(i32 argc, i8 *argv[])
{
	u32 base_size = MB(50);
	if (!base_memory_init(base_size))
	{
		printf("error allocating base mem!\n");
		return (false);
	}

	atexit(str8_buffer_flush);

	// init log system
	log_init();

	str8 path = str8_from("assets/");
	sm___resource_mock_init(argv, path);
	Garena = resource_get_arena();

	for (u32 i = 1; i < argc; ++i)
	{
		str8 file = str8_from_cstr(Garena, argv[i]);
		log_trace(str8_from(" * Parsing : {s}"), file);

		enum sm__file_type t = detect_file_type(file);

		if (t == SM__FILE_TYPE_GLTF)
		{
			stbi_set_flip_vertically_on_load(true);
			parse_gltf_file(file);
		}
		else if (t == SM__FILE_TYPE_IMAGE)
		{
			i8 *buf = file.idata + file.size;
			while (*buf != '.') { buf--; }
			u32 end_byte = buf - file.idata;
			while (*buf != '/' && buf != file.idata) { buf--; }
			if (*buf == '/') { buf++; }
			u32 beg_byte = buf - file.idata;

			str8 image_name = str8_dup(Garena, (str8){.idata = buf, .size = end_byte - beg_byte});

			stbi_set_flip_vertically_on_load(false);
			sm__load_image(file, image_name);
		}
		else
		{
			log_warn(str8_from("[{s}] not supported file"), file);
			continue;
		}

		log_trace(str8_from(" * Done parsing : {s}"), file);
	}
	resource_for_each(print_resource_cb, 0);
	resource_for_each(dump_resource_cb, 0);

	sm___resource_mock_teardown();
	//
	// sm___resource_mock_init(base_memory, argv, path);
	// Garena = resource_get_arena();
	//
	// sm___resource_mock_read();
	// resource_for_each(print_resource_cb, 0);

	base_memory_teardown();

	return (0);
}
