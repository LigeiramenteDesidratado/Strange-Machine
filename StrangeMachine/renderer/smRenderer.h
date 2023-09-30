#ifndef SM_RENDERER_H
#define SM_RENDERER_H

#include "core/smBase.h"
#include "core/smResource.h"
#include "ecs/smECS.h"
#include "math/smMath.h"

b8 renderer_init(u32 framebuffer_width, u32 framebuffer_height);
void renderer_teardown(void);
void renderer_on_resize(u32 width, u32 height);

void renderer_start_frame(void);
void renderer_finish_frame(void);

void renderer_set_clear_color(color c);
void renderer_clear_color(void);

void renderer_clear_color_buffer(void);
void renderer_clear_depth_buffer(void);

struct texture
{
	u32 width, height;

	enum
	{
		TEXTURE_PIXELFORMAT_NONE = 0,				  // 8 bit per pixel (no alpha)
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE = 0x00000001,  // 8 bit per pixel (no alpha)
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA = 0x00000002, // 8*2 bpp (2 channels)
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA = 0x00000003,	  // 8 bpp
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G6B5 = 0x00000004,	  // 16 bpp
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8 = 0x00000005,	  // 24 bpp
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1 = 0x00000006,	  // 16 bpp (1 bit alpha)
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4 = 0x00000007,	  // 16 bpp (4 bit alpha)
		TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 = 0x00000008,	  // 32 bpp
	} pixel_format;

	u8 *data;
};

enum state_bool
{
	// KEEP_CURRENT = 0
	STATE_TRUE = 1,
	STATE_FALSE = 2,
};

enum blend_mode
{
	// KEEP_CURRENT = 0,
	BLEND_MODE_ALPHA = 1,
	BLEND_MODE_ADDITIVE,
	BLEND_MODE_MULTIPLIED,
	BLEND_MODE_ADD_COLORS,
	BLEND_MODE_SUBTRACT_COLORS,
	BLEND_MODE_ALPHA_PREMULTIPLY,
	BLEND_MODE_CUSTOM,
	SM__BLEND_MODE_MAX,
};

struct blend_state
{
	enum state_bool enable;
	enum blend_mode mode;
};

enum depth_func
{
	// KEEP_CURRENT = 0,
	DEPTH_FUNC_NEVER = 1,
	DEPTH_FUNC_LESS,
	DEPTH_FUNC_EQUAL,
	DEPTH_FUNC_LEQUAL,
	DEPTH_FUNC_GREATER,
	DEPTH_FUNC_NOTEQUAL,
	DEPTH_FUNC_GEQUAL,
	DEPTH_FUNC_ALWAYS,
	SM__DEPTH_FUNC_MAX,
};

struct depth_state
{
	enum state_bool enable;
	enum depth_func depth_func;
};

enum cull_mode
{
	// KEEP_CURRENT = 0,
	CULL_MODE_FRONT = 1,
	CULL_MODE_BACK,
	CULL_MODE_FRONT_AND_BACK,
	SM__CULL_MODE_MAX,
};

enum polygon_mode
{
	// KEEP_CURRENT = 0,
	POLYGON_MODE_POINT = 1,
	POLYGON_MODE_LINE,
	POLYGON_MODE_FILL,
	SM__POLYGON_MODE_MAX,
};

enum winding_mode
{
	// KEEP_CURRENT = 0,
	WINDING_MODE_CLOCK_WISE = 1,
	WINDING_MODE_COUNTER_CLOCK_WISE,
	SM__WINDING_MODE_MAX,
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

u32 renderer_get_framebuffer_width(void);
u32 renderer_get_framebuffer_height(void);

void renderer_upload_texture(image_resource *image);
void renderer_upload_shader(struct shader_resource *shader);

void renderer_texture_add(str8 texture_name);
void renderer_texture_set(str8 texture_name, str8 sampler_name);

void renderer_shader_add(str8 shader_name, str8 vertex, str8 fragment);
void renderer_shader_set(str8 shader_name);
void renderer_shader_set_uniform(str8 name, void *value, u32 size, u32 count);

void renderer_blend_set(const struct blend_state *blend);
void renderer_depth_set(const struct depth_state *depth);
void renderer_rasterizer_set(const struct rasterizer_state *rasterizer);

void renderer_state_commit(void);
void renderer_state_clear(void);
void renderer_state_set_defaults(void);

void renderer_batch3D_begin(camera_component *camera);
void renderer_batch3D_end(void);

void renderer_batch_begin(void);
void renderer_batch_end(void);

void renderer_batch_sampler_set(str8 sampler);

void upload_mesh(mesh_component *mesh);
void draw_mesh(camera_component *camera, mesh_component *mesh, material_component *material, m4 model);
void draw_mesh2(mesh_component *mesh);
// void draw_mesh(camera_component2 *camera, mesh_component *mesh, material_component *material, m4 model);

void draw_grid(i32 slices, f32 spacing);
// void draw_sphere(v3 center_pos, f32 radius, u32 rings, u32 slices, color c);
void draw_sphere(trs transform, u32 rings, u32 slices, color c);
void draw_line_3D(v3 start_pos, v3 end_pos, color c);
void draw_capsule_wires(v3 startPos, v3 endPos, f32 radius, u32 slices, u32 rings, color color);
void draw_cube(trs t, color c);
void draw_cube_wires(trs t, color c);
void draw_aabb(struct aabb box, color c);
void draw_billboard(m4 view, v3 position, v2 size, color c);
void draw_plane(trs transform, color c);
void draw_ray(struct ray ray, color c);

u32 renderer_get_texture_size(u32 width, u32 height, u32 pixel_format);

void draw_rectangle(v2 position, v2 wh, f32 rotation, color c);
void draw_rectangle_uv(v2 position, v2 wh, v2 uv[4], b8 vertical_flip, color c);
void draw_circle(v2 center, f32 radius, f32 start_angle, f32 end_angle, u32 segments, color c);

void draw_text(str8 text, v2 pos, i32 font_size, color color);
// void draw_text_billboard_3d(v3 camp_position, str8 text, v3 pos, f32 font_size, color c);
void draw_text_billboard_3d(camera_component *camera, str8 text, f32 font_size, color c);

#endif // SM_RENDERER_H
