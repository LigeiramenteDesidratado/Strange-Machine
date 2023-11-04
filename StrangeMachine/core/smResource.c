#include "core/smResource.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "core/smRefCount.h"
#include "core/smString.h"

#include "animation/smAnimation.h"
#include "animation/smPose.h"

#include "ecs/smECS.h"
#include "ecs/smScene.h"

#include "math/smMath.h"

#include "vendor/physfs/src/physfs.h"

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
	usize resource_count;
	array(struct resource) resources;

	struct
	{
		struct resource image_resource;
		struct sm__resource_image image;

		struct resource material_resource;
		struct sm__resource_material material;
	} defaults;

	struct handle_pool mesh_pool;
	struct sm__resource_mesh *meshes;

	struct handle_pool image_pool;
	struct sm__resource_image *images;

	struct handle_pool material_pool;
	struct sm__resource_material *materials;

	struct handle_pool armature_pool;
	struct sm__resource_armature *armatures;

	struct handle_pool clip_pool;
	struct sm__resource_clip *clips;

	struct handle_pool text_pool;
	struct sm__resource_text *texts;

	struct handle_pool scene_pool;
	struct sm__resource_scene *scenes;
};

static struct resource_manager RC; // Resource Context

image_resource resource_image_alloc(void);
static b32 fs_image_write(struct fs_file *file, struct sm__resource_image *image);
static b32 fs_image_read(struct fs_file *file, struct sm__resource_image *image);
static void sm__resource_image_trace(struct sm__resource_image *image);
static str8 resource_image_pixel_format_str8(u32 pixelformat);

material_resource resource_material_alloc(void);
static b32 fs_material_write(struct fs_file *file, struct sm__resource_material *material);
static b32 fs_material_read(struct fs_file *file, struct sm__resource_material *material);
static void sm__resource_material_trace(struct sm__resource_material *material);

mesh_resource resource_mesh_alloc(void);
static b32 fs_mesh_write(struct fs_file *file, struct sm__resource_mesh *mesh);
static b32 fs_mesh_read(struct fs_file *file, struct sm__resource_mesh *mesh);
static void sm__resource_mesh_trace(struct sm__resource_mesh *mesh);
static void sm__resource_mesh_calculate_aabb(struct sm__resource_mesh *mesh);

scene_resource resource_scene_alloc(void);
static b32 fs_scene_write(struct fs_file *file, struct sm__resource_scene *scene);
static b32 fs_scene_read(struct fs_file *file, struct sm__resource_scene *scene);
static void sm__resource_scene_trace(struct sm__resource_scene *scene);

armature_resource resource_armature_alloc(void);
static b32 fs_armature_write(struct fs_file *file, struct sm__resource_armature *armature);
static b32 fs_armature_read(struct fs_file *file, struct sm__resource_armature *armature);
static void sm__resource_armature_trace(struct sm__resource_armature *armature);

clip_resource resource_clip_alloc(void);
static b32 fs_clip_write(struct fs_file *file, struct sm__resource_clip *clip);
static b32 fs_clip_read(struct fs_file *file, struct sm__resource_clip *clip);
static void sm__resource_clip_trace(struct sm__resource_clip *clip);
static f32 sm__resource_clip_get_duration(struct sm__resource_clip *clip);
static f32 sm__resourceclip_sample(struct sm__resource_clip *clip, struct pose *pose, f32 t);
static f32 sm__resource_clip_adjust_time(struct sm__resource_clip *clip, f32 t);
static f32 sm__resource_clip_get_start_time(struct sm__resource_clip *clip);
static f32 sm__resource_clip_get_end_time(struct sm__resource_clip *clip);

text_resource resource_text_alloc(void);
static b32 fs_text_write(struct fs_file *file, struct sm__resource_text *text);
static b32 fs_text_read(struct fs_file *file, struct sm__resource_text *text);
static void sm__resource_text_trace(struct sm__resource_text *text);

// clang-format off
typedef void (*resource_trace_fn)(void*);
static resource_trace_fn resource_tracer[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_trace_fn)sm__resource_image_trace,
    [RESOURCE_MATERIAL] = (resource_trace_fn)sm__resource_material_trace,
    [RESOURCE_MESH]     = (resource_trace_fn)sm__resource_mesh_trace,
    [RESOURCE_SCENE]    = (resource_trace_fn)sm__resource_scene_trace,
    [RESOURCE_ARMATURE] = (resource_trace_fn)sm__resource_armature_trace,
    [RESOURCE_CLIP]     = (resource_trace_fn)sm__resource_clip_trace,
    [RESOURCE_TEXT]     = (resource_trace_fn)sm__resource_text_trace,
};

static str8 resource_str8[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = str8_from("IMAGE"),
    [RESOURCE_MATERIAL] = str8_from("MATERIAL"),
    [RESOURCE_MESH]     = str8_from("MESH"),
    [RESOURCE_SCENE]    = str8_from("SCENE"),
    [RESOURCE_ARMATURE] = str8_from("ARMATURE"),
    [RESOURCE_CLIP]     = str8_from("CLIP"),
    [RESOURCE_TEXT]     = str8_from("TEXT"),
};

static str8 resource_state_str8[] = {
	[RESOURCE_STATE_INITIAL] = str8_from("RESOURCE_STATE_INITIAL"),
	[RESOURCE_STATE_ALLOC]   = str8_from("RESOURCE_STATE_ALLOC"),
	[RESOURCE_STATE_OK]      = str8_from("RESOURCE_STATE_OK"),
	[RESOURCE_STATE_INVALID] = str8_from("RESOURCE_STATE_INVALID"),
};

typedef b32 (*resource_writer_fn)(struct fs_file *, void *);
static resource_writer_fn resource_writer[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_writer_fn)fs_image_write,
    [RESOURCE_MATERIAL] = (resource_writer_fn)fs_material_write,
    [RESOURCE_MESH]     = (resource_writer_fn)fs_mesh_write,
    [RESOURCE_SCENE]    = (resource_writer_fn)fs_scene_write,
    [RESOURCE_ARMATURE] = (resource_writer_fn)fs_armature_write,
    [RESOURCE_CLIP]     = (resource_writer_fn)fs_clip_write,
    [RESOURCE_TEXT]     = (resource_writer_fn)fs_text_write,
};

typedef b32 (*resource_reader_fn)(struct fs_file *, void *);
static resource_reader_fn resource_reader[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_reader_fn)fs_image_read,
    [RESOURCE_MATERIAL] = (resource_reader_fn)fs_material_read,
    [RESOURCE_MESH]     = (resource_reader_fn)fs_mesh_read,
    [RESOURCE_SCENE]    = (resource_reader_fn)fs_scene_read,
    [RESOURCE_ARMATURE] = (resource_reader_fn)fs_armature_read,
    [RESOURCE_CLIP]     = (resource_reader_fn)fs_clip_read,
    [RESOURCE_TEXT]     = (resource_reader_fn)fs_text_read,
};

typedef struct resource_handle (*resource_maker_fn)(void*);
static resource_maker_fn resource_maker[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_maker_fn)resource_image_make,
    [RESOURCE_MATERIAL] = (resource_maker_fn)resource_material_make,
    [RESOURCE_MESH]     = (resource_maker_fn)resource_mesh_make,
    [RESOURCE_SCENE]    = (resource_maker_fn)resource_scene_make,
    [RESOURCE_ARMATURE] = (resource_maker_fn)resource_armature_make,
    [RESOURCE_CLIP]     = (resource_maker_fn)resource_clip_make,
    [RESOURCE_TEXT]     = (resource_maker_fn)resource_text_make,
};

typedef struct resource_handle (*resource_allocator_fn)(void);
static resource_allocator_fn resource_allocator[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_allocator_fn)resource_image_alloc,
    [RESOURCE_MATERIAL] = (resource_allocator_fn)resource_material_alloc,
    [RESOURCE_MESH]     = (resource_allocator_fn)resource_mesh_alloc,
    [RESOURCE_SCENE]    = (resource_allocator_fn)resource_scene_alloc,
    [RESOURCE_ARMATURE] = (resource_allocator_fn)resource_armature_alloc,
    [RESOURCE_CLIP]     = (resource_allocator_fn)resource_clip_alloc,
    [RESOURCE_TEXT]     = (resource_allocator_fn)resource_text_alloc,
};

typedef void *(*resource_at_fn)(struct resource_handle);
static resource_at_fn resource_at[RESOURCE_MAX] = {
    [RESOURCE_IMAGE]    = (resource_at_fn)resource_image_at,
    [RESOURCE_MATERIAL] = (resource_at_fn)resource_material_at,
    [RESOURCE_MESH]     = (resource_at_fn)resource_mesh_at,
    [RESOURCE_SCENE]    = (resource_at_fn)resource_scene_at,
    [RESOURCE_ARMATURE] = (resource_at_fn)resource_armature_at,
    [RESOURCE_CLIP]     = (resource_at_fn)resource_clip_at,
    [RESOURCE_TEXT]     = (resource_at_fn)resource_text_at,
};

