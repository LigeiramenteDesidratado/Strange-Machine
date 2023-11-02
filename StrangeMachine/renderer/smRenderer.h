#ifndef SM_RENDERER_H
#define SM_RENDERER_H

#include "core/smBase.h"
#include "core/smResource.h"
#include "math/smMath.h"

b32 renderer_init(u32 framebuffer_width, u32 framebuffer_height);
void renderer_teardown(void);

struct renderer_slot
{
	handle_t id;
};

// clang-format off

typedef struct renderer_buffer_handle  { handle_t id; } buffer_handle;
typedef struct renderer_texture_handle { handle_t id; } texture_handle;
typedef struct renderer_sampler_handle { handle_t id; } sampler_handle;
typedef struct renderer_shader_handle  { handle_t id; } shader_handle;
typedef struct renderer_pipeline_handle{ handle_t id; } pipeline_handle;
typedef struct renderer_pass_handle    { handle_t id; } pass_handle;
typedef struct renderer_context_handle { handle_t id; } context_handle;

// clang-format on

enum buffer_type
{
	BUFFER_TYPE_DEFAULT = 0x0, // value 0 reserved for default-init
	BUFFER_TYPE_VERTEXBUFFER,
	BUFFER_TYPE_INDEXBUFFER,
	BUFFER_TYPE_MAX,

	// enforce 32-bit size enum
	SM__BUFFER_TYPE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

enum buffer_usage
{
	BUFFER_USAGE_DEFAULT = 0x0, // value 0 reserved for default-init
	BUFFER_USAGE_IMMUTABLE,
	BUFFER_USAGE_DYNAMIC,
	BUFFER_USAGE_STREAM,
	BUFFER_USAGE_MAX,

	// enforce 32-bit size enum
	SM__BUFFER_USAGE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct renderer_buffer_desc
{
	u32 _start_canary;
	str8 label;

	enum buffer_type buffer_type;
	enum buffer_usage usage;
	isize size;
	void *data;

	u32 _end_canary;
};

struct renderer_buffer
{
	struct renderer_slot slot;
	str8 label;

	enum buffer_type buffer_type;
	enum buffer_usage usage;
	isize size;
	void *data;

	u32 gl_handle;
};

struct renderer_buffer *renderer_buffer_at(buffer_handle handle);
buffer_handle renderer_buffer_make(const struct renderer_buffer_desc *desc);

enum texture_pixel_format
{
	TEXTURE_PIXELFORMAT_DEFAULT = 0x0,
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,  // 8 bit per pixel (no alpha)
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA, // 8*2 bpp (2 channels)
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA,	     // 8 bpp
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G6B5,     // 16 bpp
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8,     // 24 bpp
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,   // 16 bpp (1 bit alpha)
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4,   // 16 bpp (4 bit alpha)
	TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,   // 32 bpp

	TEXTURE_PIXELFORMAT_DEPTH,
	TEXTURE_PIXELFORMAT_DEPTH_STENCIL,

	TEXTURE_PIXELFORMAT_MAX,

	// enforce 32-bit size enum
	SM__TEXTURE_PIXELFORMAT_ENFORCE_ENUM_SIZE = 0x7fffffff
};

enum texture_usage
{
	TEXTURE_USAGE_DEFAULT = 0x0, // value 0 reserved for default-init
	TEXTURE_USAGE_IMMUTABLE,
	TEXTURE_USAGE_DYNAMIC,
	TEXTURE_USAGE_STREAM,
	TEXTURE_USAGE_MAX,

	// enforce 32-bit size enum
	SM__TEXTURE_USAGE_ENFORCE_ENUM_SIZE = 0x7FFFFFFF
};

struct renderer_texture_desc
{
	u32 _start_canary;

	image_resource handle;
	str8 label;

	struct
	{
		u32 width, height;
		enum texture_usage usage;
		enum texture_pixel_format pixel_format;
		void *data;
	};

	u32 _end_canary;
};

struct renderer_texture
{
	struct renderer_slot slot;
	str8 label;

	image_resource resource_handle;

	union
	{
		struct
		{
			u32 width, height;
			enum texture_usage usage;
			enum texture_pixel_format pixel_format;
			void *data;
		};
	};

	u32 gl_handle;
};

texture_handle renderer_texture_make(const struct renderer_texture_desc *desc);
struct renderer_texture *renderer_texture_at(texture_handle handle);

enum sampler_filter
{
	FILTER_DEFAULT = 0x0,
	FILTER_NONE,
	FILTER_NEAREST,
	FILTER_LINEAR,
	FILTER_MAX,

