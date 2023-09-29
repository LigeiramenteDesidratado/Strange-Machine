#include "animation/smAnimation.h"
#include "animation/smPose.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "core/smRefCount.h"
#include "core/smString.h"
#include "ecs/smECS.h"
#include "ecs/smScene.h"
#include "math/smMath.h"

#include "core/smResource.h"

static str8
resource_image_pixel_format_str8(u32 pixelformat)
{
	switch (pixelformat)
	{
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: return str8_from("UNCOMPRESSED_GRAYSCALE");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA: return str8_from("UNCOMPRESSED_GRAY_ALPHA");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_ALPHA: return str8_from("UNCOMPRESSED_ALPHA");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G6B5: return str8_from("UNCOMPRESSED_R5G6B5");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8: return str8_from("UNCOMPRESSED_R8G8B8");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1: return str8_from("UNCOMPRESSED_R5G5B5A1");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4: return str8_from("UNCOMPRESSED_R4G4B4A4");
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: return str8_from("UNCOMPRESSED_R8G8B8A8");
	default: return str8_from("UNKOWN PIXLFORMAT");
	}
}

u32
resource_image_size(u32 width, u32 height, u32 pixel_format)
{
	u32 result = 0; // Size in bytes
	u32 bpp = 0;	// Bits per pixel
	switch (pixel_format)
	{
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_ALPHA:
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: bpp = 8; break;
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4: bpp = 16; break;
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: bpp = 32; break;
	case IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8: bpp = 24; break;
	default: sm__unreachable(); break;
	}

	result = width * height * bpp / 8; // Total data size in bytes

	return (result);
}

static void
resource_image_print(struct image_resource *image)
{
	log_trace(str8_from("        - dim            : {u3d}x{u3d}"), image->width, image->height);
	log_trace(str8_from("        - pixelformat    : {s}"), resource_image_pixel_format_str8(image->pixel_format));
	log_trace(str8_from("        - texture handle : {u3d}"), image->texture_handle);
}

static void
resource_material_print(struct material_resource *material)
{
	log_trace(str8_from("        - double sided   : {b}"), material->double_sided);
	log_trace(str8_from("        - color          : ({cv}) 0x{cx}"), material->color, material->color);
}

static void
resource_mesh_print(struct mesh_resource *mesh)
{
	log_trace(str8_from("        - vertices: {u3d}"), array_len(mesh->positions));
	log_trace(str8_from("        - indexed : {b}"), mesh->indices != 0);
	log_trace(str8_from("        - skinned : {b}"), mesh->skin_data.is_skinned);
	log_trace(str8_from("        - flags   : {u3d}"), mesh->flags);
}

static str8
resource_scene_node_prop_str8(u32 prop)
{
	switch (prop)
	{
	case 0: return str8_from("NO PROP");
	case NODE_PROP_STATIC_BODY: return str8_from("NODE_PROP_STATIC_BODY");
	case NODE_PROP_RIGID_BODY: return str8_from("NODE_PROP_RIGID_BODY");
	case NODE_PROP_PLAYER: return str8_from("NODE_PROP_PLAYER");
	default: return str8_from("UNKOWN NODE PROP");
	}
}

static void
resource_scene_print(struct scene_resource *scene)
{
	log_trace(str8_from("        - nodes   : {u3d}"), array_len(scene->nodes));

	for (u32 i = 0; i < array_len(scene->nodes); ++i)
	{
		log_trace(str8_from("        - name    : {s}"), scene->nodes[i].name);
		log_trace(str8_from("        - position: {v3}"), scene->nodes[i].position);
		log_trace(str8_from("        - rotation: {v4}"), scene->nodes[i].rotation);
		log_trace(str8_from("        - scale   : {v3}"), scene->nodes[i].scale);
		log_trace(str8_from("        - prop    : {s}"), resource_scene_node_prop_str8(scene->nodes[i].prop));
		log_trace(str8_from("        - mesh    : {s}"), scene->nodes[i].mesh);
		log_trace(str8_from("        - material: {s}"), scene->nodes[i].material);
		log_trace(str8_from("        - armature: {s}"), scene->nodes[i].armature);
	}
}

static void
resource_armature_print(struct armature_resource *armature)
{
	log_trace(str8_from("    - armatures joints: {u3d}"), array_len(armature->rest.parents));
}

static void
resource_clip_print(struct clip_resource *clip)
{
	log_trace(str8_from("        - start     : {f}"), (f64)clip->start_time);
	log_trace(str8_from("        - end       : {f}"), (f64)clip->end_time);
	log_trace(str8_from("        - duration  : {f}"), (f64)clip->end_time - (f64)clip->start_time);
	log_trace(str8_from("        - looping   : {b}"), clip->looping);
	log_trace(str8_from("        - tracks    : {u3d}"), array_len(clip->tracks));
}

#define PtrDif(a, b)		  ((u8 *)(a) - (u8 *)(b))
#define PtrAsInt(a)		  PtrDif(a, 0)
#define Member(S, m)		  (((S *)0)->m)
#define OffsetOfMember(S, m)	  PtrAsInt(&Member(S, m))
#define CastFromMember(S, m, ptr) (S *)((u8 *)(ptr)-OffsetOfMember(S, m))

#define GEN_NAME		       str8_resource
#define GEN_KEY_TYPE		       str8
#define GEN_VALUE_TYPE		       struct resource *
#define GEN_HASH_KEY_FN(_key)	       str8_hash(_key)
#define GEN_CMP_KEY_FN(_key_a, _key_b) str8_eq(_key_a, _key_b)
#include "core/smHashMap.inl"

struct resource_manager
{
	struct arena arena;

	struct str8_resource_map map;
	array(struct resource) resources;

	struct
	{
		struct resource image_resource;
		struct image_resource image;

		struct resource material_resource;
		struct material_resource material;
	} defaults;

	array(struct mesh_resource) meshes;
	array(struct image_resource) images;
	array(struct material_resource) materials;
	array(struct armature_resource) armatures;
	array(struct clip_resource) clips;
	array(struct scene_resource) scenes;

	array(struct frag_shader_resource) frag_shaders;
	array(struct vert_shader_resource) vert_shaders;
	array(struct shader_resource) shaders;
};

static struct resource_manager RC; // Resource Context

#include "vendor/physfs/src/physfs.h"

#if 0
#	define MA_MALLOC(sz)	     arena_reserve(&RC.arena, sz)
#	define MA_REALLOC(p, newsz) arena_resize(&RC.arena, p, newsz)
#	define MA_FREE(p)	     arena_free(&RC.arena, p)

#	define DRWAV_MALLOC(sz)	arena_reserve(&RC.arena, sz)
#	define DRWAV_REALLOC(p, newsz) arena_resize(&RC.arena, p, newsz)
#	define DRWAV_FREE(p)		arena_free(&RC.arena, p)

#	define DRFLAC_MALLOC(sz)	 arena_reserve(&RC.arena, sz)
#	define DRFLAC_REALLOC(p, newsz) arena_resize(&RC.arena, p, newsz)
#	define DRFLAC_FREE(p)		 arena_free(&RC.arena, p)

#	define DRMP3_MALLOC(sz)	arena_reserve(&RC.arena, sz)
#	define DRMP3_REALLOC(p, newsz) arena_resize(&RC.arena, p, newsz)
#	define DRMP3_FREE(p)		arena_free(&RC.arena, p)

#	define MINIAUDIO_IMPLEMENTATION
#	include "vendor/miniaudio/miniaudio.h"
#endif

static b8 fs_image_write(struct fs_file *file, struct image_resource *image);
static b8 fs_image_read(struct fs_file *file, struct image_resource *image);

static b8 fs_material_write(struct fs_file *file, struct material_resource *material);
static b8 fs_material_read(struct fs_file *file, struct material_resource *material);

static b8 fs_mesh_write(struct fs_file *file, struct mesh_resource *mesh);
static b8 fs_mesh_read(struct fs_file *file, struct mesh_resource *mesh);

static b8 fs_scene_write(struct fs_file *file, struct scene_resource *scene);
static b8 fs_scene_read(struct fs_file *file, struct scene_resource *scene);

static b8 fs_armature_write(struct fs_file *file, struct armature_resource *armature);
static b8 fs_armature_read(struct fs_file *file, struct armature_resource *armature);

static b8 fs_clip_write(struct fs_file *file, struct clip_resource *clip);
static b8 fs_clip_read(struct fs_file *file, struct clip_resource *clip);

static void fs_write_b8(struct fs_file *file, b8 data);
static b8 fs_read_b8(struct fs_file *file);
static void fs_write_b8a(struct fs_file *file, array(b8) data);
static array(b8) fs_read_b8a(struct fs_file *file);
static void fs_write_u8(struct fs_file *file, u8 data);
static u8 fs_read_u8(struct fs_file *file);
static void fs_write_u8a(struct fs_file *file, array(u8) data);
static array(u8) fs_read_u8a(struct fs_file *file);
static void fs_write_str8(struct fs_file *file, str8 str);
static str8 fs_read_str8(struct fs_file *file);
static void fs_write_i32(struct fs_file *file, i32 data);
static i32 fs_read_i32(struct fs_file *file);
static void fs_write_i32a(struct fs_file *file, array(i32) data);
static array(i32) fs_read_i32a(struct fs_file *file);
static void fs_write_u32(struct fs_file *file, u32 data);
static u32 fs_read_u32(struct fs_file *file);
static void fs_write_u32a(struct fs_file *file, array(u32) data);
static array(u32) fs_read_u32a(struct fs_file *file);
static void fs_write_u64(struct fs_file *file, u64 data);
static u64 fs_read_u64(struct fs_file *file);
static void fs_write_u64a(struct fs_file *file, array(u64) data);
static array(u64) fs_read_u64a(struct fs_file *file);
static void fs_write_f32(struct fs_file *file, f32 data);
static f32 fs_read_f32(struct fs_file *file);
static void fs_write_f32a(struct fs_file *file, array(f32) data);
static array(f32) fs_read_f32a(struct fs_file *file);
static void fs_write_v2(struct fs_file *file, v2 v);
static v2 fs_read_v2(struct fs_file *file);
static void fs_write_v2a(struct fs_file *file, array(v2) v);
static array(v2) fs_read_v2a(struct fs_file *file);
static void fs_write_v3(struct fs_file *file, v3 v);
static v3 fs_read_v3(struct fs_file *file);
static void fs_write_v3a(struct fs_file *file, array(v3) v);
static array(v3) fs_read_v3a(struct fs_file *file);
static void fs_write_v4(struct fs_file *file, v4 v);
static v4 fs_read_v4(struct fs_file *file);
static void fs_write_v4a(struct fs_file *file, array(v4) v);
static array(v4) fs_read_v4a(struct fs_file *file);
static void fs_write_iv4(struct fs_file *file, iv4 v);
static iv4 fs_read_iv4(struct fs_file *file);
static void fs_write_iv4a(struct fs_file *file, array(iv4) v);
static array(iv4) fs_read_iv4a(struct fs_file *file);
static void fs_write_m4(struct fs_file *file, m4 v);
static m4 fs_read_m4(struct fs_file *file);
static void fs_write_m4a(struct fs_file *file, array(m4) v);
static array(m4) fs_read_m4a(struct fs_file *file);

static void
sm__physfs_log_last_error(str8 message)
{
	PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
	const i8 *err_str = PHYSFS_getErrorByCode(err);

	log_error(str8_from("{s} PHYSFS_Error: {s}"), message, (str8){.cidata = err_str, .size = (u32)strlen(err_str)});
}

static void *
sm__physfs_malloc(u64 size)
{
	void *result;

	result = arena_reserve(&RC.arena, (u32)size);

	return (result);
}

static void *
sm__physfs_realloc(void *ptr, u64 size)
{
	void *result;

	result = arena_resize(&RC.arena, ptr, (u32)size);

	return (result);
}

static void
sm__physfs_free(void *ptr)
{
	arena_free(&RC.arena, ptr);
}