// clang-format on

static void fs_write_b8(struct fs_file *file, b8 data);
static b8 fs_read_b8(struct fs_file *file);
static void fs_write_b8a(struct fs_file *file, array(b8) data);
static array(b8) fs_read_b8a(struct fs_file *file);

static void fs_write_b32(struct fs_file *file, b32 data);
static b32 fs_read_b32(struct fs_file *file);
static void fs_write_b32a(struct fs_file *file, array(b32) data);
static array(b32) fs_read_b32a(struct fs_file *file);

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
resource_trace(struct resource *resource)
{
	if (resource_tracer[resource->type])
	{
		log_trace(str8_from("============| {s} |============"), resource_str8[resource->type]);
		log_trace(str8_from(" * slot id    : {u3d}"), resource->slot.id);
		log_trace(str8_from(" * slot status: {s}"), resource_state_str8[resource->slot.state]);
		log_trace(str8_from(" * slot ref   : 0x{u6x}"), resource->slot.ref);
		log_trace(str8_from(" * name       : {s}"), resource->label);
		log_trace(str8_from(" * uri        : {s}"), resource->uri);
		log_trace(str8_from(" * refs       : {u3d}"), resource->refs.ref_count);

		if (resource->slot.state == RESOURCE_STATE_OK)
		{
			void *data = 0;
			if (resource_at[resource->type])
			{
				data = resource_at[resource->type]((struct resource_handle){resource->slot.id});
			}
			if (data)
			{
				resource_tracer[resource->type](data);
			}
			else
			{
				log_trace(str8_from(" * data: 0"));
			}
		}
		log_trace(str8_newline_const);
	}
	else
	{
		log_error(str8_from("[{s}] invalid resource type {s}"), resource->label, resource_str8[resource->type]);
	}
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
const union magic text_resource_magic = {.magic = "//SMTEXT"};
const union magic text2_resource_magic = {.magic = "// SMTEX"};

static b32
sm__resource_write_header(struct fs_file *file, struct resource *resource)
{
	union magic mgc = {0};

	if (resource->type == RESOURCE_IMAGE)
	{
		mgc = image_resource_magic;
	}
	else if (resource->type == RESOURCE_MATERIAL)
	{
		mgc = material_resource_magic;
	}
	else if (resource->type == RESOURCE_MESH)
	{
		mgc = mesh_resource_magic;
	}
	else if (resource->type == RESOURCE_SCENE)
	{
		mgc = scene_resource_magic;
	}
	else if (resource->type == RESOURCE_ARMATURE)
	{
		mgc = armature_resource_magic;
	}
	else if (resource->type == RESOURCE_CLIP)
	{
		mgc = clip_resource_magic;
	}

	if (mgc.x == 0)
	{
		log_error(str8_from("invalid magic {u3d}"), resource->type);
		return (0);
	}
	if (resource->label.size == 0)
	{
		log_error(str8_from("resource must have a name"));
		return (0);
	}

	fs_write_u64(file, mgc.x);
	fs_write_str8(file, resource->label);

	return (1);
}

static b32
sm__resource_read_header(struct fs_file *file, struct resource *resource)
{
	union magic mgc = {0};

	resource->type = 0;

	mgc.x = fs_read_u64(file);
	if (mgc.x == image_resource_magic.x)
	{
		resource->type = RESOURCE_IMAGE;
	}
	else if (mgc.x == material_resource_magic.x)
	{
		resource->type = RESOURCE_MATERIAL;
	}
	else if (mgc.x == mesh_resource_magic.x)
	{
		resource->type = RESOURCE_MESH;
	}
	else if (mgc.x == scene_resource_magic.x)
	{
		resource->type = RESOURCE_SCENE;
	}
	else if (mgc.x == armature_resource_magic.x)
	{
		resource->type = RESOURCE_ARMATURE;
	}
	else if (mgc.x == clip_resource_magic.x)
	{
		resource->type = RESOURCE_CLIP;
	}
	else if (mgc.x == text_resource_magic.x || mgc.x == text2_resource_magic.x)
	{
		resource->type = RESOURCE_TEXT;
	}

	if (resource->type == 0)
	{
		log_error(str8_from("invalid magic {u6d}"), mgc.x);
		return (0);
	}
	else if (resource->type != RESOURCE_TEXT)
	{
		resource->label = fs_read_str8(file);
	}

	return (1);
}

void
resource_write(struct resource *resource)
{
	struct str8_buf path = str_buf_begin(&RC.arena);
	str_buf_append(&RC.arena, &path, str8_from("dump/"));
	str_buf_append(&RC.arena, &path, resource->label);
	str8 dump_path = str_buf_end(&RC.arena, path);

	struct fs_file file = fs_file_open(dump_path, 0);
	if (!file.ok)
	{
		str8_release(&RC.arena, &dump_path);
		log_error(str8_from("[{s}] error while opening file"), resource->label);
		return;
	}
	if (!sm__resource_write_header(&file, resource))
	{
		log_error(str8_from("[{s}] error while writing resource header"), resource->label);
		goto cleanup;
	}

	if (resource_writer[resource->type])
	{
		void *data = 0;
		if (resource_at[resource->type])
		{
			data = resource_at[resource->type]((struct resource_handle){resource->slot.id});
		}
		if (data)
		{
			if (!resource_writer[resource->type](&file, data))
			{
				log_error(str8_from("[{s}] error while writting"), resource->label);
				goto cleanup;
			}
		}
		else
		{
			log_error(str8_from("[{s}] error while writting"), resource->label);
			goto cleanup;
		}
	}
	else
	{
		log_error(str8_from("[{s}] inalid resource type {u3d}"), resource->label, resource->type);
		goto cleanup;
	}

	log_trace(str8_from("[{s}] file written successfully"), resource->label);
cleanup:
	str8_release(&RC.arena, &dump_path);
	fs_file_close(&file);
}

static b32
sm__resource_step_over_header(struct fs_file *file)
{
	// back to begin of the file
	if (!PHYSFS_seek(file->fsfile, 0) || !PHYSFS_seek(file->fsfile, sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("seek failed"));
		return (0);
	}

	u32 len;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error getting name offset"));
		return (0);
	}

	// MAGIC NUMBER + STR LEN + STR
	len += sizeof(u32) + sizeof(u64);
	if (!PHYSFS_seek(file->fsfile, len))
	{
		sm__physfs_log_last_error(str8_from("seek failed"));
		return (0);
	}

	sm__assert(PHYSFS_tell(file->fsfile) == len);

	return (1);
}

static b32
sm__resource_read(struct resource *resource)
{
	struct fs_file file = fs_file_open(resource->uri, 1);
	if (!file.ok)
	{
		log_error(str8_from("[{s}] error while opening file"), resource->uri);
		return (0);
	}

	if (resource->type != RESOURCE_TEXT)
	{
		if (!sm__resource_step_over_header(&file))
		{
			log_error(str8_from("[{s}] error while stepping over the file header"), resource->uri);
			fs_file_close(&file);
			return (0);
		}
	}

	if (resource_reader[resource->type])
	{
		sm__assert(resource_allocator[resource->type] && resource_at[resource->type]);

		struct resource_handle handle = resource_allocator[resource->type]();
		void *data = resource_at[resource->type](handle);

		if (!resource_reader[resource->type](&file, data))
		{
			log_error(str8_from("[{s}] error while reading resource"), resource->label);
			fs_file_close(&file);
			return (0);
		}

		struct resource_slot *slot = (struct resource_slot *)data;

		slot->state = RESOURCE_STATE_OK;
		slot->id = handle.id;
		slot->ref = resource;

		resource->slot = *slot;
	}
	else
	{
		log_error(str8_from("[{s}] invalid resource type {u3d}"), resource->label, resource->type);
		fs_file_close(&file);
		return (0);
	}

	log_trace(str8_from("[{s}] resource file read succesfully"), resource->label);
	fs_file_close(&file);
	return (1);
}

static b32
sm__resource_prefetch(struct resource *resource)
{
	// validate resource
	sm__assert(resource->uri.size > 0);
	sm__assert(resource->slot.state == RESOURCE_STATE_INITIAL);

	// struct resource resource = {0};
	struct fs_file file = fs_file_open(resource->uri, 1);
	if (!file.ok)
	{
		log_error(str8_from("[{s}] error while opening file"), resource->uri);
		return (0);
	}
	if (!sm__resource_read_header(&file, resource))
	{
		log_error(str8_from("[{s}] error while prefetching resource header"), resource->uri);
		fs_file_close(&file);
		return (0);
	}
	fs_file_close(&file);

	if (resource->type == RESOURCE_TEXT)
	{
		resource->label = str8_dup(&RC.arena, resource->uri);
	}

	log_trace(str8_from("[{s}] resource header read succesfully"), resource->label);

	return (1);
}

