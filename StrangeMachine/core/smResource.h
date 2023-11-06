#ifndef SM_CORE_RESOURCE_H
#define SM_CORE_RESOURCE_H

#include "core/smArray.h"
#include "core/smHandlePool.h"
#include "core/smRefCount.h"
#include "core/smString.h"

#include "animation/smPose.h"

#include "math/smMath.h"
#include "math/smShape.h"

// clang-format off
struct resource_handle { handle_t id; };
typedef struct resource_handle image_resource;
typedef struct resource_handle material_resource;
typedef struct resource_handle mesh_resource;
typedef struct resource_handle scene_resource;
typedef struct resource_handle armature_resource;
typedef struct resource_handle clip_resource;
typedef struct resource_handle text_resource;

enum resource_state
{
	RESOURCE_STATE_INITIAL,
	RESOURCE_STATE_ALLOC,
	RESOURCE_STATE_OK,
	RESOURCE_STATE_INVALID,

	// enforce 32-bit size enum
	SM__RESOURCE_STATE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct resource;
struct resource_slot
{
	enum resource_state state;
	handle_t id;

	REF(struct resource) ref;
};

// clang-format on

enum resource_type
{
	RESOURCE_NONE = 0,
	RESOURCE_IMAGE,
	RESOURCE_MATERIAL,
	RESOURCE_MESH,
	RESOURCE_SCENE,
	RESOURCE_ARMATURE,
	RESOURCE_CLIP,
	RESOURCE_TEXT,

	RESOURCE_MAX,

	// enforce 32-bit size enum
	SM__RESOURCE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct resource
{
	struct resource_slot slot;

	str8 label;
	str8 uri;
	enum resource_type type;

	struct ref_counter refs;
};

struct resource resource_make(str8 name, u32 type, struct resource_slot slot);
struct resource *resource_ref_inc(struct resource *resource);
void resource_ref_dec(struct resource *resource);
b32 resource_validate(struct resource *resource);
struct resource *resource_push(struct resource *resource);
struct resource *resource_get_by_label(str8 name);
void resource_trace(struct resource *resource);
void resource_write(struct resource *resource);
void resource_for_each(b32 (*cb)(str8, struct resource *, void *), void *user_data);

struct resource *resource_get_default_image(void);
struct resource *resource_get_default_material(void);

enum image_pixel_format
{
	IMAGE_PIXELFORMAT_NONE = 0x0,
	IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,  // 8 bit per pixel (no alpha)
	IMAGE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA, // 8*2 bpp (2 channels)
	IMAGE_PIXELFORMAT_UNCOMPRESSED_ALPHA,	   // 8 bpp
	IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G6B5,	   // 16 bpp
	IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8,	   // 24 bpp
	IMAGE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,   // 16 bpp (1 bit alpha)
	IMAGE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4,   // 16 bpp (4 bit alpha)
	IMAGE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,   // 32 bpp
	IMAGE_PIXELFORMAT_MAX,

	// enforce 32-bit size enum
	SM__IMAGE_PIXELFORMAT_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct resource_image_desc
{
	u32 _start_canary;

	str8 label;
	u32 width, height;

	enum image_pixel_format pixel_format;

	u8 *data;

	u32 _end_canary;
};

image_resource resource_image_make(const struct resource_image_desc *desc);

struct sm__resource_image
{
	// Alway the first item
	struct resource_slot slot;

	u32 width, height;

	enum image_pixel_format pixel_format;

	u8 *data;

	handle_t __texture_handle;
};

image_resource resource_image_get_by_label(str8 label);
struct sm__resource_image *resource_image_at(image_resource handle);
u32 resource_image_size(u32 width, u32 height, u32 pixel_format);

struct resource_material_desc
{
	u32 _start_canary;

	str8 label;

	color color;
	b32 double_sided;

	str8 image;

	u32 _end_canary;
};

material_resource resource_material_make(const struct resource_material_desc *desc);

struct sm__resource_material
{
	// Alway the first item
	struct resource_slot slot;

	color color;
	b32 double_sided;

	str8 image;
};

material_resource resource_material_get_by_label(str8 label);
struct sm__resource_material *resource_material_at(material_resource handle);

enum mesh_flags
{
	MESH_FLAG_NONE = 0,
	MESH_FLAG_DIRTY = BIT(0),
	MESH_FLAG_RENDERABLE = BIT(1),
	MESH_FLAG_SKINNED = BIT(2),
	MESH_FLAG_DRAW_AABB = BIT(3),
	MESH_FLAG_BLEND = BIT(4),
	MESH_FLAG_DOUBLE_SIDED = BIT(5),

	// enforce 32-bit size enum
	SM__MESH_FLAG_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct resource_mesh_desc
{
	u32 _start_canary;

	str8 label;

	array(v3) positions;
	array(v2) uvs;
	array(v4) colors;
	array(v3) normals;
	array(u32) indices;

	struct
	{
		array(v4) weights;
		array(iv4) influences;
		m4 *pose_palette;
	} skin_data;

	struct aabb aabb;
	enum mesh_flags flags;

	u32 _end_canary;
};

mesh_resource resource_mesh_make(const struct resource_mesh_desc *desc);

struct sm__resource_mesh
{
	// Alway the first item
	struct resource_slot slot;

	array(v3) positions;
	array(v2) uvs;
	array(v4) colors;
	array(v3) normals;
	array(u32) indices;

	struct
	{
		array(v4) weights;
		array(iv4) influences;

		// add an additional copy of the pose and normal data, as well as a matrix palette to use for CPU
		// skinning.
		m4 *pose_palette;
	} skin_data;