	// enforce 32-bit size enum
	SM__FILTER_ENFORCE_ENUM_SIZE = 0x7FFFFFFF
};

enum sampler_wrap
{
	WRAP_DEFAULT = 0x0, // value 0 reserved for default-init
	WRAP_REPEAT,
	WRAP_CLAMP_TO_EDGE,
	WRAP_CLAMP_TO_BORDER,
	WRAP_MIRRORED_REPEAT,
	WRAP_MAX,

	// enforce 32-bit size enum
	SM__WRAP_ENFORCE_ENUM_SIZE = 0x7FFFFFFF
};

enum sampler_border_color
{
	BORDER_COLOR_DEFAULT = 0x0,
	BORDER_COLOR_TRANSPARENT_BLACK,
	BORDER_COLOR_OPAQUE_BLACK,
	BORDER_COLOR_OPAQUE_WHITE,
	BORDER_COLOR_MAX,

	// enforce 32-bit size enum
	SM__BORDER_COLOR_ENFORCE_ENUM_SIZE = 0x7FFFFFFF
};

struct renderer_sampler_desc
{
	u32 _start_canary;

	str8 label;

	enum sampler_filter min_filter, mag_filter;
	enum sampler_wrap wrap_u, wrap_v, wrap_w;

	f32 min_lod, max_lod;
	enum sampler_border_color border_color;
	// enum compare_func compare;

	u32 _end_canary;
};

struct renderer_sampler
{
	struct renderer_slot slot;
	str8 label;

	enum sampler_filter min_filter, mag_filter;
	enum sampler_wrap wrap_u, wrap_v, wrap_w;

	f32 min_lod, max_lod;
	enum sampler_border_color border_color;

	u32 gl_handle;
};
struct renderer_sampler *renderer_sampler_at(sampler_handle handle);
sampler_handle renderer_sampler_make(const struct renderer_sampler_desc *desc);

#define MAX_COLOR_ATTACHMENTS 4

struct renderer_pass_desc
{
	u32 _start_canary;
	str8 label;

	texture_handle color_attachments[MAX_COLOR_ATTACHMENTS];
	texture_handle depth_stencil_attachment;

	u32 _end_canary;
};

struct renderer_pass
{
	struct renderer_slot slot;
	str8 label;

	u32 width, height;
	u32 color_attachments_count;
	texture_handle color_attachments[MAX_COLOR_ATTACHMENTS];
	texture_handle depth_stencil_attachment;

	u32 gl_handle; // framebuffer handle
};

enum load_action
{
	LOAD_ACTION_DEFAULT,
	LOAD_ACTION_CLEAR,
	LOAD_ACTION_LOAD,
	LOAD_ACTION_DONTCARE,

	// enforce 32-bit size enum
	SM__LOAD_ACTION_ENFORCE_ENUM_SIZE = 0x7fffffff
};

enum store_action
{
	STORE_ACTION_DEFAULT,
	STORE_ACTION_STORE,
	STORE_ACTION_DONTCARE,

	// enforce 32-bit size enum
	SM__STORE_ACTION_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct color_attachment_action
{
	enum load_action load_action;	// default: LOADACTION_CLEAR
	enum store_action store_action; // default: STOREACTION_STORE
	color clear_value;		// default: { 0.5f, 0.5f, 0.5f, 1.0f }
};

struct depth_attachment_action
{
	enum load_action load_action;	// default: LOADACTION_CLEAR
	enum store_action store_action; // default: STOREACTION_DONTCARE
	f32 clear_value;		// default: 1.0
};

struct renderer_pass_action
{
	u32 _start_canary;
	struct color_attachment_action colors[MAX_COLOR_ATTACHMENTS];
	struct depth_attachment_action depth;
	// struct renderer_attachment_action stencil;
	u32 _end_canary;
};

pass_handle renderer_pass_make(const struct renderer_pass_desc *desc);
struct renderer_pass *renderer_pass_at(pass_handle handle);
void renderer_pass_begin(pass_handle pass, const struct renderer_pass_action *pass_action);
void renderer_pass_end(void);

enum shader_type
{
	SHADER_TYPE_INVALID = 0x0,

	SHADER_TYPE_B8,
	SHADER_TYPE_I32,
	SHADER_TYPE_F32,

	SHADER_TYPE_V2,
	SHADER_TYPE_V3,
	SHADER_TYPE_V4,

	SHADER_TYPE_BV2,
	SHADER_TYPE_BV3,
	SHADER_TYPE_BV4,