// static void
// sm__resource_manager_load_defaults(void)
// {
// 	{
// 		u8 *data = arena_reserve(&RC.arena, sizeof(u8) * 4);
// 		memset(data, 0xFF, sizeof(u8) * 4);
//
// 		struct sm__resource_image default_image = {
// 		    .width = 1,
// 		    .height = 1,
// 		    .pixel_format = IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
// 		    .data = data,
// 		};
//
// 		RC.defaults.image = default_image;
// 		RC.defaults.image_resource =
// 		    resource_make(str8_from("StrangeMachineDefaultImage"), RESOURCE_IMAGE, );
// 	}
//
// 	{
// 		struct sm__resource_material default_material = {
// 		    .color = cWHITE,
// 		    .image = str8_from("StrangeMachineDefaultImage"),
// 		};
// 		RC.defaults.material = default_material;
// 		RC.defaults.material_resource =
// 		    resource_make(str8_from("StrangeMachineDefaultMaterial"), RESOURCE_MATERIAL, &RC.defaults.material);
// 	}
// }

#define RESOURCE_INITIAL_CAPACITY_RESOURCES 128
#define RESOURCE_INITIAL_CAPACITY_IMAGES    64
#define RESOURCE_INITIAL_CAPACITY_MESHES    64
#define RESOURCE_INITIAL_CAPACITY_CLIPS	    64
#define RESOURCE_INITIAL_CAPACITY_MATERIALS 64
#define RESOURCE_INITIAL_CAPACITY_ARMATUES  16
#define RESOURCE_INITIAL_CAPACITY_SCENES    16
#define RESOURCE_INITIAL_CAPACITY_TEXTS	    32

b32
resource_manager_init(i8 *argv[], str8 assets_folder)
{
	struct buf m_resource = base_memory_reserve(MB(15));

	arena_make(&RC.arena, m_resource);
	arena_validate(&RC.arena);

	// clang-format off
	handle_pool_make(&RC.arena, &RC.image_pool   , RESOURCE_INITIAL_CAPACITY_IMAGES);
	handle_pool_make(&RC.arena, &RC.mesh_pool    , RESOURCE_INITIAL_CAPACITY_MESHES);
	handle_pool_make(&RC.arena, &RC.clip_pool    , RESOURCE_INITIAL_CAPACITY_CLIPS);
	handle_pool_make(&RC.arena, &RC.material_pool, RESOURCE_INITIAL_CAPACITY_MATERIALS);
	handle_pool_make(&RC.arena, &RC.armature_pool, RESOURCE_INITIAL_CAPACITY_ARMATUES);
	handle_pool_make(&RC.arena, &RC.scene_pool   , RESOURCE_INITIAL_CAPACITY_SCENES);
	handle_pool_make(&RC.arena, &RC.text_pool    , RESOURCE_INITIAL_CAPACITY_TEXTS);

	RC.resource_count = 0;
	RC.resources = arena_reserve(&RC.arena, sizeof(struct resource)          * RESOURCE_INITIAL_CAPACITY_RESOURCES);
	RC.images    = arena_reserve(&RC.arena, sizeof(struct sm__resource_image)* RESOURCE_INITIAL_CAPACITY_IMAGES);
	RC.meshes    = arena_reserve(&RC.arena, sizeof(struct sm__resource_mesh)     * RESOURCE_INITIAL_CAPACITY_MESHES);
	RC.clips     = arena_reserve(&RC.arena, sizeof(struct sm__resource_clip)     * RESOURCE_INITIAL_CAPACITY_CLIPS);
	RC.materials = arena_reserve(&RC.arena, sizeof(struct sm__resource_material) * RESOURCE_INITIAL_CAPACITY_MATERIALS);
	RC.armatures = arena_reserve(&RC.arena, sizeof(struct sm__resource_armature) * RESOURCE_INITIAL_CAPACITY_ARMATUES);
	RC.scenes    = arena_reserve(&RC.arena, sizeof(struct sm__resource_scene)    * RESOURCE_INITIAL_CAPACITY_SCENES);
	RC.texts    = arena_reserve(&RC.arena, sizeof(struct sm__resource_text)    * RESOURCE_INITIAL_CAPACITY_TEXTS);

	memset(RC.resources, 0x0, sizeof(struct resource)          * RESOURCE_INITIAL_CAPACITY_RESOURCES);
	memset(RC.images   , 0x0, sizeof(struct sm__resource_image)* RESOURCE_INITIAL_CAPACITY_IMAGES);
	memset(RC.meshes   , 0x0, sizeof(struct sm__resource_mesh)     * RESOURCE_INITIAL_CAPACITY_MESHES);
	memset(RC.clips    , 0x0, sizeof(struct sm__resource_clip)     * RESOURCE_INITIAL_CAPACITY_CLIPS);
	memset(RC.materials, 0x0, sizeof(struct sm__resource_material) * RESOURCE_INITIAL_CAPACITY_MATERIALS);
	memset(RC.armatures, 0x0, sizeof(struct sm__resource_armature) * RESOURCE_INITIAL_CAPACITY_ARMATUES);
	memset(RC.scenes   , 0x0, sizeof(struct sm__resource_scene)    * RESOURCE_INITIAL_CAPACITY_SCENES);
	memset(RC.texts   , 0x0, sizeof(struct sm__resource_text)    * RESOURCE_INITIAL_CAPACITY_TEXTS);
	// clang-format on

	PHYSFS_Allocator a = {
	    .Malloc = (void *(*)(unsigned long long))sm__physfs_malloc,
	    .Realloc = (void *(*)(void *, unsigned long long))sm__physfs_realloc,
	    .Free = sm__physfs_free,
	};
	PHYSFS_setAllocator(&a);

	if (!PHYSFS_init(argv[0]))
	{
		sm__physfs_log_last_error(str8_from("error while initializing."));
		return (0);
	}

	if (!PHYSFS_mount(assets_folder.idata, "/", 1))
	{
		sm__physfs_log_last_error(str8_from("error while mounting"));
		return (0);
	}

	// if (!PHYSFS_mount("assets/exported/assets.zip", "/", 1))
	// {
	// 	sm__physfs_log_last_error(str8_from("error while mounting"));
	// 	return (0);
	// }

	if (!PHYSFS_setWriteDir(assets_folder.idata))
	{
		sm__physfs_log_last_error(str8_from("error while setting write dir"));
		return (0);
	}

	RC.map = str8_resource_map_make(&RC.arena);

	// sm__resource_manager_load_defaults();

	return (1);
}

void
resource_manager_teardown(void)
{
	PHYSFS_deinit();
}

struct resource
resource_make(str8 name, u32 type, struct resource_slot slot)
{
	struct resource result = {
	    .label = name,
	    .type = type,
	    .refs = {0},
	    .slot = slot,
	};

	return (result);
}

struct resource *
resource_ref_inc(struct resource *resource)
{
	sm__assert(resource);

	rc_increment(&resource->refs);

	return (resource);
}

void
resource_ref_dec(struct resource *resource)
{
	sm__assert(resource);

	rc_decrement(&resource->refs);
}

b32
resource_validate(struct resource *resource)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	sm__assertf(resource->label.size > 0, "resource.label not set");
	sm__assert(resource->type > RESOURCE_NONE && resource->type < RESOURCE_MAX);
	sm__assertf(resource->slot.state != RESOURCE_STATE_INVALID, "resource.slot.state is invalid");
	sm__assertf(resource->slot.ref == 0, "resource.slot.ref is set");
	if (resource->slot.state == RESOURCE_STATE_INITIAL)
	{
		sm__assertf(resource->uri.size > 0, "resource.uri not set");
		sm__assertf(resource->slot.id == INVALID_HANDLE,
		    "resource.slot.id is set but resource.slot.state is marked as RESOURCE_STATE_INITIAL");
	}
	else if (resource->slot.state == RESOURCE_STATE_OK)
	{
		sm__assertf(resource->slot.id != INVALID_HANDLE,
		    "resource.slot.id is not set but resource.slot.state is marked as RESOURCE_STATE_OK");
	}

	return (1);
#endif
}

struct resource *
resource_push(struct resource *resource)
{
	struct resource *result = 0;

	if (!resource_validate(resource))
	{
		return (result);
	}

	sm__assert(RC.resource_count < RESOURCE_INITIAL_CAPACITY_RESOURCES);

	RC.resources[RC.resource_count++] = *resource;
	result = &RC.resources[RC.resource_count - 1];
	result->slot.ref = result;

	struct str8_resource_result result_map = str8_resource_map_put(&RC.arena, &RC.map, result->label, result);
	if (result_map.ok)
	{
		log_error(str8_from("[{s}] duplicated resource!"), resource->label);
		sm__assert(0);
	}

	return (result);
}

struct resource *
resource_get_by_label(str8 name)
{
	struct resource *result = 0;

	struct str8_resource_result result_map = str8_resource_map_get(&RC.map, name);
	if (!result_map.ok)
	{
		log_warn(str8_from("[{s}] resource not found"), name);
		return (result);
	}

	result = result_map.value;

	if (result->slot.state == RESOURCE_STATE_INITIAL)
	{
		sm__resource_read(result);
	}
	sm__assert(result->slot.state == RESOURCE_STATE_OK);
	return (result);
}