	struct aabb aabb;

	// TODO
	handle_t __position_handle;
	handle_t __uvs_handle;
	handle_t __colors_handle;
	handle_t __normals_handle;
	handle_t __indices_handle;

	enum mesh_flags flags;
};

mesh_resource resource_mesh_get_by_label(str8 label);
struct sm__resource_mesh *resource_mesh_at(mesh_resource handle);
void resource_mesh_calculate_aabb(mesh_resource handle);

enum node_prop
{
	NODE_PROP_NONE = 0,
	NODE_PROP_STATIC_BODY = BIT(0),
	NODE_PROP_RIGID_BODY = BIT(1),
	NODE_PROP_PLAYER = BIT(2),

	// enforce 32-bit size enum
	SM__NODE_PROP_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct sm__resource_scene_node
{
	str8 name;

	i32 parent_index;
	// array(i32) children;

	v3 position;
	v3 scale;
	v4 rotation;

	enum node_prop prop;

	str8 mesh;
	str8 material;
	str8 armature;
};

struct resource_scene_desc
{
	u32 _start_canary;
	str8 label;
	array(struct sm__resource_scene_node) nodes;
	u32 _end_canary;
};

scene_resource resource_scene_make(const struct resource_scene_desc *desc);

struct sm__resource_scene
{
	// Alway the first item
	struct resource_slot slot;

	array(struct sm__resource_scene_node) nodes;
};

scene_resource resource_scene_get_by_label(str8 label);
struct sm__resource_scene *resource_scene_at(scene_resource handle);

struct resource_armature_desc
{
	u32 _start_canary;

	str8 label;

	struct pose rest;
	struct pose bind;

	array(m4) inverse_bind;
	array(str8) names;
	u32 _end_canary;
};

armature_resource resource_armature_make(const struct resource_armature_desc *desc);

struct sm__resource_armature
{
	// Alway the first item
	struct resource_slot slot;

	struct pose rest;
	struct pose bind;

	array(m4) inverse_bind;
	array(str8) names;
};

armature_resource resource_armature_get_by_label(str8 label);
struct sm__resource_armature *resource_armature_at(armature_resource handle);

struct resource_clip_desc
{
	u32 _start_canary;

	str8 label;

	array(struct transform_track) tracks;
	b32 looping;
	f32 start_time, end_time;

	u32 _end_canary;
};

clip_resource resource_clip_make(const struct resource_clip_desc *desc);

struct sm__resource_clip
{
	// Alway the first item
	struct resource_slot slot;

	array(struct transform_track) tracks;
	b32 looping;
	f32 start_time, end_time;
};

clip_resource resource_clip_get_by_label(str8 label);
struct sm__resource_clip *resource_clip_at(clip_resource handle);
f32 resource_clip_get_duration(clip_resource handle);
f32 resource_clip_sample(clip_resource handle, struct pose *pose, f32 t);
f32 resource_clip_adjust_time(clip_resource handle, f32 t);

struct resource_text_desc
{
	u32 _start_canary;

	str8 label;
	str8 text;

	u32 _end_canary;
};

text_resource resource_text_make(const struct resource_text_desc *desc);

struct sm__resource_text
{
	// Alway the first item
	struct resource_slot slot;

	str8 data;
};

text_resource resource_text_get_by_label(str8 label);
struct sm__resource_text *resource_text_at(text_resource handle);

// file handling
extern const u32 FS_FILETYPE_REGULAR;
extern const u32 FS_FILETYPE_DIRECTORY;
extern const u32 FS_FILETYPE_SYMLINK;
extern const u32 FS_FILETYPE_OTHER;

struct fs_stat
{
	i64 filesize;	// size in bytes, -1 for non-files and unknown
	i64 modtime;	// last modification time
	i64 createtime; // like modtime, but for file creation time
	i64 accesstime; // like modtime, but for file access time
	u32 filetype;	// File? Directory? Symlink?
	i32 readonly;	// non-zero if read only, zero if writable.
};

struct fs_file
{
	b32 ok;
	void *fsfile;
	struct fs_stat status;
};
struct fs_file fs_file_open(str8 name, b32 read_only);
struct fs_file fs_file_open_read_cstr(const i8 *name);
struct fs_file fs_file_open_write_cstr(const i8 *name);
void fs_file_close(struct fs_file *resource_file);

i64 fs_file_write(struct fs_file *file, const void *buffer, size_t size);
i64 fs_file_read(struct fs_file *file, void *buffer, size_t size);
i32 fs_file_eof(struct fs_file *file);
i64 fs_file_tell(struct fs_file *file);
i32 fs_file_seek(struct fs_file *file, u64 position);

const i8 *fs_file_last_error(void);

// Conversion tool utils
b32 sm___resource_mock_init(i8 *argv[], str8 assets_folder);
void sm___resource_mock_teardown(void);
b32 sm___resource_mock_read(struct resource *resource);

void sm__resource_map_dirs(str8 *dirs, u32 n_dirs);

// struct sm__resource_image *sm___resource_mock_push_image(struct sm__resource_image *image);
// struct sm__resource_material *sm___resource_mock_push_material(struct sm__resource_material *material);
// struct sm__resource_mesh *sm___resource_mock_push_mesh(struct sm__resource_mesh *mesh);
// struct sm__resource_scene *sm___resource_mock_push_scene(struct sm__resource_scene *scene);
// struct sm__resource_armature *sm___resource_mock_push_armature(struct sm__resource_armature *armature);
// struct sm__resource_clip *sm___resource_mock_push_clip(struct sm__resource_clip *clip);

#endif // SM_CORE_RESOURCE_H