	SHADER_TYPE_IV2,
	SHADER_TYPE_IV3,
	SHADER_TYPE_IV4,

	SHADER_TYPE_M2,
	SHADER_TYPE_M2X3,
	SHADER_TYPE_M2X4,

	SHADER_TYPE_M3X2,
	SHADER_TYPE_M3,
	SHADER_TYPE_M3X4,

	SHADER_TYPE_M4X2,
	SHADER_TYPE_M4X3,
	SHADER_TYPE_M4,

	SHADER_TYPE_SAMPLER_1D,
	SHADER_TYPE_SAMPLER_2D,
	SHADER_TYPE_SAMPLER_3D,

	SHADER_TYPE_SAMPLER_CUBE,
	SHADER_TYPE_SAMPLER_1D_SHADOW,
	SHADER_TYPE_SAMPLER_2D_SHADOW,

	SHADER_TYPE_MAX,

	// enforce 32-bit size enum
	SM__SHADER_TYPE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct shader_attribute
{
	str8 name;

	u32 size;
	enum shader_type type;
	i32 location;
};

struct shader_uniform
{
	str8 name;

	u32 size;
	enum shader_type type;
	i32 location;

	void *data; // cast to the proper type
};

struct shader_sampler
{
	str8 name;

	enum shader_type type;
	i32 location;

	b8 dirty;
};

struct renderer_shader_stage
{
	struct resource_handle handle;
	str8 source;
};

struct renderer_shader_desc
{
	u32 _start_canary;

	str8 label;

	struct renderer_shader_stage vs;
	struct renderer_shader_stage fs;

	u32 _end_canary;
};

struct renderer_shader
{
	struct renderer_slot slot;
	str8 label;

	struct renderer_shader_stage vs;
	struct renderer_shader_stage fs;

	u32 attributes_count;
	array(struct shader_attribute) attributes;

	u32 uniforms_count;
	array(struct shader_uniform) uniforms;

	u32 samplers_count;
	array(struct shader_sampler) samplers;

	u32 gl_vs_handle;
	u32 gl_fs_handle;
	u32 gl_shader_program_handle; // OpenGL program
};

void renderer_shader_set_uniform(str8 name, void *value, u32 size, u32 count);
struct renderer_shader *renderer_shader_at(shader_handle handle);
shader_handle renderer_shader_make(const struct renderer_shader_desc *desc);

enum state_bool
{
	STATE_DEFAULT = 0x0,
	STATE_TRUE,
	STATE_FALSE,
	STATE_MAX,

	// enforce 32-bit size enum
	SM__STATE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

#define state_bool_to_b8(_b) ((b8)((_b) == STATE_TRUE))

enum blend_mode
{
	BLEND_MODE_DEFAULT = 0x0,
	BLEND_MODE_ALPHA,
	BLEND_MODE_ADDITIVE,
	BLEND_MODE_MULTIPLIED,
	BLEND_MODE_ADD_COLORS,
	BLEND_MODE_SUBTRACT_COLORS,
	BLEND_MODE_ALPHA_PREMULTIPLY,
	BLEND_MODE_CUSTOM,
	BLEND_MODE_MAX,

	// enforce 32-bit size enum
	SM__BLEND_MODE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct blend_state
{
	enum state_bool enable;
	enum blend_mode mode;
};

enum depth_func
{
	DEPTH_FUNC_DEFAULT = 0x0,
	DEPTH_FUNC_NEVER,
	DEPTH_FUNC_LESS,
	DEPTH_FUNC_EQUAL,
	DEPTH_FUNC_LEQUAL,
	DEPTH_FUNC_GREATER,
	DEPTH_FUNC_NOTEQUAL,
	DEPTH_FUNC_GEQUAL,
	DEPTH_FUNC_ALWAYS,
	DEPTH_FUNC_MAX,

	// enforce 32-bit size enum
	SM__DEPTH_FUNC_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct depth_state
{
	enum state_bool enable;
	enum depth_func depth_func;
};

enum cull_mode
{
	CULL_MODE_DEFAULT = 0x0,
	CULL_MODE_FRONT,
	CULL_MODE_BACK,
	CULL_MODE_FRONT_AND_BACK,
	CULL_MODE_MAX,

	// enforce 32-bit size enum
	SM__CULL_MODE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

enum polygon_mode
{
	POLYGON_MODE_DEFAULT = 0x0,
	POLYGON_MODE_POINT,
	POLYGON_MODE_LINE,
	POLYGON_MODE_FILL,
	POLYGON_MODE_MAX,