void
resource_for_each(b32 (*cb)(str8, struct resource *, void *), void *user_data)
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
fs_file_open(str8 name, b32 read_only)
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

	result.ok = 1;

	return (result);
}

struct fs_file
fs_file_open_read_cstr(const i8 *name)
{
	str8 filename = (str8){.cidata = name, (u32)strlen(name)};
	return fs_file_open(filename, 1);
}

struct fs_file
fs_file_open_write_cstr(const i8 *name)
{
	str8 filename = (str8){.cidata = name, (u32)strlen(name)};
	return fs_file_open(filename, 0);
}

void
fs_file_close(struct fs_file *file)
{
	sm__assert(file->ok);
	sm__assert(file->fsfile);

	PHYSFS_close(file->fsfile);

	file->fsfile = 0;
	file->ok = 0;
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

static b32
sm__resource_image_validate(sm__maybe_unused const struct resource_image_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_image_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_image_desc not initialized");
	sm__assertf(desc->label.size > 0, "resource_image_desc.label not set");
	sm__assertf(desc->width > 0, "resource_image_desc.width must be > 0");
	sm__assertf(desc->height > 0, "resource_image_desc.height must be > 0");
	sm__assertf(desc->data != 0, "resource_image_desc.data: no data");
	sm__assertf(desc->pixel_format > IMAGE_PIXELFORMAT_NONE && desc->pixel_format < IMAGE_PIXELFORMAT_MAX,
	    "resource_image_desc.pixel_format: unkow pixel format");

	return (result);
#endif
}

image_resource
resource_image_make(const struct resource_image_desc *desc)
{
	image_resource result = resource_image_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_image *image_at = resource_image_at(result);
		image_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_image_validate(desc))
		{
			image_at->slot.id = result.id;
			image_at->slot.state = RESOURCE_STATE_OK;

			image_at->width = desc->width;
			image_at->height = desc->height;
			image_at->pixel_format = desc->pixel_format;
			image_at->data = desc->data;

			struct resource resource = resource_make(desc->label, RESOURCE_IMAGE, image_at->slot);
			image_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

image_resource
resource_image_get_by_label(str8 label)
{
	image_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_IMAGE);

	result.id = resource->slot.id;

	return (result);
}

image_resource
resource_image_alloc(void)
{
	image_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.image_pool);
	result.id = handle;

	return result;
}

struct sm__resource_image *
resource_image_at(image_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.images[slot_index];
}

static b32
fs_image_write(struct fs_file *file, struct sm__resource_image *image)
{
	fs_write_u32(file, image->width);
	fs_write_u32(file, image->height);
	fs_write_u32(file, image->pixel_format);

	u32 size = resource_image_size(image->width, image->height, image->pixel_format);

	i64 bw = PHYSFS_writeBytes(file->fsfile, image->data, size * sizeof(u8));
	if (bw != (i64)(size * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing image data"));
		sm__assert(0);
	}

	return (1);
}

static b32
fs_image_read(struct fs_file *file, struct sm__resource_image *image)
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
		return (0);
	}

	return (1);
}

static void
sm__resource_image_trace(struct sm__resource_image *image)
{
	log_trace(str8_from("        - dim            : {u3d}x{u3d}"), image->width, image->height);
	log_trace(str8_from("        - pixelformat    : {s}"), resource_image_pixel_format_str8(image->pixel_format));
}

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

static b32
sm__resource_material_validate(sm__maybe_unused const struct resource_material_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_material_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_material_desc not initialized");
	sm__assertf(desc->label.size > 0, "resource_material_desc.label no set");
	sm__assertf(desc->image.size > 0, "resource_material_desc.image not set");

	return (result);
#endif
}

material_resource
resource_material_make(const struct resource_material_desc *desc)
{
	material_resource result = resource_material_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_material *material_at = resource_material_at(result);
		material_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_material_validate(desc))
		{
			material_at->slot.id = result.id;
			material_at->slot.state = RESOURCE_STATE_OK;

			material_at->image = desc->image;
			material_at->color = desc->color;
			material_at->double_sided = desc->double_sided;

			struct resource resource = resource_make(desc->label, RESOURCE_MATERIAL, material_at->slot);
			material_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

material_resource
resource_material_get_by_label(str8 label)
{
	material_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_MATERIAL);

	result.id = resource->slot.id;

	return (result);
}

material_resource
resource_material_alloc(void)
{
	material_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.material_pool);
	result.id = handle;

	return result;
}

struct sm__resource_material *
resource_material_at(material_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.materials[slot_index];
}

static b32
fs_material_write(struct fs_file *file, struct sm__resource_material *material)
{
	fs_write_u32(file, material->color.hex);
	fs_write_b32(file, material->double_sided);
	fs_write_str8(file, material->image);

	return (1);
}

static b32
fs_material_read(struct fs_file *file, struct sm__resource_material *material)
{
	material->color.hex = fs_read_u32(file);
	material->double_sided = fs_read_b32(file);
	material->image = fs_read_str8(file);

	return (1);
}

static void
sm__resource_material_trace(struct sm__resource_material *material)
{
	log_trace(str8_from("        - double sided   : {b}"), material->double_sided);
	log_trace(str8_from("        - color          : ({cv}) 0x{cx}"), material->color, material->color);
	log_trace(str8_from("        - image          : {s}"), material->image);
}

static b32
sm__resource_mesh_validate(sm__maybe_unused const struct resource_mesh_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_mesh_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_mesh_desc not initialized");

	sm__assertf(desc->label.size > 0, "resource_mesh_desc.label no set");

	sm__assertf(desc->positions, "resource_mesh_desc.positions not provided");
	sm__assertf(array_len(desc->positions) > 0, "resource_mesh_desc.positions.size cannot be 0");

	sm__assertf(desc->uvs, "resource_mesh_desc.uvs not provided");
	sm__assertf(array_len(desc->uvs) > 0, "resource_mesh_desc.uvs.size cannot be 0");

	sm__assertf(desc->colors, "resource_mesh_desc.colors not provided");
	sm__assertf(array_len(desc->colors) > 0, "resource_mesh_desc.colors.size cannot be 0");

	sm__assertf(desc->normals, "resource_mesh_desc.normals not provided");
	sm__assertf(array_len(desc->normals) > 0, "resource_mesh_desc.normals.size cannot be 0");

	sm__assertf(desc->indices, "resource_mesh_desc.indices not provided");
	sm__assertf(array_len(desc->indices) > 0, "resource_mesh_desc.indices.size cannot be 0");

	if (desc->flags & MESH_FLAG_SKINNED)
	{
		sm__assertf(desc->skin_data.weights, "resource_mesh_desc.skin_data.weights not provided");
		sm__assertf(
		    array_len(desc->skin_data.weights) > 0, "resource_mesh_desc.skin_data.weights.size cannot be 0");

		sm__assertf(desc->skin_data.influences, "resource_mesh_desc.skin_data.influences not provided");
		sm__assertf(array_len(desc->skin_data.influences) > 0,
		    "resource_mesh_desc.skin_data.influences.size cannot be 0");
	}

	sm__assertf(glm_aabb_isvalid((f32(*)[3])desc->aabb.data), "resource_mesh_desc.aabb is not valid");

	return (result);
#endif
}

mesh_resource
resource_mesh_make(const struct resource_mesh_desc *desc)
{
	mesh_resource result = resource_mesh_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_mesh *mesh_at = resource_mesh_at(result);
		mesh_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_mesh_validate(desc))
		{
			mesh_at->slot.id = result.id;
			mesh_at->slot.state = RESOURCE_STATE_OK;

			mesh_at->positions = desc->positions;
			mesh_at->uvs = desc->uvs;
			mesh_at->colors = desc->colors;
			mesh_at->normals = desc->normals;
			mesh_at->indices = desc->indices;

			if (desc->flags & MESH_FLAG_SKINNED)
			{
				mesh_at->skin_data.weights = desc->skin_data.weights;
				mesh_at->skin_data.influences = desc->skin_data.influences;
			}

			mesh_at->aabb = desc->aabb;
			mesh_at->flags = desc->flags;

			struct resource resource = resource_make(desc->label, RESOURCE_MESH, mesh_at->slot);
			mesh_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

mesh_resource
resource_mesh_get_by_label(str8 label)
{
	mesh_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_MESH);

	result.id = resource->slot.id;

	return (result);
}

mesh_resource
resource_mesh_alloc(void)
{
	mesh_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.mesh_pool);
	result.id = handle;

	return result;
}

struct sm__resource_mesh *
resource_mesh_at(mesh_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.meshes[slot_index];
}

static b32
fs_mesh_write(struct fs_file *file, struct sm__resource_mesh *mesh)
{
	fs_write_v3a(file, mesh->positions);
	fs_write_v2a(file, mesh->uvs);
	fs_write_v4a(file, mesh->colors);
	fs_write_v3a(file, mesh->normals);
	fs_write_u32a(file, mesh->indices);

	fs_write_u32(file, mesh->flags);
	if (mesh->flags & MESH_FLAG_SKINNED)
	{
		fs_write_v4a(file, mesh->skin_data.weights);
		fs_write_iv4a(file, mesh->skin_data.influences);
	}

	return (1);
}

