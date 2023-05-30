#ifndef SM_RENDERER_H
#define SM_RENDERER_H

#include "core/smBase.h"
#include "core/smResource.h"
#include "ecs/smECS.h"
#include "math/smMath.h"

b8 renderer_init(u32 width, u32 height);
void renderer_teardown(void);
void renderer_on_resize(u32 width, u32 height);

struct texture
{
	u32 width, height;

#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE  0x00000001 // 8 bit per pixel (no alpha)
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA 0x00000002 // 8*2 bpp (2 channels)
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA	    0x00000003 // 8 bpp
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G6B5	    0x00000004 // 16 bpp
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8	    0x00000005 // 24 bpp
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1   0x00000006 // 16 bpp (1 bit alpha)
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4   0x00000007 // 16 bpp (4 bit alpha)
#define TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8   0x00000008 // 32 bpp
	u32 pixel_format;

	u8 *data;
};

void renderer_upload_texture(image_resource *image);

void draw_begin(void);
void draw_end(void);

void draw_begin_3d(camera_component *camera);
void draw_end_3d(void);

void draw_grid(i32 slices, f32 spacing);
void draw_mesh(camera_component *camera, mesh_component *mesh, material_component *material, m4 model);
void upload_mesh(mesh_component *mesh);
void draw_sphere(v3 center_pos, f32 radius, u32 rings, u32 slices, color c);
void sm_draw_capsule_wires(v3 startPos, v3 endPos, f32 radius, u32 slices, u32 rings, color color);

struct camera2D
{
	v2 offset;    // Camera offset (displacement from target)
	v2 target;    // Camera target (rotation and zoom origin)
	f32 rotation; // Camera rotation in degrees
	f32 zoom;     // Camera zoom (scaling), should be 1.0f by default
};

void draw_begin2D(struct camera2D *camera);
void draw_end2D(void);

void draw_rectangle(v2 position, v2 wh, u32 texture, color c);
void draw_rectangle_uv(v2 position, v2 wh, u32 texture, v2 uv[4], b8 vertical_flip, color c);
void draw_circle(v2 center, f32 radius, f32 start_angle, f32 end_angle, u32 segments, color c);

#endif // SM_RENDERER_H