void
resource_print(struct resource *resource)
{
	// clang-format off
	switch (resource->type)
	{
	case RESOURCE_IMAGE:
	{
		log_trace(str8_from("=========================RESOURCE_IMAGE========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);
		if (resource->data)
		{
			resource_image_print(resource->data);
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);

	} break;

	case RESOURCE_MATERIAL:
	{
		log_trace(str8_from("=========================RESOURCE_MATERIAL========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);

		if (resource->data)
		{
			resource_material_print(resource->material_data);
			str8 img_name = ((struct material_resource *)(resource->data))->image;
			if (img_name.size > 0)
			{
				log_trace(str8_from(" * {s}"), img_name);
				struct resource *img_resource = resource_get_by_name(img_name);
				if (img_resource)
				{
					resource_image_print(img_resource->data);
				}
				else
				{
					log_warn(str8_from("[{s}]image not loaded yet"), img_name);
				}
			}
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);
	} break;

	case RESOURCE_MESH:
	{
		log_trace(str8_from("=========================RESOURCE_MESH========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);
		if (resource->mesh_data)
		{
			resource_mesh_print(resource->mesh_data);
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);
	} break;

	case RESOURCE_SCENE:
	{
		log_trace(str8_from("=========================RESOURCE_SCENE========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);
		if (resource->scene_data)
		{
			resource_scene_print(resource->scene_data);
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);
	} break;

	case RESOURCE_ARMATURE:
	{
		log_trace(str8_from("=========================RESOURCE_ARMATURE========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);
		if (resource->armature_data)
		{
			resource_armature_print(resource->armature_data);
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);
	} break;

	case RESOURCE_CLIP:
	{
		log_trace(str8_from("=========================RESOURCE_CLIP========================="));
		log_trace(str8_from(" * name: {s}"), resource->name);
		log_trace(str8_from(" * refs: {u3d}"), resource->refs.ref_count);
		if (resource->clip_data)
		{
			resource_clip_print(resource->clip_data);
		}
		else
		{
			log_trace(str8_from(" * data: 0"));
		}
		log_trace(str8_newline_const);
	} break;
	default: log_error(str8_from("Inalid resource type {u3d}"), resource->type);
	}

	// clang-format on
}

union magic
{
	u64 x;
	i8 magic[8];
};

const union magic image_resource_magic = {.magic = "__SMIMGE"};
const union magic material_resource_magic = {.magic = "__SMMATE"};
const union magic mesh_resource_magic = {.magic = "__SMMESH"};
const union magic scene_resource_magic = {.magic = "__SMSCEN"};
const union magic armature_resource_magic = {.magic = "__SMARMA"};
const union magic clip_resource_magic = {.magic = "__SMCLIP"};

static b8
sm__resource_write_header(struct fs_file *file, struct resource *resource)
{
	union magic mgc = {0};

	if (resource->type == RESOURCE_IMAGE) { mgc = image_resource_magic; }
	else if (resource->type == RESOURCE_MATERIAL) { mgc = material_resource_magic; }
	else if (resource->type == RESOURCE_MESH) { mgc = mesh_resource_magic; }
	else if (resource->type == RESOURCE_SCENE) { mgc = scene_resource_magic; }
	else if (resource->type == RESOURCE_ARMATURE) { mgc = armature_resource_magic; }
	else if (resource->type == RESOURCE_CLIP) { mgc = clip_resource_magic; }

	if (mgc.x == 0)
	{
		log_error(str8_from("invalid magic {u3d}"), resource->type);
		return (false);
	}
	if (resource->name.size == 0)
	{
		log_error(str8_from("resource must have a name"));
		return (false);
	}

	fs_write_u64(file, mgc.x);
	fs_write_str8(file, resource->name);

	return (true);
}

static b8
sm__resource_read_header(struct fs_file *file, struct resource *resource)
{
	union magic mgc = {0};

	resource->type = 0;

	mgc.x = fs_read_u64(file);
	if (mgc.x == image_resource_magic.x) { resource->type = RESOURCE_IMAGE; }
	else if (mgc.x == material_resource_magic.x) { resource->type = RESOURCE_MATERIAL; }
	else if (mgc.x == mesh_resource_magic.x) { resource->type = RESOURCE_MESH; }
	else if (mgc.x == scene_resource_magic.x) { resource->type = RESOURCE_SCENE; }
	else if (mgc.x == armature_resource_magic.x) { resource->type = RESOURCE_ARMATURE; }
	else if (mgc.x == clip_resource_magic.x) { resource->type = RESOURCE_CLIP; }

	if (resource->type == 0)
	{
		log_error(str8_from("invalid magic {u3d}"), mgc.x);
		return (false);
	}

	resource->name = fs_read_str8(file);

	return (true);
}

void
resource_write(struct resource *resource)
{
	struct str8_buf path = str_buf_begin(&RC.arena);
	str_buf_append(&RC.arena, &path, str8_from("dump/"));
	str_buf_append(&RC.arena, &path, resource->name);
	str8 dump_path = str_buf_end(&RC.arena, path);

	struct fs_file file = fs_file_open(dump_path, false);
	if (!file.ok)
	{
		str8_release(&RC.arena, &dump_path);
		log_error(str8_from("[{s}] error while opening file"), resource->name);
		return;
	}
	if (!sm__resource_write_header(&file, resource))
	{
		log_error(str8_from("[{s}] error while writing resource header"), resource->name);
		goto cleanup;
	}

	// clang-format off
	switch (resource->type)
	{
	case RESOURCE_IMAGE:
	{
		if (!fs_image_write(&file, resource->data))
		{

			log_error(str8_from("[{s}] error while material mesh file"), resource->name);
			goto cleanup;
		}
	} break;

	case RESOURCE_MATERIAL:
	{
		if (!fs_material_write(&file, resource->data))
		{
			log_error(str8_from("[{s}] error while material mesh file"), resource->name);
			goto cleanup;
		}
	} break;

	case RESOURCE_MESH:
	{
		if (!fs_mesh_write(&file, resource->data))
		{
			log_error(str8_from("[{s}] error while writing scene file"), resource->name);
			goto cleanup;
		}
	} break;

	case RESOURCE_SCENE:
	{
		// TODO: maybe scene in txt mode
		if (!fs_scene_write(&file, resource->scene_data))
		{
			log_error(str8_from("[{s}] error while writing mesh file"), resource->name);
			goto cleanup;
		}
	} break;

	case RESOURCE_ARMATURE:
	{
		if (!fs_armature_write(&file, resource->armature_data))
		{
			log_error(str8_from("[{s}] error while writing armature file"), resource->name);
			goto cleanup;
		}
	} break;

	case RESOURCE_CLIP:
	{
		if (!fs_clip_write(&file, resource->clip_data))
		{
			log_error(str8_from("[{s}] error while writing clip file"), resource->name);
			goto cleanup;
		}
	} break;

	default:
	{
		log_error(str8_from("Inalid resource type {u3d}"), resource->type);
		goto cleanup;
	}
	}
	// clang-format on

	log_trace(str8_from("[{s} file written successfully"), resource->name);
cleanup:
	str8_release(&RC.arena, &dump_path);
	fs_file_close(&file);
}

static b8
sm__resource_step_over_header(struct fs_file *file)
{
	// back to begin of the file
	if (!PHYSFS_seek(file->fsfile, 0) || !PHYSFS_seek(file->fsfile, sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("seek failed"));
		return (false);
	}

	u32 len;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error getting name offset"));
		return (false);
	}

	// MAGIC NUMBER + STR LEN + STR
	len += sizeof(u32) + sizeof(u64);
	if (!PHYSFS_seek(file->fsfile, len))
	{
		sm__physfs_log_last_error(str8_from("seek failed"));
		return (false);
	}

	assert(PHYSFS_tell(file->fsfile) == len);

	return (true);
}

static b8
sm__resource_read(str8 resource_name, struct resource *resource)
{
	struct fs_file file = fs_file_open(resource_name, true);
	if (!file.ok)
	{
		log_error(str8_from("[{s}] error while opening file"), resource_name);
		return (false);
	}

	if (!sm__resource_step_over_header(&file))
	{
		log_error(str8_from("[{s}] error while stepping over the file header"), resource_name);
		fs_file_close(&file);
		return (false);
	}

	// clang-format off
	switch (resource->type)
	{
	case RESOURCE_IMAGE:
	{
		struct image_resource image = {0};
		if (!fs_image_read(&file, &image))
		{
			log_error(str8_from("[{s}] error while reading image file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.images, image);
		resource->image_data = array_last_item(RC.images);
	} break;

	case RESOURCE_MATERIAL:
	{
		struct material_resource material = {0};
		if (!fs_material_read(&file, &material))
		{
			log_error(str8_from("[{s}] error while reading material file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.materials, material);
		resource->material_data = array_last_item(RC.materials);
	} break;

	case RESOURCE_MESH:
	{
		struct mesh_resource mesh = {0};
		if (!fs_mesh_read(&file, &mesh))
		{
			log_error(str8_from("[{s}] error while reading mesh file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.meshes, mesh);
		resource->mesh_data = array_last_item(RC.meshes);
	} break;

	case RESOURCE_SCENE:
	{
		struct scene_resource scene = {0};
		if (!fs_scene_read(&file, &scene))
		{
			log_error(str8_from("[{s}] error while reading scene file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.scenes, scene);
		resource->scene_data = array_last_item(RC.scenes);
	} break;

	case RESOURCE_ARMATURE:
	{
		struct armature_resource armature = {0};
		if (!fs_armature_read(&file, &armature))
		{
			log_error(str8_from("[{s}] error while reading armature file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.armatures, armature);
		resource->armature_data = array_last_item(RC.armatures);
	} break;

	case RESOURCE_CLIP:
	{
		struct clip_resource clip = {0};
		if (!fs_clip_read(&file, &clip))
		{
			log_error(str8_from("[{s}] error while reading clip file"), resource->name);
			fs_file_close(&file);
			return (false);
		}

		array_push(&RC.arena, RC.clips, clip);
		resource->clip_data = array_last_item(RC.clips);
	} break;

	default: log_error(str8_from("Invalid resource type {u3d}"), resource->type);
	}
	// clang-format on

	log_trace(str8_from("[{s}] resource file read succesfully"), resource->name);

	return (true);
}

static struct resource *
sm__resource_prefetch(str8 resource_name)
{
	struct resource resource = {0};
	struct fs_file file = fs_file_open(resource_name, true);
	if (!file.ok)
	{
		log_error(str8_from("[{s}] error while opening file"), resource_name);
		return (0);
	}
	if (!sm__resource_read_header(&file, &resource))
	{
		log_error(str8_from("[{s}] error while prefetching resource header"), resource_name);
		fs_file_close(&file);
		return (0);
	}
	fs_file_close(&file);

	struct resource *result = resource_push(&resource);

	log_trace(str8_from("[{s}] resource header read succesfully"), result->name);

	return (result);
}

static void
sm__resource_manager_load_defaults(void)
{
	{
		u8 *data = arena_reserve(&RC.arena, sizeof(u8) * 4);
		memset(data, 0xFF, sizeof(u8) * 4);

		struct image_resource default_image = {
		    .width = 1,
		    .height = 1,
		    .pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
		    .data = data,
		};

		RC.defaults.image = default_image;
		RC.defaults.image_resource =
		    resource_make(str8_from("StrangeMachineDefaultImage"), RESOURCE_IMAGE, &RC.defaults.image);
	}

	{
		struct material_resource default_material = {
		    .color = cWHITE,
		    .image = str8_from("StrangeMachineDefaultImage"),
		};
		RC.defaults.material = default_material;
		RC.defaults.material_resource =
		    resource_make(str8_from("StrangeMachineDefaultMaterial"), RESOURCE_MATERIAL, &RC.defaults.material);
	}
}

static void init_miniaudio();

b8
resource_manager_init(i8 *argv[], str8 assets_folder)
{
	struct buf m_resource = base_memory_reserve(MB(15));

	arena_make(&RC.arena, m_resource);
	arena_validate(&RC.arena);

	array_set_cap(&RC.arena, RC.resources, 64);
	array_set_cap(&RC.arena, RC.images, 64);
	array_set_cap(&RC.arena, RC.meshes, 64);
	array_set_cap(&RC.arena, RC.clips, 64);
	array_set_cap(&RC.arena, RC.materials, 64);
	array_set_cap(&RC.arena, RC.armatures, 16);
	array_set_cap(&RC.arena, RC.scenes, 16);
	array_set_cap(&RC.arena, RC.shaders, 16);
	array_set_cap(&RC.arena, RC.frag_shaders, 32);
	array_set_cap(&RC.arena, RC.vert_shaders, 32);

	memset(RC.resources, 0x0, sizeof(struct resource) * 64);
	memset(RC.images, 0x0, sizeof(struct image_resource) * 64);
	memset(RC.meshes, 0x0, sizeof(struct mesh_resource) * 64);
	memset(RC.clips, 0x0, sizeof(struct clip_resource) * 64);
	memset(RC.materials, 0x0, sizeof(struct material_resource) * 64);
	memset(RC.armatures, 0x0, sizeof(struct armature_resource) * 16);
	memset(RC.scenes, 0x0, sizeof(struct scene_resource) * 16);
	memset(RC.shaders, 0x0, sizeof(struct shader_resource) * 16);
	memset(RC.frag_shaders, 0x0, sizeof(struct frag_shader_resource) * 32);
	memset(RC.vert_shaders, 0x0, sizeof(struct vert_shader_resource) * 32);

	PHYSFS_Allocator a = {
	    .Malloc = (void *(*)(unsigned long long))sm__physfs_malloc,
	    .Realloc = (void *(*)(void *, unsigned long long))sm__physfs_realloc,
	    .Free = sm__physfs_free,
	};
	PHYSFS_setAllocator(&a);

	if (!PHYSFS_init(argv[0]))
	{
		sm__physfs_log_last_error(str8_from("error while initializing."));
		return (false);
	}

	if (!PHYSFS_mount(assets_folder.idata, "/", 1))
	{
		sm__physfs_log_last_error(str8_from("error while mounting"));
		return (false);
	}

	// if (!PHYSFS_mount("assets/exported/assets.zip", "/", 1))
	// {
	// 	sm__physfs_log_last_error(str8_from("error while mounting"));
	// 	return (false);
	// }

	if (!PHYSFS_setWriteDir(assets_folder.idata))
	{
		sm__physfs_log_last_error(str8_from("error while setting write dir"));
		return (false);
	}

	RC.map = str8_resource_map_make(&RC.arena);

	sm__resource_manager_load_defaults();

	// init_miniaudio();

	return (true);
}

void
resource_manager_teardown(void)
{
	PHYSFS_deinit();
}

struct resource
resource_make(str8 name, u32 type, void *ptr)
{
	struct resource result = {
	    .name = name,
	    .type = type,
	    .data = ptr,
	    .refs = {0},
	};

	return (result);
}

struct resource *
resource_make_reference(struct resource *resource)
{
	assert(resource);

	rc_increment(&resource->refs);

	return (resource);
}

void
resource_unmake_reference(struct resource *resource)
{
	assert(resource);

	rc_decrement(&resource->refs);
}

void
resource_validate(struct resource *resource)
{
	assert(resource->name.size != 0);
	assert(resource->type != 0);
}

struct resource *
resource_push(struct resource *resource)
{
	struct resource *result = 0;

	resource_validate(resource);

	array_push(&RC.arena, RC.resources, *resource);
	result = array_last_item(RC.resources);

	struct str8_resource_result result_map = str8_resource_map_put(&RC.arena, &RC.map, result->name, result);
	if (result_map.ok)
	{
		log_error(str8_from("[{s}] duplicated resource!"), resource->name);
		assert(0);
	}

	return (result);
}

struct resource *
resource_get_by_name(str8 name)
{
	struct resource *result = 0;

	struct str8_resource_result result_map = str8_resource_map_get(&RC.map, name);
	if (!result_map.ok) { return (result); }

	result = result_map.value;
	return (result);
}

void
resource_for_each(b8 (*cb)(str8, struct resource *, void *), void *user_data)
{
	str8_resource_for_each(&RC.map, cb, user_data);
}

struct arena *
resource_get_arena(void)
{
	return &RC.arena;
}

const u32 FS_FILETYPE_REGULAR = PHYSFS_FILETYPE_REGULAR;
const u32 FS_FILETYPE_DIRECTORY = PHYSFS_FILETYPE_DIRECTORY;
const u32 FS_FILETYPE_SYMLINK = PHYSFS_FILETYPE_SYMLINK;
const u32 FS_FILETYPE_OTHER = PHYSFS_FILETYPE_OTHER;

struct fs_file
fs_file_open(str8 name, b8 read_only)
{
	struct fs_file result = {0};

	if (read_only)
	{
		if (!PHYSFS_exists(name.idata))
		{
			log_error(str8_from("[{s}] file does not exist"), name);
			return (result);
		}

		PHYSFS_Stat stat;
		if (!PHYSFS_stat(name.idata, &stat))
		{
			log_error(str8_from("[{s}] error getting file status"), name);
			return (result);
		}

		result.status.filesize = stat.filesize;
		result.status.modtime = stat.modtime;
		result.status.createtime = stat.createtime;
		result.status.accesstime = stat.accesstime;
		result.status.filetype = stat.filetype;
		result.status.readonly = stat.readonly;

		if (result.status.filetype != PHYSFS_FILETYPE_REGULAR)
		{
			log_error(str8_from("[{s}] it's not a regular file"), name);
			return (result);
		}

		result.fsfile = PHYSFS_openRead(name.idata);
		if (!result.fsfile)
		{
			sm__physfs_log_last_error(str8_from("error while open (read mode)"));
			return (result);
		}
	}
	else
	{
		result.fsfile = PHYSFS_openWrite(name.idata);
		if (!result.fsfile)
		{
			sm__physfs_log_last_error(str8_from("error while open (write mode)"));
			return (result);
		}
	}

	result.ok = true;

	return (result);
}

struct fs_file
fs_file_open_read_cstr(const i8 *name)
{
	str8 filename = (str8){.cidata = name, (u32)strlen(name)};
	return fs_file_open(filename, true);
}

struct fs_file
fs_file_open_write_cstr(const i8 *name)
{
	str8 filename = (str8){.cidata = name, (u32)strlen(name)};
	return fs_file_open(filename, false);
}

void
fs_file_close(struct fs_file *file)
{
	assert(file->ok);
	assert(file->fsfile);

	PHYSFS_close(file->fsfile);

	file->fsfile = 0;
	file->ok = false;
	file->status = (struct fs_stat){0};
}

i64
fs_file_write(struct fs_file *file, const void *src, size_t size)
{
	i64 result;

	result = PHYSFS_writeBytes(file->fsfile, src, size);

	return (result);
}

i64
fs_file_read(struct fs_file *file, void *buffer, size_t size)
{
	i64 result;

	result = PHYSFS_readBytes(file->fsfile, buffer, size);

	return (result);
}

i32
fs_file_eof(struct fs_file *file)
{
	i32 result;

	result = PHYSFS_eof(file->fsfile);

	return (result);
}

i64
fs_file_tell(struct fs_file *file)
{
	i32 result;

	result = PHYSFS_tell(file->fsfile);

	return (result);
}

i32
fs_file_seek(struct fs_file *file, u64 position)
{
	i32 result;

	result = PHYSFS_seek(file->fsfile, position);

	return (result);
}

const i8 *
fs_file_last_error(void)
{
	const i8 *result;

	PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
	result = PHYSFS_getErrorByCode(err);

	return (result);
}

static b8
fs_image_write(struct fs_file *file, struct image_resource *image)
{
	log_trace(str8_from("    __// writing material"));

	fs_write_u32(file, image->width);
	fs_write_u32(file, image->height);
	fs_write_u32(file, image->pixel_format);

	u32 size = resource_image_size(image->width, image->height, image->pixel_format);

	i64 bw = PHYSFS_writeBytes(file->fsfile, image->data, size * sizeof(u8));
	if (bw != (i64)(size * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing image data"));
		assert(false);
	}

	return (true);
}

static b8
fs_image_read(struct fs_file *file, struct image_resource *image)
{
	image->width = fs_read_u32(file);
	image->height = fs_read_u32(file);
	image->pixel_format = fs_read_u32(file);

	u32 size = resource_image_size(image->width, image->height, image->pixel_format);

	image->data = arena_reserve(&RC.arena, size);

	i64 br = PHYSFS_readBytes(file->fsfile, image->data, size * sizeof(u8));
	if (br != (i64)(size * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while reading image data"));
		return (false);
	}

	return (true);
}

static b8
fs_material_write(struct fs_file *file, struct material_resource *material)
{
	log_trace(str8_from("    __// writing material"));

	fs_write_u32(file, material->color.hex);
	fs_write_b8(file, material->double_sided);
	fs_write_str8(file, material->image);

	// TODO: handle custom shader

	return (true);
}

static b8
fs_material_read(struct fs_file *file, struct material_resource *material)
{
	material->color.hex = fs_read_u32(file);
	material->double_sided = fs_read_b8(file);
	material->image = fs_read_str8(file);

	// TODO: handle custom shader

	return (true);
}

static b8
fs_mesh_write(struct fs_file *file, struct mesh_resource *mesh)
{
	log_trace(str8_from("    __// writing mesh"));

	fs_write_v3a(file, mesh->positions);
	fs_write_v2a(file, mesh->uvs);
	fs_write_v4a(file, mesh->colors);
	fs_write_v3a(file, mesh->normals);
	fs_write_u32a(file, mesh->indices);

	fs_write_u32(file, mesh->flags);

	fs_write_b8(file, mesh->skin_data.is_skinned);
	if (mesh->skin_data.is_skinned)
	{
		fs_write_v4a(file, mesh->skin_data.weights);
		fs_write_iv4a(file, mesh->skin_data.influences);
	}

	return (true);
}

static b8
fs_mesh_read(struct fs_file *file, struct mesh_resource *mesh)
{
	mesh->positions = fs_read_v3a(file);
	mesh->uvs = fs_read_v2a(file);
	mesh->colors = fs_read_v4a(file);
	mesh->normals = fs_read_v3a(file);
	mesh->indices = fs_read_u32a(file);

	mesh->flags = fs_read_u32(file);

	mesh->skin_data.is_skinned = fs_read_b8(file);
	if (mesh->skin_data.is_skinned)
	{
		mesh->skin_data.weights = fs_read_v4a(file);
		mesh->skin_data.influences = fs_read_iv4a(file);
	}

	return (true);
}

static b8
fs_scene_write(struct fs_file *file, struct scene_resource *scene)
{
	log_trace(str8_from("    __// writing scene"));

	u32 len = array_len(scene->nodes);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		struct scene_node *node = &scene->nodes[i];
		fs_write_str8(file, node->name);

		fs_write_i32(file, node->parent_index);
		fs_write_i32a(file, node->children);
		fs_write_v3(file, node->position);
		fs_write_v3(file, node->scale);
		fs_write_v4(file, node->rotation);

		fs_write_u32(file, node->prop);

		str8 empty = str8_from("\0");
		if (node->mesh.size > 0) { fs_write_str8(file, node->mesh); }
		else { fs_write_str8(file, empty); }

		if (node->material.size > 0) { fs_write_str8(file, node->material); }
		else { fs_write_str8(file, empty); }

		if (node->armature.size > 0) { fs_write_str8(file, node->armature); }
		else { fs_write_str8(file, empty); }
	}

	return (true);
}

static b8
fs_scene_read(struct fs_file *file, struct scene_resource *scene)
{
	u32 len = fs_read_u32(file);
	array_set_len(&RC.arena, scene->nodes, len);
	for (u32 i = 0; i < len; ++i)
	{
		struct scene_node *node = &scene->nodes[i];
		node->name = fs_read_str8(file);

		node->parent_index = fs_read_i32(file);
		node->children = fs_read_i32a(file);
		node->position = fs_read_v3(file);
		node->scale = fs_read_v3(file);
		node->rotation = fs_read_v4(file);

		node->prop = fs_read_u32(file);

		str8 name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0"))) { node->mesh = str8_empty_const; }
		else { node->mesh = name; }

		name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0"))) { node->material = str8_empty_const; }
		else { node->material = name; }

		name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0"))) { node->armature = str8_empty_const; }
		else { node->armature = name; }
	}

	return (true);
}

static b8
fs_armature_write(struct fs_file *file, struct armature_resource *armature)
{
	log_trace(str8_from("    __// writing armature"));

	// writing rest pose
	u32 len = array_len(armature->rest.joints);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		fs_write_v4(file, armature->rest.joints[i].translation);
		fs_write_v4(file, armature->rest.joints[i].rotation);
		fs_write_v3(file, armature->rest.joints[i].scale);
	}
	fs_write_i32a(file, armature->rest.parents);

	// writing bind pose
	len = array_len(armature->bind.joints);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		fs_write_v4(file, armature->bind.joints[i].translation);
		fs_write_v4(file, armature->bind.joints[i].rotation);
		fs_write_v3(file, armature->bind.joints[i].scale);
	}
	fs_write_i32a(file, armature->bind.parents);

	fs_write_m4a(file, armature->inverse_bind);
	// TODO: write names

	return (true);
}

static b8
fs_armature_read(struct fs_file *file, struct armature_resource *armature)
{
	// reading rest pose
	u32 len = fs_read_u32(file);
	array_set_len(&RC.arena, armature->rest.joints, len);
	for (u32 i = 0; i < len; ++i)
	{
		armature->rest.joints[i].translation = fs_read_v4(file);
		armature->rest.joints[i].rotation = fs_read_v4(file);
		armature->rest.joints[i].scale = fs_read_v3(file);
	}
	armature->rest.parents = fs_read_i32a(file);

	// reading bind pose
	len = fs_read_u32(file);
	array_set_len(&RC.arena, armature->bind.joints, len);
	for (u32 i = 0; i < len; ++i)
	{
		armature->bind.joints[i].translation = fs_read_v4(file);
		armature->bind.joints[i].rotation = fs_read_v4(file);
		armature->bind.joints[i].scale = fs_read_v3(file);
	}
	armature->bind.parents = fs_read_i32a(file);

	armature->inverse_bind = fs_read_m4a(file);
	// TODO: read names

	return (true);
}

static void
sm__write_track(struct fs_file *file, struct track *track)
{
	assert(track->interpolation == INTERPOLATION_CONSTANT || track->interpolation == INTERPOLATION_LINEAR ||
	       track->interpolation == INTERPOLATION_CUBIC);
	fs_write_u32(file, track->interpolation);

	assert(track->track_type == TRACK_TYPE_SCALAR || track->track_type == TRACK_TYPE_V3 ||
	       track->track_type == TRACK_TYPE_V4);
	fs_write_u32(file, track->track_type);

	// clang-format off
	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
	{
		u32 len = array_len(track->frames_scalar);
		fs_write_u32(file, len);

		i64 bw =
		    PHYSFS_writeBytes(file->fsfile, track->frames_scalar, len * sizeof(*track->frames_scalar));
		if (bw != (i64)(len * sizeof(*track->frames_scalar)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_scalar"));
			assert(0);
		}
	} break;

	case TRACK_TYPE_V3:
	{
		u32 len = array_len(track->frames_v3);
		fs_write_u32(file, len);

		i64 bw = PHYSFS_writeBytes(file->fsfile, track->frames_v3, len * sizeof(*track->frames_v3));
		if (bw != (i64)(len * sizeof(*track->frames_v3)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_v3"));
			assert(0);
		}
	} break;

	case TRACK_TYPE_V4:
	{
		u32 len = array_len(track->frames_v4);
		fs_write_u32(file, len);

		i64 bw = PHYSFS_writeBytes(file->fsfile, track->frames_v4, len * sizeof(*track->frames_v4));
		if (bw != (i64)(len * sizeof(*track->frames_v4)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_v4"));
			assert(0);
		}
	} break;
	}
	// clang-format on

	fs_write_i32a(file, track->sampled_frames);
}

static void
sm__read_track(struct fs_file *file, struct track *track)
{
	u32 interp = 0;
	// track->interpolation = fs_read_u32(file);
	interp = fs_read_u32(file);
	memcpy((u32 *)&track->interpolation, &interp, sizeof(u32));
	assert(track->interpolation == INTERPOLATION_CONSTANT || track->interpolation == INTERPOLATION_LINEAR ||
	       track->interpolation == INTERPOLATION_CUBIC);

	track->track_type = fs_read_u32(file);
	assert(track->track_type == TRACK_TYPE_SCALAR || track->track_type == TRACK_TYPE_V3 ||
	       track->track_type == TRACK_TYPE_V4);

	switch (track->track_type)
	{
	case TRACK_TYPE_SCALAR:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_scalar, len);

			i64 br =
			    PHYSFS_readBytes(file->fsfile, track->frames_scalar, len * sizeof(*track->frames_scalar));
			if (br != (i64)(len * sizeof(*track->frames_scalar)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_scalar"));
				assert(0);
			}
		}
		break;

	case TRACK_TYPE_V3:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_v3, len);

			i64 br = PHYSFS_readBytes(file->fsfile, track->frames_v3, len * sizeof(*track->frames_v3));
			if (br != (i64)(len * sizeof(*track->frames_v3)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_v3"));
				assert(0);
			}
		}
		break;

	case TRACK_TYPE_V4:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_v4, len);

			i64 br = PHYSFS_readBytes(file->fsfile, track->frames_v4, len * sizeof(*track->frames_v4));
			if (br != (i64)(len * sizeof(*track->frames_v4)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_v4"));
				assert(0);
			}
		}
		break;
	}

	track->sampled_frames = fs_read_i32a(file);
}

static b8
fs_clip_write(struct fs_file *file, struct clip_resource *clip)
{
	log_trace(str8_from("    __// writing clip"));

	u32 len = array_len(clip->tracks);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		fs_write_u32(file, clip->tracks[i].id);
		sm__write_track(file, &clip->tracks[i].position);
		sm__write_track(file, &clip->tracks[i].rotation);
		sm__write_track(file, &clip->tracks[i].scale);
	}

	fs_write_b8(file, clip->looping);
	fs_write_f32(file, clip->start_time);
	fs_write_f32(file, clip->end_time);

	return (true);
}

static b8
fs_clip_read(struct fs_file *file, struct clip_resource *clip)
{
	u32 len = fs_read_u32(file);
	array_set_len(&RC.arena, clip->tracks, len);
	memset(clip->tracks, 0x0, len * sizeof(struct transform_track));
	for (u32 i = 0; i < len; ++i)
	{
		clip->tracks[i].id = fs_read_u32(file);
		sm__read_track(file, &clip->tracks[i].position);
		sm__read_track(file, &clip->tracks[i].rotation);
		sm__read_track(file, &clip->tracks[i].scale);
	}

	clip->looping = fs_read_b8(file);
	clip->start_time = fs_read_f32(file);
	clip->end_time = fs_read_f32(file);

	return (true);
}

static void
fs_write_str8(struct fs_file *file, str8 str)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &str.size, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing str8 len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, str.idata, str.size);
	if (bw != (i64)str.size)
	{
		sm__physfs_log_last_error(str8_from("error while writing str8"));
		assert(0);
	}
}

static str8
fs_read_str8(struct fs_file *file)
{
	str8 result;

	u32 str_len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &str_len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading str8"));
		assert(0);
	}

	i8 *str_data = arena_reserve(&RC.arena, str_len + 1);
	br = PHYSFS_readBytes(file->fsfile, str_data, str_len);
	if (br != (i64)str_len)
	{
		sm__physfs_log_last_error(str8_from("error while reading str8"));
		assert(0);
	}

	result.size = str_len;
	result.idata = str_data;
	result.idata[str_len] = 0;

	return (result);
}

static void
fs_write_b8(struct fs_file *file, b8 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(b8));
	if (bw != (i64)sizeof(b8))
	{
		sm__physfs_log_last_error(str8_from("error while writing b8"));
		assert(0);
	}
}

static b8
fs_read_b8(struct fs_file *file)
{
	b8 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(b8));
	if (br != (i64)sizeof(b8))
	{
		sm__physfs_log_last_error(str8_from("error while reading b8"));
		assert(0);
	}

	return (result);
}

static void
fs_write_b8a(struct fs_file *file, array(b8) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing b8 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(b8));
	if (bw != (i64)(len * sizeof(b8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing b8 array"));
		assert(0);
	}
}

static array(b8) fs_read_b8a(struct fs_file *file)
{
	array(b8) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading b8 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(b8));
	if (br != (i64)(len * sizeof(b8)))
	{
		sm__physfs_log_last_error(str8_from("error while reading b8 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u8(struct fs_file *file, u8 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(u8));
	if (bw != (i64)sizeof(u8))
	{
		sm__physfs_log_last_error(str8_from("error while writing u8"));
		assert(0);
	}
}

static u8
fs_read_u8(struct fs_file *file)
{
	u8 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(u8));
	if (br != (i64)sizeof(u8))
	{
		sm__physfs_log_last_error(str8_from("error while reading u8"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u8a(struct fs_file *file, array(u8) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing u8 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u8));
	if (bw != (i64)(len * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u8 array"));
		assert(0);
	}
}

static array(u8) fs_read_u8a(struct fs_file *file)
{
	array(u8) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading u8 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u8));
	if (br != (i64)(len * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u8 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_i32(struct fs_file *file, i32 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(i32));
	if (bw != (i64)sizeof(i32))
	{
		sm__physfs_log_last_error(str8_from("error while writing i32"));
		assert(0);
	}
}

static i32
fs_read_i32(struct fs_file *file)
{
	i32 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(i32));
	if (br != (i64)sizeof(i32))
	{
		sm__physfs_log_last_error(str8_from("error while reading i32"));
		assert(0);
	}

	return (result);
}

static void
fs_write_i32a(struct fs_file *file, array(i32) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing i32 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(i32));
	if (bw != (i64)(len * sizeof(i32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing i32 array"));
		assert(0);
	}
}

static array(i32) fs_read_i32a(struct fs_file *file)
{
	array(i32) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading i32 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(i32));
	if (br != (i64)(len * sizeof(i32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading i32 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u32(struct fs_file *file, u32 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing u32"));
		assert(0);
	}
}

static u32
fs_read_u32(struct fs_file *file)
{
	u32 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading u32"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u32a(struct fs_file *file, array(u32) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing u32 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u32));
	if (bw != (i64)(len * sizeof(u32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u32 array"));
		assert(0);
	}
}

static array(u32) fs_read_u32a(struct fs_file *file)
{
	array(u32) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading u32 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u32));
	if (br != (i64)(len * sizeof(u32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u32 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u64(struct fs_file *file, u64 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(u64));
	if (bw != (i64)sizeof(u64))
	{
		sm__physfs_log_last_error(str8_from("error while writing u64"));
		assert(0);
	}
}

static u64
fs_read_u64(struct fs_file *file)
{
	u64 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(u64));
	if (br != (i64)sizeof(u64))
	{
		sm__physfs_log_last_error(str8_from("error while reading u64"));
		assert(0);
	}

	return (result);
}

static void
fs_write_u64a(struct fs_file *file, array(u64) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u64))
	{
		sm__physfs_log_last_error(str8_from("error while writing u64 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u64));
	if (bw != (i64)(len * sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u64 array"));
		assert(0);
	}
}

static array(u64) fs_read_u64a(struct fs_file *file)
{
	array(u64) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u64))
	{
		sm__physfs_log_last_error(str8_from("error while reading u64 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u64));
	if (br != (i64)(len * sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u64 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_f32(struct fs_file *file, f32 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(f32));
	if (bw != (i64)sizeof(f32))
	{
		sm__physfs_log_last_error(str8_from("error while writing f32"));
		assert(0);
	}
}

static f32
fs_read_f32(struct fs_file *file)
{
	f32 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(f32));
	if (br != (i64)sizeof(f32))
	{
		sm__physfs_log_last_error(str8_from("error while reading f32"));
		assert(0);
	}

	return (result);
}

static void
fs_write_f32a(struct fs_file *file, array(f32) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing f32 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(f32));
	if (bw != (i64)(len * sizeof(f32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing f32 array"));
		assert(0);
	}
}

static array(f32) fs_read_f32a(struct fs_file *file)
{
	array(f32) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading f32 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(f32));
	if (br != (i64)(len * sizeof(f32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading f32 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v2(struct fs_file *file, v2 v)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, v.data, sizeof(v2));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing v2"));
		assert(0);
	}
}

static v2
fs_read_v2(struct fs_file *file)
{
	v2 result;

	i64 br = PHYSFS_readBytes(file->fsfile, result.data, sizeof(v2));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading v2"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v2a(struct fs_file *file, array(v2) v)
{
	u32 len = array_len(v);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing v2 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v2));
	if (bw != (i64)(len * sizeof(v2)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v2 array"));
		assert(0);
	}
}

static array(v2) fs_read_v2a(struct fs_file *file)
{
	array(v2) result = 0;

	u32 len = 0;
	i64 bw = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading v2 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	bw = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v2));
	if (bw != (i64)(len * sizeof(v2)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v2 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v3(struct fs_file *file, v3 v)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, v.data, sizeof(v3));
	if (bw != (i64)sizeof(v3))
	{
		sm__physfs_log_last_error(str8_from("error while writing v3"));
		assert(0);
	}
}

static v3
fs_read_v3(struct fs_file *file)
{
	v3 result;

	i64 br = PHYSFS_readBytes(file->fsfile, result.data, sizeof(v3));
	if (br != (i64)sizeof(v3))
	{
		sm__physfs_log_last_error(str8_from("error while reading v3"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v3a(struct fs_file *file, array(v3) v)
{
	u32 len = array_len(v);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing v3 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v3));
	if (bw != (i64)(len * sizeof(v3)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v3 array"));
		assert(0);
	}
}

static array(v3) fs_read_v3a(struct fs_file *file)
{
	array(v3) result = 0;

	u32 len = 0;
	i64 bw = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading v3 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	bw = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v3));
	if (bw != (i64)(len * sizeof(v3)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v3 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v4(struct fs_file *file, v4 v)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, v.data, sizeof(v4));
	if (bw != (i64)sizeof(v4))
	{
		sm__physfs_log_last_error(str8_from("error while writing v4"));
		assert(0);
	}
}

static v4
fs_read_v4(struct fs_file *file)
{
	v4 result;

	i64 br = PHYSFS_readBytes(file->fsfile, result.data, sizeof(v4));
	if (br != (i64)sizeof(v4))
	{
		sm__physfs_log_last_error(str8_from("error while reading v4"));
		assert(0);
	}

	return (result);
}

static void
fs_write_v4a(struct fs_file *file, array(v4) v)
{
	u32 len = array_len(v);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing v4 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v4));
	if (bw != (i64)(len * sizeof(v4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v4 array"));
		assert(0);
	}
}

static array(v4) fs_read_v4a(struct fs_file *file)
{
	array(v4) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading v4 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v4));
	if (br != (i64)(len * sizeof(v4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v4 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_iv4(struct fs_file *file, iv4 v)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, v.data, sizeof(iv4));
	if (bw != (i64)sizeof(iv4))
	{
		sm__physfs_log_last_error(str8_from("error while writing iv4"));
		assert(0);
	}
}

static iv4
fs_read_iv4(struct fs_file *file)
{
	iv4 result;

	i64 br = PHYSFS_readBytes(file->fsfile, result.data, sizeof(iv4));
	if (br != (i64)sizeof(iv4))
	{
		sm__physfs_log_last_error(str8_from("error while reading iv4"));
		assert(0);
	}

	return (result);
}

static void
fs_write_iv4a(struct fs_file *file, array(iv4) v)
{
	u32 len = array_len(v);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing iv4 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(iv4));
	if (bw != (i64)(len * sizeof(iv4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing iv4 array"));
		assert(0);
	}
}

static array(iv4) fs_read_iv4a(struct fs_file *file)
{
	array(iv4) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading iv4 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(iv4));
	if (br != (i64)(len * sizeof(iv4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading iv4 array"));
		assert(0);
	}

	return (result);
}

static void
fs_write_m4(struct fs_file *file, m4 v)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, v.data, sizeof(m4));
	if (bw != (i64)sizeof(m4))
	{
		sm__physfs_log_last_error(str8_from("error while writing m4"));
		assert(0);
	}
}

static m4
fs_read_m4(struct fs_file *file)
{
	m4 result;

	i64 br = PHYSFS_readBytes(file->fsfile, result.data, sizeof(m4));
	if (br != (i64)sizeof(m4))
	{
		sm__physfs_log_last_error(str8_from("error while reading m4"));
		assert(0);
	}

	return (result);
}

static void
fs_write_m4a(struct fs_file *file, array(m4) v)
{
	u32 len = array_len(v);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing m4 array len"));
		assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(m4));
	if (bw != (i64)(len * sizeof(m4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing m4 array"));
		assert(0);
	}
}

static array(m4) fs_read_m4a(struct fs_file *file)
{
	array(m4) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading m4 array len"));
		assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(m4));
	if (br != (i64)(len * sizeof(m4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading m4 array"));
		assert(0);
	}

	return (result);
}

#if 0

static void *
sm__ma_malloc(size_t size, sm__maybe_unused void *user_data)
{
	void *result;

	log_debug(str8_from("Calling malloc: {u6d}"), size);

	result = arena_reserve(&RC.arena, size);
	return (result);
}

static void *
sm__ma_realloc(void *ptr, size_t size, sm__maybe_unused void *user_data)
{
	void *result;

	log_debug(str8_from("Calling realloc: {u6d}"), size);

	result = arena_resize(&RC.arena, ptr, size);
	return (result);
}

static void
sm__ma_free(void *ptr, sm__maybe_unused void *user_data)
{
	log_debug(str8_from("Calling free"));
	arena_free(&RC.arena, ptr);
}

static ma_allocation_callbacks
sm__ma_allocation_callbacks_init(void)
{
	ma_allocation_callbacks result;

	result.pUserData = 0;
	result.onMalloc = sm__ma_malloc;
	result.onRealloc = sm__ma_realloc;
	result.onFree = sm__ma_free;

	return (result);
}

static ma_result
sm__physfs_vfs_open(sm__maybe_unused ma_vfs *vfs, const i8 *file_path, ma_uint32 open_mode, ma_vfs_file *file)
{
	if (file == 0) { return (MA_INVALID_ARGS); }

	*file = 0;

	if (file_path == 0 || open_mode == 0) { return (MA_INVALID_ARGS); }

	struct fs_file *f = arena_reserve(&RC.arena, sizeof(struct fs_file));

	if ((open_mode & MA_OPEN_MODE_READ) != 0)
	{
		assert((open_mode & MA_OPEN_MODE_WRITE) == 0);
		*f = fs_file_open_read_cstr(file_path);
	}
	else { *f = fs_file_open_write_cstr(file_path); }

	if (!f->ok) { return (MA_ERROR); }

	*file = f;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_close(sm__maybe_unused ma_vfs *pVFS, ma_vfs_file file)
{
	assert(file != 0);

	struct fs_file *f = (struct fs_file *)file;

	fs_file_close(f);
	arena_free(&RC.arena, f);

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_read(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, void *dest, size_t size, size_t *bytes_read)
{
	size_t result;

	assert(file != 0);
	assert(dest != 0);

	struct fs_file *f = (struct fs_file *)file;
	result = PHYSFS_readBytes(f->fsfile, dest, size);
	if (bytes_read != 0) { *bytes_read = result; }

	if (result != size)
	{
		if (result == 0 && PHYSFS_eof(f->fsfile)) { return (MA_AT_END); }
		else
		{
			sm__physfs_log_last_error(str8_from("error while reading bytes"));
			return (MA_ERROR);
		}
	}

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_write(
    sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, const void *src, size_t size, size_t *bytes_written)
{
	size_t result;

	assert(file != 0);
	assert(src != 0);

	struct fs_file *f = (struct fs_file *)file;
	result = PHYSFS_writeBytes(f->fsfile, src, size);

	if (bytes_written != 0) { *bytes_written = result; }

	// TODO: handle error
	assert(result == size);

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_seek(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
	assert(file != NULL);

	struct fs_file *f = (struct fs_file *)file;

	i64 size = f->status.filesize;
	assert(size != -1);

	i64 position = 0;

	switch (origin)
	{
	case ma_seek_origin_start: position = offset; break;
	case ma_seek_origin_end: position = size + offset; break;
	case ma_seek_origin_current:
		{
			i64 current = PHYSFS_tell(f->fsfile);
			if (current == -1)
			{
				sm__physfs_log_last_error(str8_from("error while telling."));
				return (MA_ERROR);
			}

			position = current + offset;
		}
		break;
	}

	assert(position >= 0);
	assert(position <= size); // consider EOF

	i32 err = PHYSFS_seek(f->fsfile, (u64)position);
	if (err == 0)
	{
		sm__physfs_log_last_error(str8_from("error while seeking."));
		return (MA_ERROR);
	}

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_tell(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_int64 *cursor)
{
	assert(file != 0);
	assert(cursor != 0);

	struct fs_file *f = file;

	i64 result = PHYSFS_tell(f->fsfile);
	if (result == -1)
	{
		*cursor = 0;
		sm__physfs_log_last_error(str8_from("error while telling."));
		return (MA_ERROR);
	}

	*cursor = result;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_info(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_file_info *file_info)
{
	assert(file != 0);
	assert(file_info != 0);

	struct fs_file *f = file;

	assert(f->status.filesize != -1);
	file_info->sizeInBytes = f->status.filesize;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_openw(sm__maybe_unused ma_vfs *vfs, sm__maybe_unused const wchar_t *file_path,
    sm__maybe_unused ma_uint32 openMode, sm__maybe_unused ma_vfs_file *file)
{
	assert(0 && "openw");
}

static ma_result
sm__default_vfs_init(ma_default_vfs *vfs, sm__maybe_unused const ma_allocation_callbacks *allocation_cb)
{
	if (vfs == 0) { return (MA_INVALID_ARGS); }

	vfs->cb.onOpen = sm__physfs_vfs_open;
	vfs->cb.onOpenW = sm__physfs_vfs_openw;
	vfs->cb.onClose = sm__physfs_vfs_close;
	vfs->cb.onRead = sm__physfs_vfs_read;
	vfs->cb.onWrite = sm__physfs_vfs_write;
	vfs->cb.onSeek = sm__physfs_vfs_seek;
	vfs->cb.onTell = sm__physfs_vfs_tell;
	vfs->cb.onInfo = sm__physfs_vfs_info;
	vfs->allocationCallbacks = sm__ma_allocation_callbacks_init();

	return (MA_SUCCESS);
}

static void
init_miniaudio()
{
	static ma_resource_manager_config config_resource;
	static ma_resource_manager resource_manager;

	config_resource = ma_resource_manager_config_init();
	static ma_default_vfs vfs = {0};
	sm__default_vfs_init(&vfs, 0);
	config_resource.pVFS = &vfs;
	config_resource.allocationCallbacks = sm__ma_allocation_callbacks_init();

	ma_result result = ma_resource_manager_init(&config_resource, &resource_manager);
	if (result != MA_SUCCESS)
	{
		log_error(str8_from("failed to initialize the resource manager."));
		exit(1);
	}

	static ma_engine_config config_engine;
	static ma_engine engine;

	config_engine = ma_engine_config_init();
	config_engine.pResourceManager = &resource_manager;
	config_engine.allocationCallbacks = sm__ma_allocation_callbacks_init();

	result = ma_engine_init(&config_engine, &engine);
	if (result != MA_SUCCESS)
	{
		ma_device_uninit(config_engine.pDevice);
		log_error(str8_from("failed to initialize the engine."));
		exit(1);
	}

	ma_engine_listener_set_position(&engine, 0, 0.0f, 0.0f, 0.0f);
	ma_engine_listener_set_direction(&engine, 0, 0.0f, 0.0f, 0.0f);
	ma_engine_listener_set_world_up(&engine, 0, 0.0f, 1.0f, 0.0f);

	static ma_sound sound;
	result = ma_sound_init_from_file(&engine, "exported/ghost-love.wav", MA_SOUND_FLAG_STREAM, 0, 0, &sound);
	if (result != MA_SUCCESS)
	{
		ma_engine_uninit(&engine);
		log_error(str8_from("failed to load sound file."));
		exit(1);
	}

	ma_sound_set_pinned_listener_index(&sound, 0);
	ma_sound_set_position(&sound, 10.0f, 0.0f, 0.0f);

	ma_sound_set_looping(&sound, 1);
	ma_sound_start(&sound);

	// core_wait(5.0f);
	// printf("Press Enter to stop recording...\n");
	// getchar();
	//
	// ma_sound_uninit(&sound);
	// ma_engine_uninit(&engine);
}
#endif

static struct frag_shader_resource *
load_frag_shader(str8 name)
{
	struct frag_shader_resource *result;

	struct fs_file file = fs_file_open(name, true);
	if (!file.ok) { return (0); }

	str8 fragment = {
	    .data = arena_reserve(&RC.arena, (u32)file.status.filesize + 1), .size = (u32)file.status.filesize};

	u32 read_bytes = (u32)PHYSFS_readBytes(file.fsfile, fragment.data, fragment.size);
	assert(read_bytes == file.status.filesize);
	fragment.data[file.status.filesize] = 0;

	struct frag_shader_resource shader = {.name = str8_dup(&RC.arena, name), .fragment = fragment};

	array_push(&RC.arena, RC.frag_shaders, shader);
	result = &RC.frag_shaders[array_len(RC.frag_shaders) - 1];

	fs_file_close(&file);

	return (result);
}

static struct vert_shader_resource *
load_vert_shader(str8 name)
{
	struct vert_shader_resource *result;

	struct fs_file file = fs_file_open(name, true);
	if (!file.ok) return (0);

	str8 vertex = {
	    .data = arena_reserve(&RC.arena, (u32)file.status.filesize + 1), .size = (u32)file.status.filesize};

	u32 read_bytes = (u32)PHYSFS_readBytes(file.fsfile, vertex.data, vertex.size);
	assert(read_bytes == file.status.filesize);
	vertex.data[file.status.filesize] = 0;

	struct vert_shader_resource shader = {.name = str8_dup(&RC.arena, name), .vertex = vertex};

	array_push(&RC.arena, RC.vert_shaders, shader);
	result = &RC.vert_shaders[array_len(RC.vert_shaders) - 1];

	fs_file_close(&file);

	return (result);
}

static struct shader_resource *
shader_resource_make_reference(struct shader_resource *shader)
{
	assert(shader);

	rc_increment(&shader->refs);

	return (shader);
}

struct shader_attribute
shader_resource_get_attribute(struct shader_resource *shader, str8 attribute)
{
	struct shader_attribute result = {.location = -1};

	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		if (str8_eq(shader->attributes[i].name, attribute))
		{
			assert(shader->attributes[i].location != -1);
			result = shader->attributes[i];
			break;
		}
	}

	return (result);
}

struct shader_uniform
shader_resource_get_uniform(struct shader_resource *shader, str8 attribute)

{
	struct shader_uniform result = {.location = -1};

	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		if (str8_eq(shader->uniforms[i].name, attribute))
		{
			assert(shader->uniforms[i].location != -1);
			result = shader->uniforms[i];
			break;
		}
	}

	return (result);
}

struct shader_sampler
shader_resource_get_sampler(struct shader_resource *shader, str8 sampler)

{
	struct shader_sampler result = {.location = -1};

	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		if (str8_eq(shader->samplers[i].name, sampler))
		{
			assert(shader->samplers[i].location != -1);
			result = shader->samplers[i];
			break;
		}
	}

	return (result);
}

i32
shader_resource_get_attribute_loc(struct shader_resource *shader, str8 attribute, b8 assert)
{
	i32 result = -1;

	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		if (str8_eq(shader->attributes[i].name, attribute))
		{
			result = shader->attributes[i].location;
			break;
		}
	}

	if (assert) { assert(result != -1); }

	return (result);
}

i32
shader_resource_get_uniform_loc(struct shader_resource *shader, str8 uniform, b8 assert)
{
	i32 result = -1;

	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		if (str8_eq(shader->uniforms[i].name, uniform))
		{
			result = shader->uniforms[i].location;
			break;
		}
	}

	if (assert) { assert(result != -1); }

	return (result);
}

i32
shader_resource_get_sampler_loc(struct shader_resource *shader, str8 sampler, b8 assert)
{
	i32 result = -1;

	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		if (str8_eq(shader->samplers[i].name, sampler))
		{
			result = shader->samplers[i].location;
			break;
		}
	}

	if (assert) { assert(result != -1); }

	return (result);
}

struct shader_resource *
load_shader(str8 name, str8 vertex, str8 fragment)
{
	struct shader_resource *result = 0;
	for (u32 i = 0; i < array_len(RC.shaders); ++i)
	{
		if (str8_eq(RC.shaders[i].name, name))
		{
			result = &RC.shaders[i];
			break;
		}
	}

	if (result)
	{
		assert(str8_eq(result->vertex->name, vertex));
		assert(str8_eq(result->fragment->name, fragment));

		return shader_resource_make_reference(result);
	}

	struct shader_resource shader = {0};
	for (u32 i = 0; i < array_len(RC.vert_shaders); ++i)
	{
		if (str8_eq(RC.vert_shaders[i].name, vertex))
		{
			shader.vertex = &RC.vert_shaders[i];
			rc_increment(&shader.vertex->refs);
			break;
		}
	}

	for (u32 i = 0; i < array_len(RC.frag_shaders); ++i)
	{
		if (str8_eq(RC.frag_shaders[i].name, fragment))
		{
			shader.fragment = &RC.frag_shaders[i];
			rc_increment(&shader.fragment->refs);
			break;
		}
	}

	if (shader.vertex == 0)
	{
		shader.vertex = load_vert_shader(vertex);
		rc_increment(&shader.vertex->refs);
	}
	if (shader.fragment == 0)
	{
		shader.fragment = load_frag_shader(fragment);
		rc_increment(&shader.fragment->refs);
	}
	// for (u32 i = 0; i < ARRAY_SIZE(shader.attributes); ++i) { shader.attributes[i].location = -1; }
	// for (u32 i = 0; i < ARRAY_SIZE(shader.uniforms); ++i) { shader.uniforms[i].location = -1; }

	shader.name = str8_dup(&RC.arena, name);

	array_push(&RC.arena, RC.shaders, shader);
	result = &RC.shaders[array_len(RC.shaders) - 1];

	return shader_resource_make_reference(result);
}

struct resource *
resource_get_default_material(void)
{
	return (&RC.defaults.material_resource);
}

struct resource *
resource_get_default_image(void)
{
	return (&RC.defaults.image_resource);
}

b8
sm___resource_mock_init(i8 *argv[], str8 assets_folder)
{
	struct buf m_resource = base_memory_reserve(MB(30));
	arena_make(&RC.arena, m_resource);
	arena_validate(&RC.arena);

	array_set_cap(&RC.arena, RC.resources, 64);
	array_set_cap(&RC.arena, RC.images, 64);
	array_set_cap(&RC.arena, RC.meshes, 64);
	array_set_cap(&RC.arena, RC.clips, 64);
	array_set_cap(&RC.arena, RC.materials, 64);
	array_set_cap(&RC.arena, RC.armatures, 16);
	array_set_cap(&RC.arena, RC.scenes, 16);
	array_set_cap(&RC.arena, RC.shaders, 16);
	array_set_cap(&RC.arena, RC.frag_shaders, 32);
	array_set_cap(&RC.arena, RC.vert_shaders, 32);

	memset(RC.resources, 0x0, sizeof(struct resource) * 64);
	memset(RC.images, 0x0, sizeof(struct image_resource) * 64);
	memset(RC.meshes, 0x0, sizeof(struct mesh_resource) * 64);
	memset(RC.clips, 0x0, sizeof(struct clip_resource) * 64);
	memset(RC.materials, 0x0, sizeof(struct material_resource) * 64);
	memset(RC.armatures, 0x0, sizeof(struct armature_resource) * 16);
	memset(RC.scenes, 0x0, sizeof(struct scene_resource) * 16);
	memset(RC.shaders, 0x0, sizeof(struct shader_resource) * 16);
	memset(RC.vert_shaders, 0x0, sizeof(struct vert_shader_resource) * 32);
	memset(RC.frag_shaders, 0x0, sizeof(struct frag_shader_resource) * 32);

	PHYSFS_Allocator physfs_alloca = {
	    .Malloc = (void *(*)(unsigned long long))sm__physfs_malloc,
	    .Realloc = (void *(*)(void *, unsigned long long))sm__physfs_realloc,
	    .Free = sm__physfs_free,
	};
	PHYSFS_setAllocator(&physfs_alloca);

	if (!PHYSFS_init(argv[0]))
	{
		sm__physfs_log_last_error(str8_from("error while initializing."));
		return (false);
	}

	if (!PHYSFS_mount(assets_folder.idata, "/", 1))
	{
		sm__physfs_log_last_error(str8_from("error while mounting"));
		return (false);
	}

	// if (!PHYSFS_mount("assets/dump/dump.zip", "/", 1))
	// {
	// 	sm__physfs_log_last_error(str8_from("error while mounting"));
	// 	return (false);
	// }

	if (!PHYSFS_setWriteDir(assets_folder.idata))
	{
		sm__physfs_log_last_error(str8_from("error while setting write dir"));
		return (false);
	}

	RC.map = str8_resource_map_make(&RC.arena);

	sm__resource_manager_load_defaults();

	return (true);
}

void
sm___resource_mock_teardown(void)
{
	PHYSFS_deinit();

	arena_release(&RC.arena);
	RC = (struct resource_manager){0};
}

void
sm___resource_mock_read(void)
{
	i8 **rc = PHYSFS_enumerateFiles("/dump");
	i8 **i;

	for (i = rc; *i != 0; i++)
	{
		struct str8_buf path_ctor = str_buf_begin(&RC.arena);
		str_buf_append(&RC.arena, &path_ctor, str8_from("dump/"));
		str8 file_name = (str8){.idata = *i, .size = (u32)strlen(*i)};
		str_buf_append(&RC.arena, &path_ctor, file_name);
		str8 file_path = str_buf_end(&RC.arena, path_ctor);

		PHYSFS_Stat stat = {0};
		PHYSFS_stat(file_path.idata, &stat);

		if (stat.filetype == PHYSFS_FILETYPE_REGULAR)
		{
			// TODO: impose an order: IMAGES -> MATERIALS -> MESHES -> SCENES
			struct resource *resource = sm__resource_prefetch(file_path);
			if (resource == 0) { log_warn(str8_from("[{s}] ignoring file"), file_path); }
			else { sm__resource_read(file_path, resource); }
		}
		else { log_warn(str8_from("[{s}] it is not a regular file. Skipping"), file_path); }

		arena_free(&RC.arena, file_path.data);
	}

	PHYSFS_freeList(rc);
}

struct image_resource *
sm___resource_mock_push_image(struct image_resource *image)
{
	struct image_resource *result = 0;

	array_push(&RC.arena, RC.images, *image);

	result = array_last_item(RC.images);
	assert(result);

	return (result);
}

struct material_resource *
sm___resource_mock_push_material(struct material_resource *material)
{
	struct material_resource *result = 0;

	array_push(&RC.arena, RC.materials, *material);

	result = array_last_item(RC.materials);
	assert(result);

	return (result);
}

struct mesh_resource *
sm___resource_mock_push_mesh(struct mesh_resource *mesh)
{
	struct mesh_resource *result = 0;

	array_push(&RC.arena, RC.meshes, *mesh);

	result = array_last_item(RC.meshes);
	assert(result);

	return (result);
}

struct scene_resource *
sm___resource_mock_push_scene(struct scene_resource *scene)
{
	struct scene_resource *result = 0;

	array_push(&RC.arena, RC.scenes, *scene);

	result = array_last_item(RC.scenes);
	assert(result);

	return (result);
}

struct armature_resource *
sm___resource_mock_push_armature(struct armature_resource *armature)
{
	struct armature_resource *result = 0;

	array_push(&RC.arena, RC.armatures, *armature);

	result = array_last_item(RC.armatures);
	assert(result);

	return (result);
}

struct clip_resource *
sm___resource_mock_push_clip(struct clip_resource *clip)
{
	struct clip_resource *result = 0;

	array_push(&RC.arena, RC.clips, *clip);

	result = array_last_item(RC.clips);
	assert(result);

	return (result);
}

#if 0
static struct transform sm__gltf_get_local_transform(const cgltf_node *node);

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

static i32
sm__gltf_get_material_index(cgltf_material *target, cgltf_material *all_materials, u32 num_materials)
{
	if (target == 0) { return (-1); }
	assert(all_materials);

	for (u32 i = 0; i < num_materials; ++i)
	{
		if (target == &all_materials[i]) { return (i32)i; }
	}

	return (-1);
}

u8 *stbi_load(const i8 *filename, int *x, int *y, int *comp, int req_comp);
void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

static void
sm__gltf_load_images(cgltf_data *data)
{
	log_trace(str8_from("* loading images"));
	for (u32 i = 0; i < data->images_count; ++i)
	{
		cgltf_image *gltf_image = &data->images[i];
		struct image_resource image = {0};

		image.name = str8_from_cstr(&RC.arena, gltf_image->name);

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

		log_trace(str8_from("    {u3d} [{s}] loading image"), i, image.name);
		log_trace(str8_from("        - {u3d}x{u3d}:{u3d}"), image.width, image.height, channels);

		array_push(&RC.arena, RC.images, image);
	}
}

static void
sm__gltf_load_materials(cgltf_data *data)
{
	log_trace(str8_from("* loading materials"));
	for (u32 i = 0; i < data->materials_count; ++i)
	{
		const cgltf_material *gltf_material = &data->materials[i];
		struct material_resource material = {0};
		material.color = v4_one();

		material.name = str8_from_cstr(&RC.arena, gltf_material->name);
		u32 hash = str8_hash(material.name);
		material.id = i + hash;

		if (gltf_material->has_pbr_metallic_roughness)
		{
			const cgltf_float *base_color = gltf_material->pbr_metallic_roughness.base_color_factor;
			v4 c = v4_new(base_color[0], base_color[1], base_color[2], base_color[3]);
			if (glm_vec4_eq_eps(c.data, 0.0f)) { c = v4_one(); }
			material.color = c;

			if (gltf_material->pbr_metallic_roughness.base_color_texture.texture)
			{
				for (u32 j = 0; j < array_len(RC.images); ++j)
				{
					i8 *image_name = gltf_material->pbr_metallic_roughness.base_color_texture
							     .texture->image->name;
					if (strncmp(image_name, RC.images[j].name.idata, RC.images[j].name.size) == 0)
					{
						material.image_ref = resource_image_make_reference(&RC.images[j]);
						break;
					}
				}

				if (!material.image_ref)
				{
					material.image_ref = resource_image_make_reference(res_image_get_default());
				}
			}
		}
		material.double_sided = gltf_material->double_sided;

		log_trace(str8_from("    {u3d} [{s}] loading material"), i, material.name);
		log_trace(str8_from("        - double sided: {b}"), material.double_sided);
		log_trace(
		    str8_from("        - color       : ({v4}) 0x{cx}"), material.color, color_from_v4(material.color));

		array_push(&RC.arena, RC.materials, material);
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
	array_set_len(&RC.arena, values, accessor->count * component_count);
	for (cgltf_size i = 0; i < accessor->count; ++i)
	{
		cgltf_accessor_read_float(accessor, i, &values[i * component_count], component_count);
	}

	cgltf_size accessor_count = accessor->count;

	for (u32 i = 0; i < accessor_count; ++i)
	{
		u32 idx = i * component_count;
		switch (attr_type)
		{
		case cgltf_attribute_type_position:
			{
				v3 position = v3_new(values[idx + 0], values[idx + 1], values[idx + 2]);
				array_push(&RC.arena, mesh->positions, position);
			}
			break;
		case cgltf_attribute_type_texcoord:
			{
				v2 tex_coord = v2_new(values[idx + 0], (1.0f - values[idx + 1]));
				array_push(&RC.arena, mesh->uvs, tex_coord);
			}
			break;
		case cgltf_attribute_type_normal:
			{
				v3 normal = v3_new(values[idx + 0], values[idx + 1], values[idx + 2]);
				if (glm_vec3_eq_eps(normal.data, 0.0f)) { normal = v3_up(); }
				glm_vec3_normalize(normal.data);
				array_push(&RC.arena, mesh->normals, normal);
			}
			break;
		case cgltf_attribute_type_color:
			{
				v4 c = v4_new(values[idx + 0], values[idx + 1], values[idx + 2], values[idx + 3]);
				array_push(&RC.arena, mesh->colors, c);
			}
			break;
		case cgltf_attribute_type_weights:
			{
				assert(skin);
				v4 weight = v4_new(values[idx + 0], values[idx + 1], values[idx + 2], values[idx + 3]);
				array_push(&RC.arena, mesh->skin_data.weights, weight);
			}
			break;
		case cgltf_attribute_type_joints:
			{
				assert(skin);
				iv4 joints;

				joints.data[0] = (i32)(values[idx + 0] + 0.5f);
				joints.data[1] = (i32)(values[idx + 1] + 0.5f);
				joints.data[2] = (i32)(values[idx + 2] + 0.5f);
				joints.data[3] = (i32)(values[idx + 3] + 0.5f);

				joints.data[0] =
				    sm__gltf_get_node_index(skin->joints[joints.data[0]], nodes, node_count);
				joints.data[1] =
				    sm__gltf_get_node_index(skin->joints[joints.data[1]], nodes, node_count);
				joints.data[2] =
				    sm__gltf_get_node_index(skin->joints[joints.data[2]], nodes, node_count);
				joints.data[3] =
				    sm__gltf_get_node_index(skin->joints[joints.data[3]], nodes, node_count);

				joints.data[0] = glm_max(0, joints.data[0]);
				joints.data[1] = glm_max(0, joints.data[1]);
				joints.data[2] = glm_max(0, joints.data[2]);
				joints.data[3] = glm_max(0, joints.data[3]);

				array_push(&RC.arena, mesh->skin_data.influences, joints);
			}
			break;

		default: break;
		}
	}

	array_release(&RC.arena, values);
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

			mesh.name = str8_from_cstr(&RC.arena, gltf_mesh->name);

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
				array_set_len(&RC.arena, mesh.indices, ic);
				for (u32 k = 0; k < ic; ++k)
				{
					mesh.indices[k] = cgltf_accessor_read_index(primitive->indices, k);
				}
			}

			if (!mesh.colors)
			{
				u32 len = array_len(mesh.positions);
				array_set_len(&RC.arena, mesh.colors, len);
				for (u32 c = 0; c < len; ++c) mesh.colors[c] = v4_one();
			}

			log_trace(str8_from("    {u3d} [{s}] loading mesh"), i, mesh.name);
			log_trace(str8_from("        - vertices: {u3d}"), array_len(mesh.positions));
			log_trace(str8_from("        - indexed : {b}"), mesh.indices != 0);

			mesh.flags = MESH_FLAG_DIRTY;
			array_push(&RC.arena, RC.meshes, mesh);
		}
	}
}

static void
sm__gltf_load_scenes(cgltf_data *data)
{
	cgltf_node *gltf_nodes = data->nodes;
	u32 gltf_nodes_count = data->nodes_count;

	assert(data->scenes_count == 1);
	struct scene_resource scene = {0};
	scene.name = str8_from_cstr(&RC.arena, data->scenes[0].name);

	array_set_len(&RC.arena, scene.nodes, gltf_nodes_count);
	memset(scene.nodes, 0x0, sizeof(struct scene_node) * array_len(scene.nodes));

	assert(data->skins_count == 0);
	for (u32 i = 0; i < array_len(scene.nodes); ++i)
	{
		const cgltf_node *gltf_n = &gltf_nodes[i];

		struct scene_node *n = &scene.nodes[i];
		n->name = str8_from_cstr(&RC.arena, gltf_n->name);

		i32 parent_index = sm__gltf_get_node_index(gltf_n->parent, gltf_nodes, gltf_nodes_count);
		n->parent_index = parent_index;

		array_set_cap(&RC.arena, n->children, gltf_n->children_count);
		for (u32 c = 0; c < gltf_n->children_count; ++c)
		{
			i32 child_index = sm__gltf_get_node_index(gltf_n->children[c], gltf_nodes, gltf_nodes_count);

			array_push(&RC.arena, n->children, child_index);
		}

		struct transform transform = sm__gltf_get_local_transform(gltf_n);
		glm_vec4_copy(transform.position.data, n->position.data);
		glm_vec3_copy(transform.scale.data, n->scale.data);
		glm_vec4_copy(transform.rotation.data, n->rotation.data);

		assert(!gltf_n->skin);
		if (gltf_n->mesh)
		{
			u32 cstr_len = strlen(gltf_n->mesh->name);
			str8 mesh_name = (str8){.idata = gltf_n->mesh->name, .size = cstr_len};

			mesh_resource *mesh = resource_mesh_get_by_name(mesh_name);
			assert(mesh);

			n->mesh_ref = resource_mesh_make_reference(mesh);

			if (gltf_n->mesh->primitives->material)
			{
				cgltf_material *target = gltf_n->mesh->primitives->material;
				i32 material_id =
				    sm__gltf_get_material_index(target, data->materials, data->materials_count);
				u32 hash = material_id + strc_hash(target->name);

				for (u32 m = 0; m < array_len(RC.materials); ++m)
				{
					if (hash == RC.materials[m].id)
					{
						n->material_ref = resource_material_make_reference(&RC.materials[m]);
						break;
					}
				}
			}
			if (!n->material_ref)
			{
				n->material_ref = resource_material_make_reference(resource_material_get_default());
			}
		}
	}

	array_push(&RC.arena, RC.scenes, scene);
}

static void
sm__gltf_update_armature_inverse_bind_pose(struct armature_resource *armature)
{
	assert(armature != 0);

	u32 size = array_len(armature->bind.joints);
	array_set_len(&RC.arena, armature->inverse_bind, size);

	for (u32 i = 0; i < size; ++i)
	{
		struct transform world = pose_get_global_transform(&armature->bind, i);

		m4 inv = transform_to_m4(world);
		glm_mat4_inv(inv.data, inv.data);

		armature->inverse_bind[i] = inv;
	}
}

static void
sm__gltf_load_armatures(cgltf_data *data)
{
	assert(data->skins_count == 0 || data->skins_count == 1);
	if (!data->skins) return;

	log_trace(str8_from("* loading armatures"));

	cgltf_skin *gltf_skin = &data->skins[0];
	u32 num_bones = data->nodes_count;

	struct pose rest = {0};
	pose_resize(&RC.arena, &rest, num_bones);

	for (u32 i = 0; i < num_bones; ++i)
	{
		cgltf_node *n = &data->nodes[i];

		struct transform local_transform = sm__gltf_get_local_transform(n);
		i32 node_parent_index = sm__gltf_get_node_index(n->parent, data->nodes, data->nodes_count);

		rest.joints[i] = local_transform;
		rest.parents[i] = node_parent_index;
	}

	array(struct transform) world_bind_pose = 0;
	array_set_len(&RC.arena, world_bind_pose, num_bones);

	for (u32 i = 0; i < num_bones; ++i) { world_bind_pose[i] = pose_get_global_transform(&rest, i); }

	if (gltf_skin->inverse_bind_matrices->count != 0)
	{
		assert(gltf_skin->inverse_bind_matrices->count == gltf_skin->joints_count);
		array(m4) inv_bind_accessor = 0;
		array_set_len(&RC.arena, inv_bind_accessor, gltf_skin->inverse_bind_matrices->count);

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

			struct transform bind_transform = transform_from_m4(bind_matrix);

			// set that transform in the world_nind_pose
			cgltf_node *joint_node = gltf_skin->joints[i];
			i32 joint_index = sm__gltf_get_node_index(joint_node, data->nodes, num_bones);
			assert(joint_index != -1);
			world_bind_pose[joint_index] = bind_transform;
		}

		array_release(&RC.arena, inv_bind_accessor);
	}

	struct pose bind = {0};
	pose_resize(&RC.arena, &bind, num_bones);
	pose_copy(&RC.arena, &bind, &rest);

	for (u32 i = 0; i < num_bones; ++i)
	{
		struct transform current = world_bind_pose[i];
		i32 p = bind.parents[i];

		// bring into parent space
		if (p >= 0)
		{
			struct transform parent = world_bind_pose[p];
			current = transform_combine(transform_inverse(parent), current);
		}
		bind.joints[i] = current;
	}

	array_release(&RC.arena, world_bind_pose);

	str8 skin_name = str8_from_cstr(&RC.arena, gltf_skin->name);
	struct armature_resource armature = {.name = skin_name, .rest = rest, .bind = bind};

	sm__gltf_update_armature_inverse_bind_pose(&armature);

	array_push(&RC.arena, RC.armatures, armature);
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
	array_set_len(&RC.arena, time, sampler->input->count * 1);
	for (u32 i = 0; i < sampler->input->count; ++i) { cgltf_accessor_read_float(sampler->input, i, &time[i], 1); }

	f32 *val = 0;
	array_set_len(&RC.arena, val, sampler->output->count * stride);
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

	switch (result->track_type)
	{
	case TRACK_TYPE_SCALAR: array_set_len(&RC.arena, result->frames_scalar, num_frames); break;
	case TRACK_TYPE_V3: array_set_len(&RC.arena, result->frames_v3, num_frames); break;
	case TRACK_TYPE_V4: array_set_len(&RC.arena, result->frames_v4, num_frames); break;
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
			}
			break;
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
			}
			break;
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
			}
			break;
		default: sm__unreachable();
		}
	}

	array_release(&RC.arena, time);
	array_release(&RC.arena, val);
}

static void
sm__gltf_load_anim_clips(cgltf_data *data)
{
	u32 num_clips = data->animations_count;
	u32 num_nodes = data->nodes_count;

	struct clip_resource *clips = 0;
	array_set_len(&RC.arena, clips, num_clips);
	memset(clips, 0x0, num_clips * sizeof(struct clip_resource));

	for (u32 i = 0; i < num_clips; ++i)
	{
		u32 num_channels = data->animations[i].channels_count;

		struct clip_resource *clip = &clips[i];
		clip->looping = true;

		clip->name = str8_from_cstr(&RC.arena, data->animations[i].name);

		for (u32 j = 0; j < num_channels; ++j)
		{
			cgltf_animation_channel *channel = &data->animations[i].channels[j];
			cgltf_node *target = channel->target_node;
			i32 node_id = sm__gltf_get_node_index(target, data->nodes, num_nodes);

			if (channel->target_path == cgltf_animation_path_type_translation)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(&RC.arena, clip, node_id);
				sm__gltf_load_track_from_channel(&track->position, 3, channel);
			}
			else if (channel->target_path == cgltf_animation_path_type_scale)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(&RC.arena, clip, node_id);
				sm__gltf_load_track_from_channel(&track->scale, 3, channel);
			}
			else if (channel->target_path == cgltf_animation_path_type_rotation)
			{
				struct transform_track *track =
				    clip_get_transform_track_from_joint(&RC.arena, clip, node_id);
				sm__gltf_load_track_from_channel(&track->rotation, 4, channel);
			}
		}

		clip_recalculate_duration(clip);
		resource_anim_clip_print(clip);
	}

	for (u32 i = 0; i < array_len(clips); ++i)
	{
		for (u32 j = 0; j < array_len(clips[i].tracks); ++j)
		{
			u32 joint = clips[i].tracks[j].id;

			struct transform_track *ttrack =
			    clip_get_transform_track_from_joint(&RC.arena, &clips[i], joint);

			track_index_look_up_table(&RC.arena, &ttrack->position);
			track_index_look_up_table(&RC.arena, &ttrack->rotation);
			track_index_look_up_table(&RC.arena, &ttrack->scale);
		}
	}

	for (u32 i = 0; i < array_len(clips); ++i) { array_push(&RC.arena, RC.clips, clips[i]); }

	array_release(&RC.arena, clips);
}

static struct transform
sm__gltf_get_local_transform(const cgltf_node *node)
{
	struct transform result = transform_identity();
	if (node->has_matrix)
	{
		m4 mat;
		memcpy(&mat, node->matrix, 16 * sizeof(f32));
		result = transform_from_m4(mat);
	}

	if (node->has_translation) { memcpy(&result.position, node->translation, 3 * sizeof(f32)); }
	if (node->has_rotation) { memcpy(&result.rotation, node->rotation, 4 * sizeof(f32)); }
	if (node->has_scale) { memcpy(&result.scale, node->scale, 3 * sizeof(f32)); }

	return (result);
}

static void *
sm__gltf_malloc(sm__maybe_unused void *user_data, u64 size)
{
	void *result;

	result = arena_reserve(&RC.arena, size);

	return (result);
}

static void
sm__gltf_free(sm__maybe_unused void *user_data, void *ptr)
{
	arena_free(&RC.arena, ptr);
}

static cgltf_result
sm__gltf_file_read(sm__maybe_unused const struct cgltf_memory_options *memory_options,
    sm__maybe_unused const struct cgltf_file_options *file_options, sm__maybe_unused const i8 *path,
    sm__maybe_unused cgltf_size *size, sm__maybe_unused void **data)
{
	struct resource_file file = resource_file_open_read_cstr(path);
	if (!file.ok) { return (cgltf_result_file_not_found); }

	cgltf_size file_size = size ? *size : 0;

	if (file_size == 0) { file_size = (cgltf_size)file.status.filesize; }

	i8 *file_data = (i8 *)sm__gltf_malloc(memory_options->user_data, file_size);
	if (!file_data)
	{
		resource_file_close(&file);
		return cgltf_result_out_of_memory;
	}
	u32 read_size = PHYSFS_readBytes(file.fsfile, file_data, file_size);

	resource_file_close(&file);

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

struct scene_resource *
load_animated_model_gltf(str8 filename)
{
	struct scene_resource *result = 0;

	cgltf_options options = {
	    .memory = {.alloc_func = sm__gltf_malloc, .free_func = sm__gltf_free	},
	    .file = {.read = sm__gltf_file_read,	 .release = sm__gltf_file_free},
	};

	cgltf_data *data = 0;
	cgltf_result gltf_result = cgltf_parse_file(&options, filename.idata, &data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not parse gltf file"), filename);
		return (result);
	}

	gltf_result = cgltf_load_buffers(&options, data, "exported/");
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not load buffers"), filename);

		cgltf_free(data);
		return (result);
	}

	gltf_result = cgltf_validate(data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] invalid gltf file"), filename);
		cgltf_free(data);
		return (result);
	}

	log_trace(str8_from("* [{s}] loading Animated model GLTF file"), filename);
	log_trace(str8_from("    - nodes count    : {u3d}"), data->nodes_count);
	log_trace(str8_from("    - meshes count   : {u3d}"), data->meshes_count);
	log_trace(str8_from("    - materials count: {u3d}"), data->materials_count);
	log_trace(str8_from("    - buffers count  : {u3d}"), data->buffers_count);
	log_trace(str8_from("    - images count   : {u3d}"), data->images_count);
	log_trace(str8_from("    - textures count : {u3d}"), data->textures_count);
	log_trace(str8_from("    - armatures      : {u3d}"), data->skins_count);
	for (u32 i = 0; i < data->skins_count; ++i)
	{
		log_trace(str8_from("        - {u3d} joints   : {u3d}"), i, data->skins[i].joints_count);
	}

	cgltf_node *gltf_nodes = data->nodes;
	u32 gltf_nodes_count = data->nodes_count;

	sm__gltf_load_meshes(data);
	sm__gltf_load_armatures(data);
	sm__gltf_load_anim_clips(data);

	assert(data->scenes_count == 1);

	struct child_parent_hierarchy
	{
		i32 node_index;
		b8 skip;
		cgltf_node *ptr_child, *ptr_parent;
	};

	array(struct child_parent_hierarchy) cph = 0;
	array_set_len(&RC.arena, cph, gltf_nodes_count);
	memset(cph, 0x0, sizeof(struct child_parent_hierarchy) * array_len(cph));

	assert(data->skins_count == 1);

	cgltf_skin *gltf_skin = &data->skins[0];
	for (u32 j = 0; j < gltf_skin->joints_count; ++j)
	{
		cgltf_node *n = gltf_skin->joints[j];

		i32 node_index = sm__gltf_get_node_index(n, gltf_nodes, gltf_nodes_count);
		assert(node_index != -1);
		cph[node_index].skip = true;
	}

	u32 scene_size = 0;
	for (u32 i = 0; i < array_len(cph); ++i)
	{
		cgltf_node *node = &gltf_nodes[i];
		struct child_parent_hierarchy *hie = &cph[i];

		if (hie->skip) { continue; }
		hie->skip = node->mesh == 0 && node->skin == 0;
		if (hie->skip) { continue; }

		i32 node_index = sm__gltf_get_node_index(node, data->nodes, data->nodes_count);

		hie->node_index = node_index;
		hie->ptr_child = node;
		hie->ptr_parent = node->parent;
		scene_size++;
	}

	assert(data->scenes_count == 1);
	struct scene_resource scene = {0};
	scene.name = str8_from_cstr(&RC.arena, data->scenes[0].name);

	array_set_len(&RC.arena, scene.nodes, scene_size);
	memset(scene.nodes, 0x0, sizeof(struct scene_node) * array_len(scene.nodes));

	u32 nodes_read = 0;
	for (u32 i = 0; i < array_len(cph); ++i)
	{
		struct child_parent_hierarchy *hie = &cph[i];
		if (hie->skip) { continue; }

		assert(nodes_read < array_len(scene.nodes));

		struct scene_node *n = &scene.nodes[nodes_read++];
		cgltf_node *gltf_n = hie->ptr_child;
		n->name = str8_from_cstr(&RC.arena, gltf_n->name);

		n->parent_index = sm__gltf_get_node_index(gltf_n->parent, gltf_nodes, gltf_nodes_count);
		if (n->parent_index != -1 && cph[n->parent_index].skip) { n->parent_index = -1; }

		array_set_cap(&RC.arena, n->children, gltf_n->children_count);
		// TODO: Fix bug prone
		for (u32 c = 0; c < gltf_n->children_count; ++c)
		{
			i32 child_index = sm__gltf_get_node_index(gltf_n->children[c], gltf_nodes, gltf_nodes_count);
			array_push(&RC.arena, n->children, child_index);
		}

		struct transform transform = sm__gltf_get_local_transform(gltf_n);
		glm_vec4_copy(transform.position.data, n->position.data);
		glm_vec3_copy(transform.scale.data, n->scale.data);
		glm_vec4_copy(transform.rotation.data, n->rotation.data);

		assert(gltf_n->skin && gltf_n->mesh);

		u32 cstr_len = strlen(gltf_n->mesh->name);
		str8 mesh_name = (str8){.idata = gltf_n->mesh->name, .size = cstr_len};

		mesh_resource *mesh = resource_mesh_get_by_name(mesh_name);
		assert(mesh);

		n->mesh_ref = resource_mesh_make_reference(mesh);

		if (gltf_n->mesh->primitives->material)
		{
			cgltf_material *target = gltf_n->mesh->primitives->material;
			i32 material_id = sm__gltf_get_material_index(target, data->materials, data->materials_count);
			u32 hash = material_id + strc_hash(target->name);

			for (u32 m = 0; m < array_len(RC.materials); ++m)
			{
				if (hash == RC.materials[m].id)
				{
					n->material_ref = resource_material_make_reference(&RC.materials[m]);
					break;
				}
			}
		}
		if (!n->material_ref)
		{
			n->material_ref = resource_material_make_reference(resource_material_get_default());
		}

		cstr_len = strlen(gltf_n->skin->name);
		str8 armature_name = (str8){.idata = gltf_n->skin->name, .size = cstr_len};
		armature_resource *armature = resource_armature_get_by_name(armature_name);
		assert(armature);
		n->armature_ref = resource_armature_make_reference(armature);
	}

	array_release(&RC.arena, cph);
	cgltf_free(data);

	array_push(&RC.arena, RC.scenes, scene);

	u32 last_scene = array_len(RC.scenes);
	assert(last_scene > 0);
	result = &RC.scenes[last_scene - 1];

	return (result);
}

struct scene_resource *
load_scene_gltf(str8 filename)
{
	struct scene_resource *result = 0;

	cgltf_options options = {
	    .memory = {.alloc_func = sm__gltf_malloc, .free_func = sm__gltf_free	},
	    .file = {.read = sm__gltf_file_read,	 .release = sm__gltf_file_free},
	};

	cgltf_data *data = 0;
	cgltf_result gltf_result = cgltf_parse_file(&options, filename.idata, &data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not parse gltf file"), filename);
		return (result);
	}

	gltf_result = cgltf_load_buffers(&options, data, "exported/");
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] could not load buffers"), filename);

		cgltf_free(data);
		return (result);
	}

	gltf_result = cgltf_validate(data);
	if (gltf_result != cgltf_result_success)
	{
		log_error(str8_from("[{s}] invalid gltf file"), filename);
		cgltf_free(data);
		return (result);
	}

	log_trace(str8_from("* [{s}] loading GLTF file"), filename);
	log_trace(str8_from("    - nodes count    : {u3d}"), data->nodes_count);
	log_trace(str8_from("    - meshes count   : {u3d}"), data->meshes_count);
	log_trace(str8_from("    - materials count: {u3d}"), data->materials_count);
	log_trace(str8_from("    - buffers count  : {u3d}"), data->buffers_count);
	log_trace(str8_from("    - images count   : {u3d}"), data->images_count);
	log_trace(str8_from("    - textures count : {u3d}"), data->textures_count);
	log_trace(str8_from("    - armatures      : {u3d}"), data->skins_count);
	for (u32 i = 0; i < data->skins_count; ++i)
	{
		log_trace(str8_from("        - {u3d} joints   : {u3d}"), i, data->skins[i].joints_count);
	}

	sm__gltf_load_images(data);
	sm__gltf_load_materials(data);
	sm__gltf_load_meshes(data);
	sm__gltf_load_scenes(data);
	cgltf_free(data);

	u32 last_scene = array_len(RC.scenes);
	assert(last_scene > 0);
	result = &RC.scenes[last_scene - 1];

	return (result);
}
#endif