static b32
fs_mesh_read(struct fs_file *file, struct sm__resource_mesh *mesh)
{
	mesh->positions = fs_read_v3a(file);
	mesh->uvs = fs_read_v2a(file);
	mesh->colors = fs_read_v4a(file);
	mesh->normals = fs_read_v3a(file);
	mesh->indices = fs_read_u32a(file);

	mesh->flags = fs_read_u32(file);

	if (mesh->flags & MESH_FLAG_SKINNED)
	{
		mesh->skin_data.weights = fs_read_v4a(file);
		mesh->skin_data.influences = fs_read_iv4a(file);
	}

	return (1);
}

static str8
sm__resource_mesh_flag_str8(enum mesh_flags flag)
{
	switch (flag)
	{
	case MESH_FLAG_NONE: return str8_from("MESH_FLAG_NONE");
	case MESH_FLAG_DIRTY: return str8_from("MESH_FLAG_DIRTY");
	case MESH_FLAG_RENDERABLE: return str8_from("MESH_FLAG_RENDERABLE");
	case MESH_FLAG_SKINNED: return str8_from("MESH_FLAG_SKINNED");
	case MESH_FLAG_DRAW_AABB: return str8_from("MESH_FLAG_DRAW_AABB");
	case MESH_FLAG_BLEND: return str8_from("MESH_FLAG_BLEND");
	case MESH_FLAG_DOUBLE_SIDED: return str8_from("MESH_FLAG_DOUBLE_SIDED");
	default: return str8_from("UNKOWN MESH FLAG");
	}
}

static void
sm__resource_mesh_trace(struct sm__resource_mesh *mesh)
{
	log_trace(str8_from("        - vertices: {u3d}"), array_len(mesh->positions));
	log_trace(str8_from("        - indexed : {b}"), mesh->indices != 0);

	char buf[256];
	char *b = buf;
	b32 first = 1;
	for (u64 i = 1; (i - 1) < UINT64_MAX; i <<= 1)
	{
		enum mesh_flags flag = mesh->flags & i;
		if (flag != MESH_FLAG_NONE)
		{
			str8 sflag = sm__resource_mesh_flag_str8(flag);
			if (!first)
			{
				memcpy(b, "|", 1);
				b++;
			}
			sm__assert(((ptrdiff_t)b + sflag.size + 1) - (ptrdiff_t)buf < 256);
			memcpy(b, sflag.idata, sflag.size);
			b += sflag.size;
			first = 0;
		}
	}
	*b = 0;
	str8 flags = str8_from_cstr_stack(buf);
	log_trace(str8_from("        - flags   : {s}"), flags);
}

static void
sm__resource_mesh_calculate_aabb(struct sm__resource_mesh *mesh)
{
	// get min and max vertex to construct bounds (struct AABB)
	v3 min_vert;
	v3 max_vert;

	if (mesh->positions != 0)
	{
		glm_vec3_copy(mesh->positions[0].data, min_vert.data);
		glm_vec3_copy(mesh->positions[0].data, max_vert.data);

		for (u32 i = 1; i < array_len(mesh->positions); ++i)
		{
			glm_vec3_minv(min_vert.data, mesh->positions[i].data, min_vert.data);
			glm_vec3_maxv(max_vert.data, mesh->positions[i].data, max_vert.data);
		}
	}

	// create the bounding box
	struct aabb axis_aligned_bb;
	glm_vec3_copy(min_vert.data, axis_aligned_bb.min.data);
	glm_vec3_copy(max_vert.data, axis_aligned_bb.max.data);

	mesh->aabb = axis_aligned_bb;
}

void
resource_mesh_calculate_aabb(mesh_resource handle)
{
	sm__assert(handle.id != INVALID_HANDLE);

	struct sm__resource_mesh *mesh = resource_mesh_at(handle);
	sm__resource_mesh_calculate_aabb(mesh);
}

static b32
sm__resource_scene_validate(sm__maybe_unused const struct resource_scene_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_scene_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_scene_desc not initialized");

	sm__assertf(desc->label.size > 0, "resource_scene_desc.label no set");

	sm__assertf(desc->nodes, "resource_scene_desc.nodes not provided");
	sm__assertf(array_len(desc->nodes) > 0, "resource_scene_desc.inodes.size cannot be 0");

	for (u32 i = 0; i < array_len(desc->nodes); ++i)
	{
		struct sm__resource_scene_node *node = &desc->nodes[i];
		sm__assertf(glm_vec3_isvalid(node->position.data), "resource_scene_desc.node.position is not valid");
		sm__assertf(glm_vec4_isvalid(node->rotation.data), "resource_scene_desc.node.rotation is not valid");
		sm__assertf(glm_vec3_isvalid(node->scale.data), "resource_scene_desc.node.scale is not valid");

		if (node->prop != NODE_PROP_NONE)
		{
			sm__assertf(node->prop & (NODE_PROP_STATIC_BODY | NODE_PROP_RIGID_BODY | NODE_PROP_PLAYER),
			    "resource_scene_desc.node.prop has no valid property");
		}
	}

	return (result);
#endif
}

scene_resource
resource_scene_make(const struct resource_scene_desc *desc)
{
	scene_resource result = resource_scene_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_scene *scene_at = resource_scene_at(result);
		scene_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_scene_validate(desc))
		{
			scene_at->slot.id = result.id;
			scene_at->slot.state = RESOURCE_STATE_OK;

			scene_at->nodes = desc->nodes;

			struct resource resource = resource_make(desc->label, RESOURCE_SCENE, scene_at->slot);
			scene_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

scene_resource
resource_scene_get_by_label(str8 label)
{
	scene_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_SCENE);

	result.id = resource->slot.id;

	return (result);
}

scene_resource
resource_scene_alloc(void)
{
	scene_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.scene_pool);
	result.id = handle;

	return result;
}

struct sm__resource_scene *
resource_scene_at(scene_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.scenes[slot_index];
}

static b32
fs_scene_write(struct fs_file *file, struct sm__resource_scene *scene)
{
	u32 len = array_len(scene->nodes);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		struct sm__resource_scene_node *node = &scene->nodes[i];
		fs_write_str8(file, node->name);

		fs_write_i32(file, node->parent_index);
		fs_write_i32a(file, node->children);
		fs_write_v3(file, node->position);
		fs_write_v3(file, node->scale);
		fs_write_v4(file, node->rotation);

		fs_write_u32(file, node->prop);

		str8 empty = str8_from("\0");
		if (node->mesh.size > 0)
		{
			fs_write_str8(file, node->mesh);
		}
		else
		{
			fs_write_str8(file, empty);
		}

		if (node->material.size > 0)
		{
			fs_write_str8(file, node->material);
		}
		else
		{
			fs_write_str8(file, empty);
		}

		if (node->armature.size > 0)
		{
			fs_write_str8(file, node->armature);
		}
		else
		{
			fs_write_str8(file, empty);
		}
	}

	return (1);
}