	// enforce 32-bit size enum
	SM__POLYGON_MODE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

enum winding_mode
{
	WINDING_MODE_DEFAULT = 0x0,
	WINDING_MODE_CLOCK_WISE,
	WINDING_MODE_COUNTER_CLOCK_WISE,
	WINDING_MODE_MAX,

	// enforce 32-bit size enum
	SM__WINDING_MODE_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct rasterizer_state
{
	enum state_bool cull_enable;
	enum cull_mode cull_mode;

	enum winding_mode winding_mode;

	enum state_bool scissor;

	enum polygon_mode polygon_mode;

	f32 line_width;
};

#define MAX_TEXTURE_SLOTS     8
#define MAX_BUFFER_SLOTS      8
#define MAX_UNIFORM_SLOTS     8
#define MAX_VERTEX_ATTRIBUTES MAX_TEXTURE_SLOTS
#define MAX_VERTEX_BUFFERS    MAX_TEXTURE_SLOTS

enum vertex_format
{
	VERTEX_FORMAT_INVALID = 0x0,
	VERTEX_FORMAT_FLOAT,
	VERTEX_FORMAT_FLOAT2,
	VERTEX_FORMAT_FLOAT3,
	VERTEX_FORMAT_FLOAT4,
	VERTEX_FORMAT_BYTE4,
	VERTEX_FORMAT_BYTE4N,
	VERTEX_FORMAT_UBYTE4,
	VERTEX_FORMAT_UBYTE4N,
	VERTEX_FORMAT_SHORT2,
	VERTEX_FORMAT_SHORT2N,
	VERTEX_FORMAT_USHORT2N,
	VERTEX_FORMAT_SHORT4,
	VERTEX_FORMAT_SHORT4N,
	VERTEX_FORMAT_USHORT4N,
	VERTEX_FORMAT_UINT10_N2,
	VERTEX_FORMAT_HALF2,
	VERTEX_FORMAT_HALF4,
	VERTEX_FORMAT_MAX,

	// enforce 32-bit size enum
	SM__VERTEX_FORMAT_ENFORCE_ENUM_SIZE = 0x7fffffff
};

struct vertex_buffer_layout_state
{
	str8 name;
	u32 stride;
	// enum vertex_step step_func;
	u32 step_rate;
};

struct vertex_attr_state
{
	str8 name;
	u32 buffer_index;
	u32 offset;
	enum vertex_format format;
};

struct renderer_vertex_layout_state
{
	struct vertex_buffer_layout_state buffers[MAX_VERTEX_ATTRIBUTES];
	struct vertex_attr_state attrs[MAX_VERTEX_ATTRIBUTES];
};

struct renderer_pipeline_desc
{
	u32 _start_canary;

	str8 label;

	shader_handle shader;
	struct renderer_vertex_layout_state layout;

	struct blend_state blend;
	struct depth_state depth;
	struct rasterizer_state rasterizer;

	u32 _end_canary;
};

struct gl__vertex_attributes
{
	// i8 vb_index; // -1 if attr is not enabled
	// i8 divisor;  // -1 if not initialized
	u8 stride;
	u8 size;
	u8 normalized;
	i32 offset;
	u32 type;
};

struct renderer_pipeline

{
	struct renderer_slot slot;
	str8 label;

	shader_handle shader;
	// struct renderer_vertex_layout_state layout;
	struct gl__vertex_attributes attrs[MAX_VERTEX_ATTRIBUTES];

	struct blend_state blend;
	struct depth_state depth;
	struct rasterizer_state rasterizer;
};

struct renderer_pipeline *renderer_pipeline_at(pipeline_handle handle);
pipeline_handle renderer_pipeline_make(const struct renderer_pipeline_desc *desc);
void renderer_pipiline_apply(pipeline_handle pipeline);

struct texture_slot
{
	str8 name;
	texture_handle texture;
	sampler_handle sampler;
};

struct buffer_slot
{
	str8 name;
	buffer_handle buffer;
};

struct uniform_const
{
	str8 name;

	u32 count;
	enum shader_type type;

	void *data;
};

struct renderer_bindings
{
	u32 _start_canary;

	struct buffer_slot buffers[MAX_BUFFER_SLOTS];
	buffer_handle index_buffer;

	struct texture_slot textures[MAX_TEXTURE_SLOTS];
	struct uniform_const uniforms[MAX_UNIFORM_SLOTS];

	u32 _end_canary;
};

void renderer_bindings_apply(struct renderer_bindings *bindings);
void renderer_draw(u32 num_elements);

#endif // SM_RENDERER_H