static b32
fs_scene_read(struct fs_file *file, struct sm__resource_scene *scene)
{
	u32 len = fs_read_u32(file);
	array_set_len(&RC.arena, scene->nodes, len);
	for (u32 i = 0; i < len; ++i)
	{
		struct sm__resource_scene_node *node = &scene->nodes[i];
		node->name = fs_read_str8(file);

		node->parent_index = fs_read_i32(file);
		node->children = fs_read_i32a(file);
		node->position = fs_read_v3(file);
		node->scale = fs_read_v3(file);
		node->rotation = fs_read_v4(file);

		node->prop = fs_read_u32(file);

		str8 name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0")))
		{
			node->mesh = str8_empty_const;
		}
		else
		{
			node->mesh = name;
		}

		name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0")))
		{
			node->material = str8_empty_const;
		}
		else
		{
			node->material = name;
		}

		name = fs_read_str8(file);
		if (str8_eq(name, str8_from("\0")))
		{
			node->armature = str8_empty_const;
		}
		else
		{
			node->armature = name;
		}
	}

	return (1);
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
sm__resource_scene_trace(struct sm__resource_scene *scene)
{
	log_trace(str8_from("        - nodes   : {u3d}"), array_len(scene->nodes));

	char buf[256];
	for (u32 i = 0; i < array_len(scene->nodes); ++i)
	{
		log_trace(str8_from(" [{u3d}]    - name    : {s}"), i, scene->nodes[i].name);
		log_trace(str8_from("        - position: {v3}"), scene->nodes[i].position);
		log_trace(str8_from("        - rotation: {v4}"), scene->nodes[i].rotation);
		log_trace(str8_from("        - scale   : {v3}"), scene->nodes[i].scale);
		log_trace(str8_from("        - mesh    : {s}"), scene->nodes[i].mesh);
		log_trace(str8_from("        - material: {s}"), scene->nodes[i].material);
		log_trace(str8_from("        - armature: {s}"), scene->nodes[i].armature);

		b32 first = 1;
		char *b = buf;
		for (u64 j = 1; (j - 1) < UINT64_MAX; j <<= 1)
		{
			enum node_prop flag = scene->nodes[i].prop & j;
			if (flag != NODE_PROP_NONE)
			{
				str8 sflag = resource_scene_node_prop_str8(flag);
				if (!first)
				{
					memcpy(b, "|", 1);
					b++;
				}
				sm__assert(((ptrdiff_t)b + sflag.size + 1) - (ptrdiff_t)buf < 256);
				memcpy(b, sflag.idata, sflag.size);
				b += sflag.size;
				first = 0;
			}
		}
		*b = 0;
		str8 flags = str8_from_cstr_stack(buf);
		log_trace(str8_from("        - prop    : {s}"), flags);
		log_trace(str8_from(""));
	}
}

static b32
sm__resource_armature_validate(sm__maybe_unused const struct resource_armature_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_armature_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_armature_desc not initialized");

	sm__assertf(desc->label.size > 0, "resource_armature_desc.label no set");

	sm__assertf(desc->bind.parents, "resource_armature_desc.bind.parents not initialized");
	sm__assertf(desc->bind.joints, "resource_armature_desc.bind.joints not initialized");

	sm__assertf(array_len(desc->bind.parents) > 0, "resource_armature_desc.bind.parents.size cannot be 0");
	sm__assertf(array_len(desc->bind.joints) > 0, "resource_armature_desc.bind.joints.size cannot be 0");

	sm__assertf(desc->rest.parents, "resource_armature_desc.rest.parents not initialized");
	sm__assertf(desc->rest.joints, "resource_armature_desc.rest.joints not initialized");

	sm__assertf(array_len(desc->rest.parents) > 0, "resource_armature_desc.rest.parents.size cannot be 0");
	sm__assertf(array_len(desc->rest.joints) > 0, "resource_armature_desc.rest.joints.size cannot be 0");

	// sm__assertf(desc->names, "resource_armature_desc.names not initialized");
	// sm__assertf(array_len(desc->names) > 0, "resource_armature_desc.names.size cannot be 0");

	sm__assertf(desc->inverse_bind, "resource_armature_desc.inverse_bind not initialized");
	sm__assertf(array_len(desc->inverse_bind) > 0, "resource_armature_desc.inverse_bind.size cannot be 0");

	return (result);
#endif
}

armature_resource
resource_armature_make(const struct resource_armature_desc *desc)
{
	armature_resource result = resource_armature_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_armature *armature_at = resource_armature_at(result);
		armature_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_armature_validate(desc))
		{
			armature_at->slot.id = result.id;
			armature_at->slot.state = RESOURCE_STATE_OK;

			armature_at->rest = desc->rest;
			armature_at->bind = desc->bind;

			armature_at->inverse_bind = desc->inverse_bind;
			armature_at->names = desc->names;

			struct resource resource = resource_make(desc->label, RESOURCE_ARMATURE, armature_at->slot);
			armature_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

armature_resource
resource_armature_get_by_label(str8 label)
{
	armature_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_ARMATURE);

	result.id = resource->slot.id;

	return (result);
}

armature_resource
resource_armature_alloc(void)
{
	armature_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.armature_pool);
	result.id = handle;

	return result;
}

struct sm__resource_armature *
resource_armature_at(armature_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.armatures[slot_index];
}

static b32
fs_armature_write(struct fs_file *file, struct sm__resource_armature *armature)
{
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

	return (1);
}

static b32
fs_armature_read(struct fs_file *file, struct sm__resource_armature *armature)
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

	return (1);
}

static void
sm__resource_armature_trace(struct sm__resource_armature *armature)
{
	log_trace(str8_from("    - armatures joints: {u3d}"), array_len(armature->rest.parents));
}

static b32
sm__resource_clip_validate(sm__maybe_unused const struct resource_clip_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_clip_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_clip_desc not initialized");

	sm__assertf(desc->label.size > 0, "resource_clip_desc.label no set");

	sm__assertf(desc->tracks, "resource_clip_desc.tracks not initialized");
	sm__assertf(array_len(desc->tracks) > 0, "resource_clip_desc.bind.tracks.size cannot be 0");

	// TODO: check start_time and end_time isnan isinf

	return (result);
#endif
}

clip_resource
resource_clip_make(const struct resource_clip_desc *desc)
{
	clip_resource result = resource_clip_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_clip *clip_at = resource_clip_at(result);
		clip_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_clip_validate(desc))
		{
			clip_at->slot.id = result.id;
			clip_at->slot.state = RESOURCE_STATE_OK;

			clip_at->end_time = desc->end_time;
			clip_at->start_time = desc->start_time;
			clip_at->tracks = desc->tracks;
			clip_at->looping = desc->looping;

			struct resource resource = resource_make(desc->label, RESOURCE_CLIP, clip_at->slot);
			clip_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

clip_resource
resource_clip_get_by_label(str8 label)
{
	clip_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_CLIP);

	result.id = resource->slot.id;

	return (result);
}

static void
sm__write_track(struct fs_file *file, struct track *track)
{
	sm__assert(track->interpolation == ANIM_INTERPOLATION_CONSTANT ||
		   track->interpolation == ANIM_INTERPOLATION_LINEAR ||
		   track->interpolation == ANIM_INTERPOLATION_CUBIC);
	fs_write_u32(file, track->interpolation);

	sm__assert(track->track_type == ANIM_TRACK_TYPE_SCALAR || track->track_type == ANIM_TRACK_TYPE_V3 ||
		   track->track_type == ANIM_TRACK_TYPE_V4);
	fs_write_u32(file, track->track_type);

	// clang-format off
	switch (track->track_type)
	{
	case ANIM_TRACK_TYPE_SCALAR:
	{
		u32 len = array_len(track->frames_scalar);
		fs_write_u32(file, len);

		i64 bw =
		    PHYSFS_writeBytes(file->fsfile, track->frames_scalar, len * sizeof(*track->frames_scalar));
		if (bw != (i64)(len * sizeof(*track->frames_scalar)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_scalar"));
			sm__assert(0);
		}
	} break;

	case ANIM_TRACK_TYPE_V3:
	{
		u32 len = array_len(track->frames_v3);
		fs_write_u32(file, len);

		i64 bw = PHYSFS_writeBytes(file->fsfile, track->frames_v3, len * sizeof(*track->frames_v3));
		if (bw != (i64)(len * sizeof(*track->frames_v3)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_v3"));
			sm__assert(0);
		}
	} break;

	case ANIM_TRACK_TYPE_V4:
	{
		u32 len = array_len(track->frames_v4);
		fs_write_u32(file, len);

		i64 bw = PHYSFS_writeBytes(file->fsfile, track->frames_v4, len * sizeof(*track->frames_v4));
		if (bw != (i64)(len * sizeof(*track->frames_v4)))
		{
			sm__physfs_log_last_error(str8_from("error while writing frames_v4"));
			sm__assert(0);
		}
	} break;
	default: sm__assertf(0, "invalid track type");
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
	sm__assert(track->interpolation == ANIM_INTERPOLATION_CONSTANT ||
		   track->interpolation == ANIM_INTERPOLATION_LINEAR ||
		   track->interpolation == ANIM_INTERPOLATION_CUBIC);

	track->track_type = fs_read_u32(file);
	sm__assert(track->track_type == ANIM_TRACK_TYPE_SCALAR || track->track_type == ANIM_TRACK_TYPE_V3 ||
		   track->track_type == ANIM_TRACK_TYPE_V4);

	switch (track->track_type)
	{
	case ANIM_TRACK_TYPE_SCALAR:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_scalar, len);

			i64 br =
			    PHYSFS_readBytes(file->fsfile, track->frames_scalar, len * sizeof(*track->frames_scalar));
			if (br != (i64)(len * sizeof(*track->frames_scalar)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_scalar"));
				sm__assert(0);
			}
		}
		break;

	case ANIM_TRACK_TYPE_V3:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_v3, len);

			i64 br = PHYSFS_readBytes(file->fsfile, track->frames_v3, len * sizeof(*track->frames_v3));
			if (br != (i64)(len * sizeof(*track->frames_v3)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_v3"));
				sm__assert(0);
			}
		}
		break;

	case ANIM_TRACK_TYPE_V4:
		{
			u32 len = fs_read_u32(file);
			array_set_len(&RC.arena, track->frames_v4, len);

			i64 br = PHYSFS_readBytes(file->fsfile, track->frames_v4, len * sizeof(*track->frames_v4));
			if (br != (i64)(len * sizeof(*track->frames_v4)))
			{
				sm__physfs_log_last_error(str8_from("error while reading frames_v4"));
				sm__assert(0);
			}
		}
		break;
	default: sm__assertf(0, "invalid track type");
	}

	track->sampled_frames = fs_read_i32a(file);
}

clip_resource
resource_clip_alloc(void)
{
	clip_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.clip_pool);
	result.id = handle;

	return result;
}

struct sm__resource_clip *
resource_clip_at(clip_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.clips[slot_index];
}

static b32
fs_clip_write(struct fs_file *file, struct sm__resource_clip *clip)
{
	u32 len = array_len(clip->tracks);
	fs_write_u32(file, len);
	for (u32 i = 0; i < len; ++i)
	{
		fs_write_u32(file, clip->tracks[i].id);
		sm__write_track(file, &clip->tracks[i].position);
		sm__write_track(file, &clip->tracks[i].rotation);
		sm__write_track(file, &clip->tracks[i].scale);
	}

	fs_write_b32(file, clip->looping);
	fs_write_f32(file, clip->start_time);
	fs_write_f32(file, clip->end_time);

	return (1);
}

static b32
fs_clip_read(struct fs_file *file, struct sm__resource_clip *clip)
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

	clip->looping = fs_read_b32(file);
	clip->start_time = fs_read_f32(file);
	clip->end_time = fs_read_f32(file);

	return (1);
}

static void
sm__resource_clip_trace(struct sm__resource_clip *clip)
{
	log_trace(str8_from("        - start     : {f}"), (f64)clip->start_time);
	log_trace(str8_from("        - end       : {f}"), (f64)clip->end_time);
	log_trace(str8_from("        - duration  : {f}"), (f64)clip->end_time - (f64)clip->start_time);
	log_trace(str8_from("        - looping   : {b}"), clip->looping);
	log_trace(str8_from("        - tracks    : {u3d}"), array_len(clip->tracks));
}

f32
resource_clip_get_duration(clip_resource handle)
{
	sm__assert(handle.id != INVALID_HANDLE);
	struct sm__resource_clip *clip = resource_clip_at(handle);

	return sm__resource_clip_get_duration(clip);
}

static f32
sm__resource_clip_get_duration(struct sm__resource_clip *clip)
{
	sm__assert(clip);
	return (clip->end_time - clip->start_time);
}

// The Sample function takes a  posere ference and a time and returns a float
// value that is also a time. This function samples the animation clip at the
// provided time into the pose reference.

f32
resource_clip_sample(clip_resource handle, struct pose *pose, f32 t)
{
	sm__assert(handle.id != INVALID_HANDLE);
	struct sm__resource_clip *clip = resource_clip_at(handle);

	return sm__resourceclip_sample(clip, pose, t);
}

static f32
sm__resourceclip_sample(struct sm__resource_clip *clip, struct pose *pose, f32 t)
{
	sm__assert(clip);
	sm__assert(pose);

	if (sm__resource_clip_get_duration(clip) == 0.0f)
	{
		return (0.0f);
	}

	t = sm__resource_clip_adjust_time(clip, t);

	u32 size = array_len(clip->tracks);
	for (u32 i = 0; i < size; ++i)
	{
		u32 j = clip->tracks[i].id; // joint
		trs local = pose_get_local_transform(pose, j);
		trs animated = transform_track_sample(&clip->tracks[i], &local, t, clip->looping);

		pose->joints[j] = animated;
	}

	return (t);
}

static f32
sm__resource_clip_adjust_time(struct sm__resource_clip *clip, f32 t)
{
	sm__assert(clip);

	if (clip->looping)
	{
		f32 duration = clip->end_time - clip->start_time;
		if (duration <= 0.0f)
		{
			return (0.0f);
		}

		t = fmodf(t - clip->start_time, clip->end_time - clip->start_time);

		if (t < 0.0f)
		{
			t += clip->end_time - clip->start_time;
		}

		t += clip->start_time;
	}
	else
	{
		if (t < clip->start_time)
		{
			t = clip->start_time;
		}
		if (t > clip->end_time)
		{
			t = clip->end_time;
		}
	}

	return (t);
}

f32
resource_clip_adjust_time(clip_resource handle, f32 t)
{
	sm__assert(handle.id != INVALID_HANDLE);
	struct sm__resource_clip *clip = resource_clip_at(handle);

	return sm__resource_clip_adjust_time(clip, t);
}

static b32
sm__resource_text_validate(sm__maybe_unused const struct resource_text_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "resource_text_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "resource_text_desc not initialized");
	sm__assertf(desc->label.size > 0, "resource_text_desc.label no set");
	sm__assertf(desc->text.size > 0, "resource_text_desc.text not provided");

	return (result);
#endif
}

text_resource
resource_text_make(const struct resource_text_desc *desc)
{
	text_resource result = resource_text_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct sm__resource_text *text_at = resource_text_at(result);
		text_at->slot.state = RESOURCE_STATE_INVALID;

		if (sm__resource_text_validate(desc))
		{
			text_at->slot.id = result.id;
			text_at->slot.state = RESOURCE_STATE_OK;

			text_at->data = desc->text;

			struct resource resource = resource_make(desc->label, RESOURCE_TEXT, text_at->slot);
			text_at->slot.ref = resource_push(&resource);
		}
	}

	return (result);
}

text_resource
resource_text_get_by_label(str8 label)
{
	text_resource result = {INVALID_HANDLE};

	struct resource *resource = resource_get_by_label(label);
	sm__assert(resource->type == RESOURCE_TEXT);

	result.id = resource->slot.id;

	return (result);
}

text_resource
resource_text_alloc(void)
{
	text_resource result;

	handle_t handle = handle_new(&RC.arena, &RC.text_pool);
	result.id = handle;

	return result;
}

struct sm__resource_text *
resource_text_at(text_resource handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.texts[slot_index];
}

static b32
fs_text_write(struct fs_file *file, struct sm__resource_text *text)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, text->data.idata, text->data.size);
	if (bw != (i64)text->data.size)
	{
		sm__physfs_log_last_error(str8_from("error while writing text"));
		sm__assert(0);
	}

	return (1);
}

static b32
fs_text_read(struct fs_file *file, struct sm__resource_text *text)
{
	// u32 str_len = file->status.filesize;
	i64 str_len = PHYSFS_fileLength(file->fsfile);
	i8 *str_data = arena_reserve(&RC.arena, str_len + 1);
	i64 br = PHYSFS_readBytes(file->fsfile, str_data, str_len);
	if (br != (i64)str_len)
	{
		log_trace(str8_from("bytes read: {i6d}, len: {i6d}"), br, str_len);
		sm__physfs_log_last_error(str8_from("error while reading text"));
		sm__assert(0);
	}

	text->data.idata = str_data;
	text->data.idata[str_len] = 0;
	text->data.size = str_len;

	return (1);
}

static void
sm__resource_text_trace(struct sm__resource_text *text)
{
	log_trace(str8_from("        - text content:"));
	log_trace(text->data);
}

static void
fs_write_str8(struct fs_file *file, str8 str)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &str.size, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing str8 len"));
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, str.idata, str.size);
	if (bw != (i64)str.size)
	{
		sm__physfs_log_last_error(str8_from("error while writing str8"));
		sm__assert(0);
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
		sm__assert(0);
	}

	i8 *str_data = arena_reserve(&RC.arena, str_len + 1);
	br = PHYSFS_readBytes(file->fsfile, str_data, str_len);
	if (br != (i64)str_len)
	{
		sm__physfs_log_last_error(str8_from("error while reading str8"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(b8));
	if (bw != (i64)(len * sizeof(b8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing b8 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(b8));
	if (br != (i64)(len * sizeof(b8)))
	{
		sm__physfs_log_last_error(str8_from("error while reading b8 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u8));
	if (bw != (i64)(len * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u8 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u8));
	if (br != (i64)(len * sizeof(u8)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u8 array"));
		sm__assert(0);
	}

	return (result);
}

static void
fs_write_b32(struct fs_file *file, b32 data)
{
	i64 bw = PHYSFS_writeBytes(file->fsfile, &data, sizeof(b32));
	if (bw != (i64)sizeof(b32))
	{
		sm__physfs_log_last_error(str8_from("error while writing b32"));
		sm__assert(0);
	}
}

static b32
fs_read_b32(struct fs_file *file)
{
	b32 result;

	i64 br = PHYSFS_readBytes(file->fsfile, &result, sizeof(b32));
	if (br != (i64)sizeof(b32))
	{
		sm__physfs_log_last_error(str8_from("error while reading b32"));
		sm__assert(0);
	}

	return (result);
}

static void
fs_write_b32a(struct fs_file *file, array(b32) data)
{
	u32 len = array_len(data);
	i64 bw = PHYSFS_writeBytes(file->fsfile, &len, sizeof(u32));
	if (bw != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while writing b32 array len"));
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(b32));
	if (bw != (i64)(len * sizeof(b32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing b32 array"));
		sm__assert(0);
	}
}

static array(b32) fs_read_b32a(struct fs_file *file)
{
	array(b32) result = 0;

	u32 len = 0;
	i64 br = PHYSFS_readBytes(file->fsfile, &len, sizeof(u32));
	if (br != (i64)sizeof(u32))
	{
		sm__physfs_log_last_error(str8_from("error while reading b32 array len"));
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(b32));
	if (br != (i64)(len * sizeof(b32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading b32 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(i32));
	if (bw != (i64)(len * sizeof(i32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing i32 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(i32));
	if (br != (i64)(len * sizeof(i32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading i32 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u32));
	if (bw != (i64)(len * sizeof(u32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u32 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u32));
	if (br != (i64)(len * sizeof(u32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u32 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(u64));
	if (bw != (i64)(len * sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("error while writing u64 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(u64));
	if (br != (i64)(len * sizeof(u64)))
	{
		sm__physfs_log_last_error(str8_from("error while reading u64 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, data, len * sizeof(f32));
	if (bw != (i64)(len * sizeof(f32)))
	{
		sm__physfs_log_last_error(str8_from("error while writing f32 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(f32));
	if (br != (i64)(len * sizeof(f32)))
	{
		sm__physfs_log_last_error(str8_from("error while reading f32 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v2));
	if (bw != (i64)(len * sizeof(v2)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v2 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	bw = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v2));
	if (bw != (i64)(len * sizeof(v2)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v2 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v3));
	if (bw != (i64)(len * sizeof(v3)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v3 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	bw = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v3));
	if (bw != (i64)(len * sizeof(v3)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v3 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(v4));
	if (bw != (i64)(len * sizeof(v4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing v4 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(v4));
	if (br != (i64)(len * sizeof(v4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading v4 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(iv4));
	if (bw != (i64)(len * sizeof(iv4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing iv4 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(iv4));
	if (br != (i64)(len * sizeof(iv4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading iv4 array"));
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
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
		sm__assert(0);
	}

	bw = PHYSFS_writeBytes(file->fsfile, v, len * sizeof(m4));
	if (bw != (i64)(len * sizeof(m4)))
	{
		sm__physfs_log_last_error(str8_from("error while writing m4 array"));
		sm__assert(0);
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
		sm__assert(0);
	}

	array_set_len(&RC.arena, result, len);
	br = PHYSFS_readBytes(file->fsfile, result, len * sizeof(m4));
	if (br != (i64)(len * sizeof(m4)))
	{
		sm__physfs_log_last_error(str8_from("error while reading m4 array"));
		sm__assert(0);
	}

	return (result);
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

b32
sm___resource_mock_init(i8 *argv[], str8 mount_directory)
{
	struct buf m_resource = base_memory_reserve(MB(30));
	arena_make(&RC.arena, m_resource);
	arena_validate(&RC.arena);

	// clang-format off
	handle_pool_make(&RC.arena, &RC.image_pool   , RESOURCE_INITIAL_CAPACITY_IMAGES);
	handle_pool_make(&RC.arena, &RC.mesh_pool    , RESOURCE_INITIAL_CAPACITY_MESHES);
	handle_pool_make(&RC.arena, &RC.clip_pool    , RESOURCE_INITIAL_CAPACITY_CLIPS);
	handle_pool_make(&RC.arena, &RC.material_pool, RESOURCE_INITIAL_CAPACITY_MATERIALS);
	handle_pool_make(&RC.arena, &RC.armature_pool, RESOURCE_INITIAL_CAPACITY_ARMATUES);
	handle_pool_make(&RC.arena, &RC.scene_pool   , RESOURCE_INITIAL_CAPACITY_SCENES);
	handle_pool_make(&RC.arena, &RC.text_pool    , RESOURCE_INITIAL_CAPACITY_TEXTS);

	RC.resource_count = 0;
	RC.resources = arena_reserve(&RC.arena, sizeof(struct resource)          * RESOURCE_INITIAL_CAPACITY_RESOURCES);
	RC.images    = arena_reserve(&RC.arena, sizeof(struct sm__resource_image)* RESOURCE_INITIAL_CAPACITY_IMAGES);
	RC.meshes    = arena_reserve(&RC.arena, sizeof(struct sm__resource_mesh)     * RESOURCE_INITIAL_CAPACITY_MESHES);
	RC.clips     = arena_reserve(&RC.arena, sizeof(struct sm__resource_clip)     * RESOURCE_INITIAL_CAPACITY_CLIPS);
	RC.materials = arena_reserve(&RC.arena, sizeof(struct sm__resource_material) * RESOURCE_INITIAL_CAPACITY_MATERIALS);
	RC.armatures = arena_reserve(&RC.arena, sizeof(struct sm__resource_armature) * RESOURCE_INITIAL_CAPACITY_ARMATUES);
	RC.scenes    = arena_reserve(&RC.arena, sizeof(struct sm__resource_scene)    * RESOURCE_INITIAL_CAPACITY_SCENES);
	RC.texts    = arena_reserve(&RC.arena, sizeof(struct sm__resource_text)    * RESOURCE_INITIAL_CAPACITY_TEXTS);

	memset(RC.resources, 0x0, sizeof(struct resource)          * RESOURCE_INITIAL_CAPACITY_RESOURCES);
	memset(RC.images   , 0x0, sizeof(struct sm__resource_image)* RESOURCE_INITIAL_CAPACITY_IMAGES);
	memset(RC.meshes   , 0x0, sizeof(struct sm__resource_mesh)     * RESOURCE_INITIAL_CAPACITY_MESHES);
	memset(RC.clips    , 0x0, sizeof(struct sm__resource_clip)     * RESOURCE_INITIAL_CAPACITY_CLIPS);
	memset(RC.materials, 0x0, sizeof(struct sm__resource_material) * RESOURCE_INITIAL_CAPACITY_MATERIALS);
	memset(RC.armatures, 0x0, sizeof(struct sm__resource_armature) * RESOURCE_INITIAL_CAPACITY_ARMATUES);
	memset(RC.scenes   , 0x0, sizeof(struct sm__resource_scene)    * RESOURCE_INITIAL_CAPACITY_SCENES);
	memset(RC.texts   , 0x0, sizeof(struct sm__resource_text)    * RESOURCE_INITIAL_CAPACITY_TEXTS);
	// clang-format on

	PHYSFS_Allocator physfs_alloca = {
	    .Malloc = (void *(*)(unsigned long long))sm__physfs_malloc,
	    .Realloc = (void *(*)(void *, unsigned long long))sm__physfs_realloc,
	    .Free = sm__physfs_free,
	};
	PHYSFS_setAllocator(&physfs_alloca);

	if (!PHYSFS_init(argv[0]))
	{
		sm__physfs_log_last_error(str8_from("error while initializing."));
		return (0);
	}

	if (!PHYSFS_mount(mount_directory.idata, "/", 1))
	{
		sm__physfs_log_last_error(str8_from("error while mounting"));
		return (0);
	}

	// if (!PHYSFS_mount("assets/dump/dump.zip", "/", 1))
	// {
	// 	sm__physfs_log_last_error(str8_from("error while mounting"));
	// 	return (0);
	// }

	if (!PHYSFS_setWriteDir(mount_directory.idata))
	{
		sm__physfs_log_last_error(str8_from("error while setting write dir"));
		return (0);
	}

	RC.map = str8_resource_map_make(&RC.arena);

	// sm__resource_manager_load_defaults();

	return (1);
}

void
sm___resource_mock_teardown(void)
{
	PHYSFS_deinit();

	arena_release(&RC.arena);
	RC = (struct resource_manager){0};
	base_memory_reset();
}

b32
sm___resource_mock_read(struct resource *resource)
{
	return sm__resource_read(resource);
}

void
sm__resource_map_dirs(str8 *dirs, u32 n_dirs)
{
	for (u32 dir = 0; dir < n_dirs; ++dir)
	{
		sm__assert(dirs[dir].size > 0);
		i8 **rc = PHYSFS_enumerateFiles(dirs[dir].idata);
		for (i8 **i = rc; *i != 0; ++i)
		{
			struct str8_buf path_ctor = str_buf_begin(&RC.arena);
			str_buf_append(&RC.arena, &path_ctor, dirs[dir]);
			str_buf_append_char(&RC.arena, &path_ctor, '/');
			str8 file_name = (str8){.idata = *i, .size = (u32)strlen(*i)};
			str_buf_append(&RC.arena, &path_ctor, file_name);
			str8 uri = str_buf_end(&RC.arena, path_ctor);

			PHYSFS_Stat stat = {0};
			PHYSFS_stat(uri.idata, &stat);

			if (stat.filetype == PHYSFS_FILETYPE_REGULAR)
			{
				// TODO: impose an order: IMAGES -> MATERIALS -> MESHES -> SCENES
				struct resource resource = {.uri = uri};
				b32 prefetch = sm__resource_prefetch(&resource);
				if (!prefetch)
				{
					log_warn(str8_from("[{s}] ignoring file"), uri);
					arena_free(&RC.arena, uri.data);
				}
				else
				{
					resource_push(&resource);
				}
			}
			else
			{
				log_warn(str8_from("[{s}] it is not a regular file. Skipping"), uri);
				arena_free(&RC.arena, uri.data);
			}
		}
	}
}
