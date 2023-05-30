#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smResource.h"
#include "ecs/smECS.h"
#include "renderer/smRenderer.h"

#include "core/smLog.h"
#include "core/smMM.h"
#include "core/smString.h"

#include <vendor/glad/glad.h>

#ifndef GL_QUADS
#	define GL_QUADS 0x0007
#endif
#ifndef GL_LUMINANCE
#	define GL_LUMINANCE 0x1909
#endif
#ifndef GL_LUMINANCE_ALPHA
#	define GL_LUMINANCE_ALPHA 0x190A
#endif

struct vertex_buffer
{
	u32 element_count; // Number of elements in the buffer (QUADS)
	v3 *positions;
	v2 *uvs;
	color *colors;
	// SM_ARRAY(sm_vec3) normals;
	u32 *indices;

	u32 vao;     // openGL vertex array object
	u32 vbos[3]; // openGL vertex buffer objects
	u32 ebo;     // openGL vertex buffer object
};

struct draw_call
{
	u32 mode;	      // Drawing mode: LINES, TRIANGLES, QUADS...
	u32 vertex_counter;   // Number of vertex of the draw
	u32 vertex_alignment; // Number of vertex required for index alignment

	u32 texture;
};

struct batch
{
	struct vertex_buffer vertex_buffer;
#define MAX_BATCH_DRAW_CALLS 256
	u32 draws_len;
	struct draw_call *draws; // Draw calls array, depends on textureId
	f32 current_depth;	 // Current depth value for next draw
};

static struct batch sm__batch_make(void);
static void sm__batch_release(struct batch *batch);

struct renderer
{
	// batch being used for rendering
	struct batch batch;
	struct dyn_buf m_renderer;

	struct
	{
		u32 vertex_counter; // Number of vertex in the buffer
		v2 uv;		    // UV coordinates
		color c;

		enum matrix_mode
		{
			MATRIX_MODE_PROJECTION = 0x00000000,
			MATRIX_MODE_MODEL_VIEW = 0x00000001,
		} matrix_mode;

		// Flag for transform
		b8 transform_required;

		// Transform matrix
		m4 transform;

		// Pointer to current matrix
		m4 *current_matrix;

		m4 modelview;
		m4 projection;

		// Stack counter
		u32 stack_counter;
		m4 stack[32]; // Matrix stack for push/pop operations
			      //
		u32 default_program3D;
		u32 default_program_skinned_3D;
		u32 default_program;
		struct resource default_texture_ref;
		// TODO: defaults
		// Defaults
		// sm_resource_entity_s default_texture;
		// sm_resource_entity_s font_texture;
		//
		// sm_resource_entity_s default_shader;
		// sm_resource_entity_s default_2d_shader;
	} state;
};

static str8
gl__error_to_string(GLenum error)
{
	switch (error)
	{
	case GL_NO_ERROR: return str8_from("GL_NO_ERROR");
	case GL_INVALID_ENUM: return str8_from("GL_INVALID_ENUM");
	case GL_INVALID_VALUE: return str8_from("GL_INVALID_VALUE");
	case GL_INVALID_OPERATION: return str8_from("GL_INVALID_OPERATION");
	case GL_OUT_OF_MEMORY: return str8_from("GL_OUT_OF_MEMORY");
	default: return str8_from("UNKNOWN");
	}
}

static b8
gl__log_call(void)
{
	GLenum err;
	while ((err = glGetError()))
	{
		log_error(str8_from("[GL Error] ({u3d}): {s}"), err, gl__error_to_string(err));
		return false;
	}
	return true;
}

#define glCall(CALL)                             \
	do {                                     \
		gl__log_call();                  \
		CALL;                            \
		assert(gl__log_call() && #CALL); \
	} while (0)

static GLuint
gl__shader_compile_vert(const str8 vertex)
{
	GLuint v;
	glCall(v = glCreateShader(GL_VERTEX_SHADER));
	const i8 *v_source = (i8 *)vertex.data;
	glCall(glShaderSource(v, 1, &v_source, NULL));
	glCall(glCompileShader(v));
	GLint success = 0;
	glCall(glGetShaderiv(v, GL_COMPILE_STATUS, &success));
	if (success != GL_TRUE)
	{
		i8 info_log[2 * 512];
		glCall(glGetShaderInfoLog(v, 2 * 512, NULL, info_log));
		log_error(str8_from("vertex compilation failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(v));
		return 0;
	}
	return v;
}

static GLuint
gl__shader_compile_frag(const str8 fragment)
{
	GLuint f;
	glCall(f = glCreateShader(GL_FRAGMENT_SHADER));
	const char *f_source = (i8 *)fragment.data;
	glCall(glShaderSource(f, 1, &f_source, NULL));
	glCall(glCompileShader(f));
	GLint success = 0;
	glCall(glGetShaderiv(f, GL_COMPILE_STATUS, &success));
	if (!success)
	{
		char info_log[2 * 512];
		glCall(glGetShaderInfoLog(f, 2 * 512, NULL, info_log));
		log_error(str8_from("fragment compilation failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(f));
		return 0;
	}
	return f;
}

static b8
gl__shader_link(GLuint shader, GLuint vertex, GLuint fragment)
{
	glCall(glAttachShader(shader, vertex));
	glCall(glAttachShader(shader, fragment));
	glCall(glLinkProgram(shader));
	GLint success = 0;
	glCall(glGetProgramiv(shader, GL_LINK_STATUS, &success));
	if (!success)
	{
		char info_log[2 * 512];
		glCall(glGetShaderInfoLog(shader, 2 * 512, NULL, info_log));
		log_error(str8_from("shader linking failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(vertex));
		glCall(glDeleteShader(fragment));
		return false;
	}
	log_info(str8_from("compiled and linked shaders successfully"));
	return true;
}

static struct renderer RC;

// void renderer_begin(sm_draw_mode_e mode);
static void sm__renderer_begin(u32 mode);
static void sm__renderer_end(void);
// void renderer_set_texture(sm_resource_entity_s id);
static b8 sm__batch_check_limit(u32 vertices);
static void sm__batch_draw(void);

static void sm__position_v3(v3 position);
static void sm__position_3f(f32 x, f32 y, f32 z);
static void sm__position_v2(v2 position);
static void sm__position_2f(f32 x, f32 y);
static void sm__uv_v2(v2 uv);
static void sm__uv_2f(f32 x, f32 y);
static void sm__color_v4(v4 color);
static void sm__renderer_color_hex(u32 color);
static void sm__color_4ub(u8 r, u8 g, u8 b, u8 a);
static void sm__color(color color);
static void sm__renderer_m4_load_identity(void);
static void sm__renderer_m4_mul(mat4 mat);
static void sm__renderer_m4_mode(enum matrix_mode mode);
static void sm__renderer_m4_pop(void);
static void sm__renderer_m4_push(void);

static void
gl__get_GL_texture_formats(u32 pixel_format, u32 *gl_internal_format, u32 *gl_format, u32 *gl_type)
{
	*gl_internal_format = 0;
	*gl_format = 0;
	*gl_type = 0;
	switch (pixel_format)
	{
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
		{
			*gl_internal_format = GL_LUMINANCE;
			*gl_format = GL_LUMINANCE;
			*gl_type = GL_UNSIGNED_BYTE;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
		{
			*gl_internal_format = GL_LUMINANCE_ALPHA;
			*gl_format = GL_LUMINANCE_ALPHA;
			*gl_type = GL_UNSIGNED_BYTE;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA:
		{
			*gl_internal_format = GL_RED;
			*gl_format = GL_RED;
			*gl_type = GL_UNSIGNED_BYTE;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
		{
			*gl_internal_format = GL_RGB;
			*gl_format = GL_RGB;
			*gl_type = GL_UNSIGNED_SHORT_5_6_5;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
		*gl_internal_format = GL_RGB;
		{
			*gl_format = GL_RGB;
			*gl_type = GL_UNSIGNED_BYTE;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
		{
			*gl_internal_format = GL_RGBA;
			*gl_format = GL_RGBA;
			*gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
		{
			*gl_internal_format = GL_RGBA;
			*gl_format = GL_RGBA;
			*gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
		}
		break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
		{
			*gl_internal_format = GL_RGBA;
			*gl_format = GL_RGBA;
			*gl_type = GL_UNSIGNED_BYTE;
		}
		break;
	default: sm__unreachable(); break;
	}
}

u32
renderer_get_texture_size(u32 width, u32 height, u32 pixel_format)
{
	u32 result = 0; // Size in bytes
	u32 bpp = 0;	// Bits per pixel
	switch (pixel_format)
	{
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA:
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: bpp = 8; break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4: bpp = 16; break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: bpp = 32; break;
	case TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8: bpp = 24; break;
	default: sm__unreachable(); break;
	}

	result = width * height * bpp / 8; // Total data size in bytes

	return (result);
}

void
renderer_upload_texture(image_resource *image)
{
	assert(image->texture_handle == 0);
	u32 handle = 0;

	u32 width = image->width;
	u32 height = image->height;
	u32 pixel_format = image->pixel_format;
	u8 *data = image->data;

	u32 gl_internal_format, gl_format, gl_type;
	gl__get_GL_texture_formats(image->pixel_format, &gl_internal_format, &gl_format, &gl_type);

	glCall(glGenTextures(1, &handle));
	glCall(glBindTexture(GL_TEXTURE_2D, handle));

	glCall(glTexImage2D(
	    GL_TEXTURE_2D, 0, (i32)gl_internal_format, (i32)width, (i32)height, 0, gl_format, gl_type, data));

	if (pixel_format == TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
	{
		i32 swizzle_mask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
		glCall(glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask));
	}
	else if (pixel_format == TEXTURE_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
	{
		i32 swizzle_mask[] = {GL_RED, GL_RED, GL_RED, GL_ALPHA};
		glCall(glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask));
	}
	else if (pixel_format == TEXTURE_PIXELFORMAT_UNCOMPRESSED_ALPHA)
	{
		i32 swizzle_mask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
		glCall(glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask));
	}

	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	glCall(glBindTexture(GL_TEXTURE_2D, 0));

	image->texture_handle = handle;
}

static u32
sm__default_program3D(void)
{
	u32 result = 0;

	struct shaders shaders =
	    load_shader(str8_from("shaders/default3D.fragment"), str8_from("shaders/default3D.vertex"));

	if (shaders.vertex->id == 0) { shaders.vertex->id = gl__shader_compile_vert(shaders.vertex->vertex); }

	if (shaders.fragment->id == 0) { shaders.fragment->id = gl__shader_compile_frag(shaders.fragment->fragment); }

	assert(shaders.vertex->id && shaders.fragment->id);

	glCall(result = glCreateProgram());
	if (!gl__shader_link(result, shaders.vertex->id, shaders.fragment->id))
	{
		glCall(glDeleteProgram(result));
		glCall(glDeleteShader(shaders.fragment->id));
		glCall(glDeleteShader(shaders.vertex->id));
		log_error(str8_from("error linking program shader"));

		return 0;
	}

	return (result);
}

static u32
sm__default_program_skinned_3D(void)
{
	u32 result = 0;

	struct shaders shaders =
	    load_shader(str8_from("shaders/default3D.fragment"), str8_from("shaders/skinned.vertex"));

	if (shaders.vertex->id == 0) { shaders.vertex->id = gl__shader_compile_vert(shaders.vertex->vertex); }

	if (shaders.fragment->id == 0) { shaders.fragment->id = gl__shader_compile_frag(shaders.fragment->fragment); }

	assert(shaders.vertex->id && shaders.fragment->id);

	glCall(result = glCreateProgram());
	if (!gl__shader_link(result, shaders.vertex->id, shaders.fragment->id))
	{
		glCall(glDeleteProgram(result));
		glCall(glDeleteShader(shaders.fragment->id));
		glCall(glDeleteShader(shaders.vertex->id));
		log_error(str8_from("error linking program shader"));

		return (0);
	}

	return (result);
}

static u32
sm__default_program(void)
{
	u32 result = 0;

	struct shaders shaders =
	    load_shader(str8_from("shaders/default.fragment"), str8_from("shaders/default.vertex"));

	if (shaders.fragment->id == 0) { shaders.fragment->id = gl__shader_compile_frag(shaders.fragment->fragment); }
	if (shaders.vertex->id == 0) { shaders.vertex->id = gl__shader_compile_vert(shaders.vertex->vertex); }

	glCall(result = glCreateProgram());
	if (!gl__shader_link(result, shaders.vertex->id, shaders.fragment->id))
	{
		glCall(glDeleteProgram(result));
		glCall(glDeleteShader(shaders.fragment->id));
		glCall(glDeleteShader(shaders.vertex->id));
		log_error(str8_from("error linking program shader"));

		return 0;
	}

	return (result);
}

static void
sm__renderer_print_extensions(void)
{
	const u8 *extensions = glGetString(GL_EXTENSIONS);
	if (!extensions) { printf("Error: Unable to get OpenGL extensions.\n"); }
	else
	{
		printf("OpenGL Extensions:\n");
		const char *start = (const char *)extensions;
		const char *end = start + strlen((const char *)extensions);
		const char *p = start;
		while (p < end)
		{
			while (*p == ' ') p++;
			const char *q = p;
			while (*q != ' ' && q < end) q++;
			printf("%.*s\n", (int)(q - p), p);
			p = q;
		}
	}
}

b8
renderer_init(u32 width, u32 height)
{
	struct buf m_base = base_memory_begin();
	RC.m_renderer = (struct dyn_buf){
	    .data = m_base.data,
	    .cap = m_base.size,
	    .len = 0,
	};

	RC.state.default_program3D = sm__default_program3D();
	RC.state.default_program_skinned_3D = sm__default_program_skinned_3D();
	RC.state.default_program = sm__default_program();
	if (!RC.state.default_program3D || !RC.state.default_program || !RC.state.default_program_skinned_3D)
	{
		log_error(str8_from("error creating program default resource"));
		return (false);
	}

	RC.state.default_texture_ref = resource_get_default_image();
	struct image_resource *default_image = RC.state.default_texture_ref.image_data;
	if (!default_image->texture_handle) { renderer_upload_texture(default_image); }

	RC.batch = sm__batch_make();

	// init stack matrices
	glm_mat4_identity_array(&RC.state.stack[0].data, ARRAY_SIZE(RC.state.stack));

	RC.state.modelview = m4_identity();
	RC.state.projection = m4_identity();
	RC.state.current_matrix = &RC.state.modelview;
	RC.state.transform_required = false;
	RC.state.transform = m4_identity();

	glCall(glDepthFunc(GL_LEQUAL));	  // Type of depth testing to apply
	glCall(glDisable(GL_DEPTH_TEST)); // Disable depth testing for 2D (only used for 3D))

	// Init state: Blending mode
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glCall(glEnable(GL_BLEND)); // Enable color blending (required to work with transparencies)

	// Init state: Culling
	// NOTE: All shapes/models triangles are drawn CCW
	glCall(glCullFace(GL_BACK));	// Cull the back face (default))
	glCall(glFrontFace(GL_CCW));	// Front face are defined counter clockwise (default))
	glCall(glEnable(GL_CULL_FACE)); // Enable backface culling

	glCall(glEnable(GL_SCISSOR_TEST));

	glCall(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	glCall(glClearDepth(1.0));
	glCall(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

	base_memory_end(RC.m_renderer.len);
	return (true);
}

void
renderer_teardown(void)
{
	sm__batch_release(&RC.batch);
}

#define QUAD_SIZE	 4
#define INDICES_PER_QUAD 6
#define VIRTICES	 16384

static struct batch
sm__batch_make(void)
{
	struct batch result = {0};

	struct vertex_buffer vertex_buffer;

	vertex_buffer.element_count = VIRTICES;
	// vertex_buffer.positions = mm_malloc(sizeof(v3) * VIRTICES * QUAD_SIZE);
	vertex_buffer.positions = (v3 *)(RC.m_renderer.data + RC.m_renderer.len);
	RC.m_renderer.len += sizeof(v3) * VIRTICES * QUAD_SIZE;

	// vertex_buffer.uvs = mm_malloc(sizeof(v2) * VIRTICES * QUAD_SIZE);
	vertex_buffer.uvs = (v2 *)(RC.m_renderer.data + RC.m_renderer.len);
	RC.m_renderer.len += sizeof(v2) * VIRTICES * QUAD_SIZE;

	// vertex_buffer.colors = mm_malloc(sizeof(color) * VIRTICES * QUAD_SIZE);
	vertex_buffer.colors = (color *)(RC.m_renderer.data + RC.m_renderer.len);
	RC.m_renderer.len += sizeof(color) * VIRTICES * QUAD_SIZE;

	// vertex_buffer.indices = mm_malloc(sizeof(u32) * VIRTICES * INDICES_PER_QUAD);
	vertex_buffer.indices = (u32 *)(RC.m_renderer.data + RC.m_renderer.len);
	RC.m_renderer.len += sizeof(u32) * VIRTICES * INDICES_PER_QUAD;

	u32 k = 0;
	for (u32 i = 0; i < (VIRTICES * INDICES_PER_QUAD); i += 6)
	{
		vertex_buffer.indices[i] = 4 * k;
		vertex_buffer.indices[i + 1] = 4 * k + 1;
		vertex_buffer.indices[i + 2] = 4 * k + 2;
		vertex_buffer.indices[i + 3] = 4 * k;
		vertex_buffer.indices[i + 4] = 4 * k + 2;
		vertex_buffer.indices[i + 5] = 4 * k + 3;
		k++;
	}
	// sm_resource_shader_s *shader_resource = sm_resource_manager_get_data(RENDERER->state.default_shader);
	// glCall(glUseProgram(RC.state.default_program));
	glCall(glGenVertexArrays(1, &vertex_buffer.vao));
	glCall(glBindVertexArray(vertex_buffer.vao));

	// positions
	glCall(glGenBuffers(1, &vertex_buffer.vbos[0]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[0]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(v3) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));

	i32 loc = -1;
	glCall(loc = glGetAttribLocation(RC.state.default_program, "a_position"));
	assert(loc != -1);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 3, GL_FLOAT, false, 0, 0));

	// uvs
	glCall(glGenBuffers(1, &vertex_buffer.vbos[1]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[1]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(v2) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));
	glCall(loc = glGetAttribLocation(RC.state.default_program, "a_uv"));
	assert(loc != -1);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 2, GL_FLOAT, false, 0, 0));

	// colors
	glCall(glGenBuffers(1, &vertex_buffer.vbos[2]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[2]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(color) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));
	glCall(loc = glGetAttribLocation(RC.state.default_program, "a_color"));
	assert(loc != -1);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, true, 0, 0));

	// normals
	// glCall(glGenBuffers(1, &vertex_buffer.vbos[3]));
	// glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[3]));
	// glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(v3) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));
	// loc = sm_resource_shader_get_location(shader_resource, "a_normal");
	// SM_ASSERT(loc != -1);
	// glCall(glEnableVertexAttribArray(loc));
	// glCall(glVertexAttribPointer(loc, 3, GL_FLOAT, false, 0, 0));
	// indices
	glCall(glGenBuffers(1, &vertex_buffer.ebo));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_buffer.ebo));
	glCall(glBufferData(
	    GL_ELEMENT_ARRAY_BUFFER, sizeof(u32) * VIRTICES * INDICES_PER_QUAD, vertex_buffer.indices, GL_STATIC_DRAW));
	glCall(glBindVertexArray(0));

	// just in case
	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

#undef QUAD_SIZE
#undef INDICES_PER_QUAD
#undef VIRTICES

	if (vertex_buffer.vao > 0) { log_info(str8_from("verttex buffer loaded successfully to VRAM")); }
	result.vertex_buffer = vertex_buffer;
	// result.draws = mm_malloc(sizeof(struct draw_call) * MAX_BATCH_DRAW_CALLS);
	result.draws = (struct draw_call *)(RC.m_renderer.data + RC.m_renderer.len);
	RC.m_renderer.len += (sizeof(struct draw_call) * MAX_BATCH_DRAW_CALLS);

	for (u32 i = 0; i < MAX_BATCH_DRAW_CALLS; ++i)
	{
		result.draws[i].mode = GL_QUADS;
		result.draws[i].vertex_counter = 0;
		result.draws[i].vertex_alignment = 0;
		result.draws[i].texture = RC.state.default_texture_ref.image_data->texture_handle;
	}
	result.draws_len = 1;
	result.current_depth = -1.0f; // Reset depth value
	return (result);
}

static void
sm__batch_release(struct batch *batch)
{
	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	glCall(glBindVertexArray(batch->vertex_buffer.vao));

	glCall(glDisableVertexAttribArray((u32)glGetAttribLocation(RC.state.default_program, "a_color")));
	glCall(glDisableVertexAttribArray((u32)glGetAttribLocation(RC.state.default_program, "a_uv")));
	glCall(glDisableVertexAttribArray((u32)glGetAttribLocation(RC.state.default_program, "a_position")));

	glCall(glBindVertexArray(0));
	glCall(glDeleteBuffers(1, &batch->vertex_buffer.vbos[0]));
	glCall(glDeleteBuffers(1, &batch->vertex_buffer.vbos[1]));
	glCall(glDeleteBuffers(1, &batch->vertex_buffer.vbos[2]));

	// glCall(glDeleteBuffers(1, &batch->vertex_buffer.vbos[4]));
	glCall(glDeleteBuffers(1, &batch->vertex_buffer.ebo));
	glCall(glDeleteVertexArrays(1, &batch->vertex_buffer.vao));

	// mm_free(batch->vertex_buffer.positions);
	// mm_free(batch->vertex_buffer.uvs);
	// mm_free(batch->vertex_buffer.colors);
	// mm_free(batch->vertex_buffer.normals);
	// mm_free(batch->vertex_buffer.indices);
	// mm_free(batch->draws);
}

static void
sm__batch_draw(void)
{
	if (RC.state.vertex_counter == 0) { goto reset; }

	glCall(glBindVertexArray(RC.batch.vertex_buffer.vao));
	u32 v_counter = RC.state.vertex_counter;

	// sub position data
	glCall(glBindBuffer(GL_ARRAY_BUFFER, RC.batch.vertex_buffer.vbos[0]));
	glCall(glBufferSubData(GL_ARRAY_BUFFER, 0, v_counter * sizeof(v3), RC.batch.vertex_buffer.positions));

	// sub uv data
	glCall(glBindBuffer(GL_ARRAY_BUFFER, RC.batch.vertex_buffer.vbos[1]));
	glCall(glBufferSubData(GL_ARRAY_BUFFER, 0, v_counter * sizeof(v2), RC.batch.vertex_buffer.uvs));

	// sub color data
	glCall(glBindBuffer(GL_ARRAY_BUFFER, RC.batch.vertex_buffer.vbos[2]));
	glCall(glBufferSubData(GL_ARRAY_BUFFER, 0, v_counter * sizeof(color), RC.batch.vertex_buffer.colors));

	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));

	glCall(glUseProgram(RC.state.default_program));

	m4 mvp;
	glm_mat4_mul(RC.state.projection.data, RC.state.modelview.data, mvp.data);

	i32 loc = -1;
	glCall(loc = glGetUniformLocation(RC.state.default_program, "u_pvm"));
	assert(loc != -1);
	glCall(glUniformMatrix4fv(loc, 1, GL_FALSE, mvp.float16));

	glCall(loc = glGetUniformLocation(RC.state.default_program, "u_tex0"));
	assert(loc != -1);
	glCall(glUniform1i(loc, 0));

	glCall(glActiveTexture(GL_TEXTURE0 + 0));

	for (u32 i = 0, vertex_offset = 0; i < RC.batch.draws_len; ++i)
	{
		struct draw_call *d_call = &RC.batch.draws[i];
		u32 text =
		    (d_call->texture) ? d_call->texture : RC.state.default_texture_ref.image_data->texture_handle;
		glCall(glBindTexture(GL_TEXTURE_2D, text));
		if (d_call->mode == GL_LINES || d_call->mode == GL_TRIANGLES)
		{
			glCall(glDrawArrays(d_call->mode, (i32)vertex_offset, (i32)d_call->vertex_counter));
		}
		else
		{
			glCall(glDrawElements(GL_TRIANGLES, d_call->vertex_counter / 4 * 6, GL_UNSIGNED_INT,
			    (void *)(vertex_offset / 4 * 6 * sizeof(u32))));
		}
		vertex_offset += (d_call->vertex_counter + d_call->vertex_alignment);
		glCall(glBindTexture(GL_TEXTURE_2D, 0));
	}

	// glCall(glBindTexture(GL_TEXTURE_2D, 0));
	glCall(glBindVertexArray(0));
	glCall(glUseProgram(0));

reset:
	RC.state.vertex_counter = 0;
	RC.batch.current_depth = -1.0f;
	for (u32 i = 0; i < MAX_BATCH_DRAW_CALLS; ++i)
	{
		RC.batch.draws[i].mode = GL_QUADS;
		RC.batch.draws[i].vertex_counter = 0;
		RC.batch.draws[i].texture = RC.state.default_texture_ref.image_data->texture_handle;
	}
	RC.batch.draws_len = 1;
}

static b8
sm__batch_check_limit(u32 vertices)
{
	b8 overflow = false;
	if (RC.state.vertex_counter + vertices >= RC.batch.vertex_buffer.element_count * 4)
	{
		u32 current_mode = RC.batch.draws[RC.batch.draws_len - 1].mode;
		u32 current_texture = RC.batch.draws[RC.batch.draws_len - 1].texture;
		overflow = true;
		sm__batch_draw();
		RC.batch.draws[RC.batch.draws_len - 1].mode = current_mode;
		RC.batch.draws[RC.batch.draws_len - 1].texture = current_texture;
	}
	return (overflow);
}

static void
sm__renderer_begin(u32 mode)
{
	const u32 draw_counter = RC.batch.draws_len;
	struct draw_call *d_call = &RC.batch.draws[draw_counter - 1];

	if (d_call->mode == mode) return;

	if (d_call->vertex_counter > 0)
	{
		if (d_call->mode == GL_LINES)
		{
			d_call->vertex_alignment =
			    ((d_call->vertex_counter < 4) ? d_call->vertex_counter : d_call->vertex_counter % 4);
		}
		else if (d_call->mode == GL_TRIANGLES)
		{
			d_call->vertex_alignment =
			    ((d_call->vertex_counter < 4) ? 1 : (4 - (d_call->vertex_counter % 4)));
		}
		else { d_call->vertex_alignment = 0; }
		if (!sm__batch_check_limit(d_call->vertex_alignment))
		{
			RC.state.vertex_counter += d_call->vertex_alignment;
			RC.batch.draws_len++;
			// draw_counter = (u32)SM_ARRAY_LEN(RC.batch.draws) + 1;
		}
	}
	if (RC.batch.draws_len >= MAX_BATCH_DRAW_CALLS) sm__batch_draw();

	d_call = &RC.batch.draws[RC.batch.draws_len - 1];
	d_call->mode = mode;
	d_call->vertex_counter = 0;
	d_call->texture = RC.state.default_texture_ref.image_data->texture_handle;
}

static void
sm__renderer_end(void)
{
	RC.batch.current_depth += (1.0f / 20000.0f);
	if (RC.state.vertex_counter >= (RC.batch.vertex_buffer.element_count * 4 - 4))
	{
		for (u32 i = RC.state.stack_counter; i >= 0; --i) { sm__renderer_m4_pop(); }
		sm__batch_draw();
	}
}

static void
sm__renderer_set_texture(u32 id)
{
	if (id == 0)
	{
		// NOTE: If quads batch limit is reached, we force a draw call
		// and next batch starts
		if (RC.state.vertex_counter >= RC.batch.vertex_buffer.element_count * 4) { sm__batch_draw(); }
		return;
	}

	struct draw_call *d_call = &RC.batch.draws[RC.batch.draws_len - 1];
	if (d_call->texture == id) return;

	if (d_call->vertex_counter > 0)
	{
		// Make sure current
		// RLGL.currentBatch->draws[i].vertexCount is
		// aligned a multiple of 4, that way, following
		// QUADS drawing will keep aligned with index
		// processing It implies adding some extra
		// alignment vertex at the end of the draw,
		// those vertex are not processed but they are
		// considered as an additional offset for the
		// next set of vertex to be drawn
		if (d_call->mode == GL_LINES)
		{
			d_call->vertex_alignment =
			    ((d_call->vertex_counter < 4) ? d_call->vertex_counter : d_call->vertex_counter % 4);
		}
		else if (d_call->mode == GL_TRIANGLES)
		{
			d_call->vertex_alignment =
			    ((d_call->vertex_counter < 4) ? 1 : (4 - (d_call->vertex_counter % 4)));
		}
		else { d_call->vertex_alignment = 0; }
		if (!sm__batch_check_limit(d_call->vertex_alignment))
		{
			RC.state.vertex_counter += d_call->vertex_alignment;
			RC.batch.draws_len++;
		}
	}

	if (RC.batch.draws_len >= MAX_BATCH_DRAW_CALLS) sm__batch_draw();
	RC.batch.draws[RC.batch.draws_len - 1].texture = id;
	RC.batch.draws[RC.batch.draws_len - 1].vertex_counter = 0;
}

static void
sm__position_v3(v3 position)
{
	struct vertex_buffer *v_buf = &RC.batch.vertex_buffer;
	if (RC.state.vertex_counter > v_buf->element_count * 4 - 4)
	{
		str8_println(str8_from("batch elements overflow"));
		return;
	}

	v3 transformed_position = position;
	if (RC.state.transform_required)
	{
		glm_mat4_mulv3(RC.state.transform.data, position.data, 1.0f, transformed_position.data);
	}

	glm_vec3_copy(transformed_position.data, v_buf->positions[RC.state.vertex_counter].data);
	glm_vec2_copy(RC.state.uv.data, v_buf->uvs[RC.state.vertex_counter].data);
	v_buf->colors[RC.state.vertex_counter] = RC.state.c;

	RC.state.vertex_counter++;
	RC.batch.draws[RC.batch.draws_len - 1].vertex_counter++;
}

static void
sm__position_3f(f32 x, f32 y, f32 z)
{
	sm__position_v3(v3_new(x, y, z));
}

static void
sm__position_v2(v2 position)
{
	sm__position_v3(v3_new(position.x, position.y, RC.batch.current_depth));
}

static void
sm__position_2f(f32 x, f32 y)
{
	sm__position_v3(v3_new(x, y, RC.batch.current_depth));
}

static void
sm__uv_v2(v2 uv)
{
	glm_vec2_copy(uv.data, RC.state.uv.data);
}

static void
sm__uv_2f(f32 x, f32 y)
{
	RC.state.uv.x = x;
	RC.state.uv.y = y;
}

static void
sm__color_v4(v4 c)
{
	RC.state.c = color_from_v4(c);
}

static void
sm__color_hex(u32 c)
{
	RC.state.c = color_from_hex(c);
}

static void
sm__color_4ub(u8 r, u8 g, u8 b, u8 a)
{
	RC.state.c.r = r;
	RC.state.c.g = g;
	RC.state.c.b = b;
	RC.state.c.a = a;
}

static void
sm__color(color c)
{
	RC.state.c = c;
}

static void
sm__renderer_m4_load_identity(void)
{
	*RC.state.current_matrix = m4_identity();
}

static void
sm__renderer_m4_mul(mat4 mat)
{
	glm_mat4_mul(RC.state.current_matrix->data, mat, RC.state.current_matrix->data);
}

static void
sm__renderer_m4_mode(enum matrix_mode mode)
{
	if (mode == MATRIX_MODE_PROJECTION) RC.state.current_matrix = &RC.state.projection;
	else if (mode == MATRIX_MODE_MODEL_VIEW) RC.state.current_matrix = &RC.state.modelview;
	RC.state.matrix_mode = mode;
}

static void
sm__renderer_m4_pop(void)
{
	if (RC.state.stack_counter > 0)
	{
		m4 m = RC.state.stack[RC.state.stack_counter - 1];
		*RC.state.current_matrix = m;
		RC.state.stack_counter--;
	}
	if (RC.state.stack_counter == 0 && RC.state.matrix_mode == MATRIX_MODE_MODEL_VIEW)
	{
		RC.state.current_matrix = &RC.state.modelview;
		RC.state.transform_required = false;
	}
}

static void
sm__renderer_m4_push(void)
{
	if (RC.state.stack_counter >= 32)
	{
		log_error(str8_from("matrix stack overflow"));
		return;
	}
	if (RC.state.matrix_mode == MATRIX_MODE_MODEL_VIEW)
	{
		RC.state.transform_required = true;
		RC.state.current_matrix = &RC.state.transform;
	}
	RC.state.stack[RC.state.stack_counter] = *RC.state.current_matrix;
	RC.state.stack_counter++;
}

static m4
sm__renderer_get_camera2D_m4(struct camera2D *camera)
{
	m4 mat_transform = m4_identity();
	// The camera in world-space is set by
	//   1. Move it to target
	//   2. Rotate by -rotation and scale by (1/zoom)
	//      When setting higher scale, it's more intuitive for the world to become bigger (= camera become smaller),
	//      not for the camera getting bigger, hence the invert. Same deal with rotation.
	//   3. Move it by (-offset);
	//      Offset defines target transform relative to screen, but since we're effectively "moving" screen (camera)
	//      we need to do it into opposite direction (inverse transform)
	// Having camera transform in world-space, inverse of it gives the modelview transform.
	// Since (A*B*C)' = C'*B'*A', the modelview is
	//   1. Move to offset
	//   2. Rotate and Scale
	//   3. Move by -target
	// m4 matOrigin = MatrixTranslate(-camera.target.x, -camera.target.y, 0.0f);
	glm_translate(mat_transform.data, v3_new(-camera->target.x, -camera->target.y, 0.0f).data);
	// m4 matRotation = MatrixRotate((Vector3){0.0f, 0.0f, 1.0f}, camera.rotation * DEG2RAD);
	glm_rotate(mat_transform.data, glm_rad(camera->rotation), v3_new(0.0f, 0.0f, 1.0f).data);
	// m4 matScale = MatrixScale(camera.zoom, camera.zoom, 1.0f);
	glm_scale(mat_transform.data, v3_new(camera->zoom, camera->zoom, 1.0f).data);
	// m4 matTranslation = MatrixTranslate(camera.offset.x, camera.offset.y, 0.0f);
	glm_translate(mat_transform.data, v3_new(camera->offset.x, camera->offset.y, 0.0f).data);
	// mat_transform =
	//     MatrixMultiply(MatrixMultiply(matOrigin, MatrixMultiply(matScale, matRotation)), matTranslation);
	return mat_transform;
}

void
draw_begin(void)
{
	sm__renderer_m4_load_identity();
}

void
draw_end(void)
{
	sm__batch_draw();
}

void
draw_begin2D(struct camera2D *camera)
{
	sm__batch_draw(); // Update and draw internal render batch
	sm__renderer_m4_load_identity();
	m4 cam_mat = sm__renderer_get_camera2D_m4(camera);
	// m4_print(cam_mat);
	sm__renderer_m4_mul(cam_mat.data);
	// Apply screen scaling if required
}

// Ends 2D mode with custom camera
void
draw_end2D(void)
{
	sm__batch_draw();		 // Update and draw internal render batch
	sm__renderer_m4_load_identity(); // Reset current matrix (modelview)
	// rlMultMatrixf(MatrixToFloat(CORE.Window.screenScale)); // Apply screen scaling if required
}

void
upload_mesh(mesh_component *mesh)
{
	mesh_resource *m = mesh->mesh_ref;
	glCall(glGenVertexArrays(1, &m->vao));
	glCall(glBindVertexArray(m->vao));

	struct attributes
	{
		u32 idx;
		const i8 *n;
		void *ptr;
		u32 size;
		u32 comp;
	} ctable[4] = {
	    {.idx = 0, .n = "a_position", .ptr = m->positions, .size = array_len(m->positions) * sizeof(v3), .comp = 3},
	    {.idx = 1, .n = "a_uv",	    .ptr = m->uvs,	   .size = array_len(m->uvs) * sizeof(v2),	   .comp = 2},
	    {.idx = 2, .n = "a_color",    .ptr = m->colors,	 .size = array_len(m->colors) * sizeof(v4),    .comp = 4},
	    {.idx = 3, .n = "a_normal",	.ptr = m->normals,   .size = array_len(m->normals) * sizeof(v3),   .comp = 3},
	};

	u32 shader_program =
	    (m->skin_data.is_skinned) ? RC.state.default_program_skinned_3D : RC.state.default_program3D;

	for (u32 i = 0; i < ARRAY_SIZE(ctable); ++i)
	{
		// assert(ctable[i].ptr != 0);
		if (!ctable[i].ptr) continue;
		glCall(glGenBuffers(1, &m->vbos[ctable[i].idx]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->vbos[ctable[i].idx]));
		glCall(glBufferData(GL_ARRAY_BUFFER, ctable[i].size, ctable[i].ptr, GL_STATIC_DRAW));

		i32 a_loc = -1;
		glCall(a_loc = glGetAttribLocation(shader_program, ctable[i].n));
		assert(a_loc != -1);
		glCall(glVertexAttribPointer((u32)a_loc, (i32)ctable[i].comp, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_loc));
	}

	assert(m->indices);
	glCall(glGenBuffers(1, &m->ebo));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ebo));
	glCall(glBufferData(GL_ELEMENT_ARRAY_BUFFER, array_len(m->indices) * sizeof(u32), m->indices, GL_STATIC_DRAW));

	if (m->skin_data.is_skinned)
	{
		glCall(glGenBuffers(1, &m->skin_data.vbos[0]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->skin_data.vbos[0]));
		glCall(glBufferData(GL_ARRAY_BUFFER, array_len(m->skin_data.weights) * sizeof(v4), m->skin_data.weights,
		    GL_STATIC_DRAW));

		i32 a_weights_loc = -1;
		glCall(a_weights_loc = glGetAttribLocation(shader_program, "a_weights"));
		assert(a_weights_loc != -1);
		glCall(glVertexAttribPointer((u32)a_weights_loc, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_weights_loc));

		glCall(glGenBuffers(1, &m->skin_data.vbos[1]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->skin_data.vbos[1]));
		glCall(glBufferData(GL_ARRAY_BUFFER, array_len(m->skin_data.influences) * sizeof(iv4),
		    m->skin_data.influences, GL_STATIC_DRAW));

		i32 a_joints_loc = -1;
		glCall(a_joints_loc = glGetAttribLocation(shader_program, "a_joints"));
		assert(a_joints_loc != -1);
		glCall(glVertexAttribPointer((u32)a_joints_loc, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_joints_loc));
	}

	glCall(glBindVertexArray(0));

	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));

	if (m->vao > 0)
	{
		log_info(str8_from(" mesh uploaded successfully to VRAM"));
		return;
	}

	assert(0);
}

void
draw_mesh(camera_component *camera, mesh_component *mesh, material_component *material, m4 model)
{
	assert(mesh->resource_ref->type == RESOURCE_MESH);

	struct mesh_resource *mes = mesh->resource_ref->data;
	struct material_resource *mat = (material->resource_ref) ? material->resource_ref->material_data : 0;

	if (!mes) { return; }

	if (mes->vao == 0) { upload_mesh(mesh); }

	v4 diffuse_color = v4_one();
	b8 double_sided = false;
	u32 shader = mes->skin_data.is_skinned ? RC.state.default_program_skinned_3D : RC.state.default_program3D;
	u32 texture = RC.state.default_texture_ref.image_data->texture_handle;

	if (mat)
	{
		if (mat->color.hex != 0) { diffuse_color = color_to_v4(mat->color); }
		double_sided = mat->double_sided;
		if (mat->shader_handle) shader = mat->shader_handle;
		if (mat->image.size != 0)
		{
			struct resource *img_resource = resource_get_by_name(mat->image);
			assert(img_resource->type == RESOURCE_IMAGE);

			struct image_resource *img = img_resource->data;
			if (!img->texture_handle) { renderer_upload_texture(img); }
			texture = img->texture_handle;
			assert(texture != 0);
		}
	}

	glCall(glBindVertexArray(mes->vao));
	glCall(glUseProgram(shader));

	i32 texture_loc = -1;
	glCall(texture_loc = glGetUniformLocation(shader, "u_tex0"));
	glCall(glUniform1i(texture_loc, 0));
	glCall(glActiveTexture(GL_TEXTURE0 + 0));
	glCall(glBindTexture(GL_TEXTURE_2D, texture));

	i32 u_model_loc = -1;
	glCall(u_model_loc = glGetUniformLocation(shader, "u_model"));
	if (u_model_loc != -1) { glCall(glUniformMatrix4fv(u_model_loc, 1, false, model.float16)); }

	i32 u_inv_model_loc = -1;
	glCall(u_inv_model_loc = glGetUniformLocation(shader, "u_inverse_model"));
	if (u_inv_model_loc != -1)
	{
		m4 inv_model;
		glm_mat4_inv(model.data, inv_model.data);
		glCall(glUniformMatrix4fv(u_inv_model_loc, 1, false, inv_model.float16));
	}

	m4 view = camera_get_view(camera);
	m4 proj = camera_get_projection(camera);

	m4 view_projection_matrix;
	glm_mat4_mul(proj.data, view.data, view_projection_matrix.data);

	i32 u_pv = -1;
	glCall(u_pv = glGetUniformLocation(shader, "u_pv"));
	if (u_pv != -1) { glCall(glUniformMatrix4fv(u_pv, 1, false, view_projection_matrix.float16)); }

	i32 u_pvm = -1;
	glCall(u_pvm = glGetUniformLocation(shader, "u_pvm"));
	if (u_pvm != -1)
	{
		m4 mvp;
		glm_mat4_mul(view_projection_matrix.data, model.data, mvp.data);
		glCall(glUniformMatrix4fv(u_pvm, 1, false, mvp.float16));
	}

	i32 u_diffuse_color = -1;
	glCall(u_diffuse_color = glGetUniformLocation(shader, "u_diffuse_color"));
	if (u_diffuse_color != -1) { glCall(glUniform4fv(u_diffuse_color, 1, diffuse_color.data)); }

	if (mes->skin_data.is_skinned)
	{
		assert(shader == RC.state.default_program_skinned_3D);
		i32 u_animated = -1;
		glCall(u_animated = glGetUniformLocation(shader, "u_animated"));
		assert(u_animated != -1);
		if (u_animated != -1)
		{
			glCall(glUniformMatrix4fv(u_animated, (i32)array_len(mes->skin_data.pose_palette), GL_FALSE,
			    mes->skin_data.pose_palette->float16));
		}
	}

	if (double_sided) glCall(glDisable(GL_CULL_FACE));

	if (mes->indices) glCall(glDrawElements(GL_TRIANGLES, (i32)array_len(mes->indices), GL_UNSIGNED_INT, 0));
	else glCall(glDrawArrays(GL_TRIANGLES, 0, (i32)array_len(mes->positions)));

	if (double_sided) glCall(glEnable(GL_CULL_FACE));

	glCall(glBindTexture(GL_TEXTURE_2D, 0));

	glCall(glUseProgram(0));
	glCall(glBindVertexArray(0));
}

void
draw_rectangle(v2 position, v2 wh, u32 texture, color c)
{
	v2 origin = v2_zero();
	// f32 rotation = 0.0f;
	v2 top_left, top_right, bottom_left, bottom_right;
	// Only calculate rotation if needed
	// if (rotation == 0.0f)
	// {
	f32 x = position.x - origin.x;
	f32 y = position.y - origin.y;
	top_left = v2_new(x, y);
	top_right = v2_new(x + wh.x, y);
	bottom_left = v2_new(x, y + wh.y);
	bottom_right = v2_new(x + wh.x, y + wh.y);
	// }
	// else
	// {
	// float sinRotation = sinf(rotation * DEG2RAD);
	// float cosRotation = cosf(rotation * DEG2RAD);
	// float x = rec.x;
	// float y = rec.y;
	// float dx = -origin.x;
	// float dy = -origin.y;
	//
	// topLeft.x = x + dx * cosRotation - dy * sinRotation;
	// topLeft.y = y + dx * sinRotation + dy * cosRotation;
	//
	// topRight.x = x + (dx + rec.width) * cosRotation - dy * sinRotation;
	// topRight.y = y + (dx + rec.width) * sinRotation + dy * cosRotation;
	//
	// bottomLeft.x = x + dx * cosRotation - (dy + rec.height) *
	// sinRotation; bottomLeft.y = y + dx * sinRotation + (dy + rec.height)
	// * cosRotation;
	//
	// bottomRight.x = x + (dx + rec.width) * cosRotation - (dy +
	// rec.height) * sinRotation; bottomRight.y = y + (dx + rec.width) *
	// sinRotation + (dy + rec.height) * cosRotation;
	// }
	sm__batch_check_limit(4);

	sm__renderer_set_texture(texture ? texture : RC.state.default_texture_ref.image_data->texture_handle);
	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);

		sm__uv_v2(v2_new(0.0f, 0.0f));
		sm__position_v2(top_left);

		sm__uv_v2(v2_new(0.0f, 1.0f));
		sm__position_v2(bottom_left);

		sm__uv_v2(v2_new(1.0f, 1.0f));
		sm__position_v2(bottom_right);

		sm__uv_v2(v2_new(1.0f, 0.0f));
		sm__position_v2(top_right);
	}
	sm__renderer_end();
	sm__renderer_set_texture(0);
}

void
draw_rectangle_uv(v2 position, v2 wh, u32 texture, v2 uv[4], b8 vertical_flip, color c)
{
	v2 origin = v2_zero();
	// f32 rotation = 0.0f;
	u32 indices[4] = {0, 1, 2, 3};
	if (vertical_flip)
	{
		indices[0] = 3;
		indices[1] = 2;
		indices[2] = 1;
		indices[3] = 0;
	}
	v2 top_left, top_right, bottom_left, bottom_right;
	// Only calculate rotation if needed
	// if (rotation == 0.0f)
	// {
	f32 x = position.x - origin.x;
	f32 y = position.y - origin.y;
	top_left = v2_new(x, y);
	top_right = v2_new(x + wh.x, y);
	bottom_left = v2_new(x, y + wh.y);
	bottom_right = v2_new(x + wh.x, y + wh.y);
	// }
	// else
	// {
	// float sinRotation = sinf(rotation * DEG2RAD);
	// float cosRotation = cosf(rotation * DEG2RAD);
	// float x = rec.x;
	// float y = rec.y;
	// float dx = -origin.x;
	// float dy = -origin.y;
	//
	// topLeft.x = x + dx * cosRotation - dy * sinRotation;
	// topLeft.y = y + dx * sinRotation + dy * cosRotation;
	//
	// topRight.x = x + (dx + rec.width) * cosRotation - dy * sinRotation;
	// topRight.y = y + (dx + rec.width) * sinRotation + dy * cosRotation;
	//
	// bottomLeft.x = x + dx * cosRotation - (dy + rec.height) *
	// sinRotation; bottomLeft.y = y + dx * sinRotation + (dy + rec.height)
	// * cosRotation;
	//
	// bottomRight.x = x + (dx + rec.width) * cosRotation - (dy +
	// rec.height) * sinRotation; bottomRight.y = y + (dx + rec.width) *
	// sinRotation + (dy + rec.height) * cosRotation;
	// }
	sm__batch_check_limit(4);
	sm__renderer_set_texture(texture ? texture : RC.state.default_texture_ref.image_data->texture_handle);
	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);
		sm__uv_v2(uv[indices[0]]);
		sm__position_v2(top_left);
		sm__uv_v2(uv[indices[1]]);
		sm__position_v2(bottom_left);
		sm__uv_v2(uv[indices[2]]);
		sm__position_v2(bottom_right);
		sm__uv_v2(uv[indices[3]]);
		sm__position_v2(top_right);
	}
	sm__renderer_end();
	sm__renderer_set_texture(0);
}

void
draw_circle(v2 center, f32 radius, f32 start_angle, f32 end_angle, u32 segments, color c)
{
	if (radius <= 0.0f) radius = 0.1f; // Avoid div by zero
	// Function expects (endAngle > startAngle)
	if (end_angle < start_angle)
	{
		// Swap values
		float tmp = start_angle;
		start_angle = end_angle;
		end_angle = tmp;
	}
	u32 min_segments = (u32)ceilf((end_angle - start_angle) / 90.0f);
	if (segments < min_segments)
	{
		// Calculate the maximum angle between segments based on the
		// error rate (usually 0.5f)
		f32 th = acosf(2 * powf(1 - 0.5f / radius, 2) - 1);
		segments = (u32)((end_angle - start_angle) * ceilf(2 * GLM_PIf / th) / 360.0f);
		if (segments <= 0) segments = min_segments;
	}
	f32 step_length = (end_angle - start_angle) / (float)segments;
	f32 angle = start_angle;
	sm__batch_check_limit(4 * segments / 2);
	// TODO: Texture
	sm__renderer_set_texture(RC.state.default_texture_ref.image_data->texture_handle);
	sm__renderer_begin(GL_QUADS);
	{
		// NOTE: Every QUAD actually represents two segments
		for (u32 i = 0; i < segments / 2; i++)
		{
			sm__color(c);
			sm__position_v2(center);
			sm__position_v2(
			    v2_new(center.x + sinf(glm_rad(angle)) * radius, center.y + cosf(glm_rad(angle)) * radius));
			sm__position_v2(v2_new(center.x + sinf(glm_rad(angle + step_length)) * radius,
			    center.y + cosf(glm_rad(angle + step_length)) * radius));
			sm__position_v2(v2_new(center.x + sinf(glm_rad(angle + step_length * 2)) * radius,
			    center.y + cosf(glm_rad(angle + step_length * 2)) * radius));
			angle += (step_length * 2);
		}
		// NOTE: In case number of segments is odd, we add one last
		// piece to the cake
		if (segments % 2)
		{
			sm__color(c);
			sm__position_v2(v2_new(center.x, center.y));
			sm__position_v2(
			    v2_new(center.x + sinf(glm_rad(angle)) * radius, center.y + cosf(glm_rad(angle)) * radius));
			sm__position_v2(v2_new(center.x + sinf(glm_rad(angle + step_length)) * radius,
			    center.y + cosf(glm_rad(angle + step_length)) * radius));
			sm__position_v2(v2_new(center.x, center.y));
		}
	}
	sm__renderer_end();
	// TODO: Texture
	sm__renderer_set_texture(0);
}

void
draw_begin_3d(camera_component *camera)
{
	sm__batch_draw();

	sm__renderer_m4_mode(MATRIX_MODE_PROJECTION);
	sm__renderer_m4_push();
	sm__renderer_m4_load_identity();

#if 1
	m4 proj = camera_get_projection(camera);
	m4 view = camera_get_view(camera);
#else
	f32 top = 0.1f * tanf(glm_rad(camera->fov * 05));
	f32 right = top * camera->aspect_ratio;

	glm_frustum(-right, right, -top, top, 0.1f, 100.f, proj.data);
	glm_lookat(camera->position.data, camera->target.data, camera->up.data, view.data);
#endif

	sm__renderer_m4_mul(proj.data);

	sm__renderer_m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__renderer_m4_load_identity();

	sm__renderer_m4_mul(view.data);

	glCall(glEnable(GL_DEPTH_TEST));
}

void
draw_end_3d(void)
{
	sm__batch_draw(); // Update and draw internal render batch
	sm__renderer_m4_mode(MATRIX_MODE_PROJECTION);
	sm__renderer_m4_pop(); // Restore previous matrix (projection) from
			       // matrix stack
	sm__renderer_m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__renderer_m4_load_identity();
	glCall(glDisable(GL_DEPTH_TEST));
}

void
draw_grid(i32 slices, f32 spacing)
{
	i32 half_slices = slices / 2;
	sm__batch_check_limit((u32)(slices + 2) * 4);
	// TODO: textures
	sm__renderer_set_texture(RC.state.default_texture_ref.image_data->texture_handle);
	sm__renderer_begin(GL_LINES);
	for (int i = -half_slices; i <= half_slices; i++)
	{
		if (i == 0) { sm__color_v4(v4_new(0.5f, 0.5f, 0.5f, 1.0f)); }
		else { sm__color_v4(v4_new(0.75f, 0.75f, 0.75f, 1.0f)); }
		sm__position_v3(v3_new((float)i * spacing, 0.0f, (float)-half_slices * spacing));
		sm__position_v3(v3_new((float)i * spacing, 0.0f, (float)half_slices * spacing));
		sm__position_v3(v3_new((float)-half_slices * spacing, 0.0f, (float)i * spacing));
		sm__position_v3(v3_new((float)half_slices * spacing, 0.0f, (float)i * spacing));
	}
	sm__renderer_end();
	// TODO: texture
	sm__renderer_set_texture(0);
}

void
draw_sphere(v3 center_pos, f32 radius, u32 rings, u32 slices, color c)
{
	u32 num_vertex = (rings + 2) * slices * 6;
	sm__batch_check_limit(num_vertex);
	sm__renderer_m4_push();
	// NOTE: Transformation is applied in inverse order (scale -> translate)
	glm_translate(RC.state.current_matrix->data, center_pos.data);
	v3 scale;
	glm_vec3_fill(scale.data, radius);
	glm_scale(RC.state.current_matrix->data, scale.data);
	// TODO texture
	sm__renderer_set_texture(RC.state.default_texture_ref.image_data->texture_handle);
	sm__renderer_begin(GL_TRIANGLES);
	{
		sm__color(c);
		f32 frings = (f32)rings;
		f32 fslices = (f32)slices;
		for (f32 i = 0; i < (frings + 2); i++)
		{
			for (f32 j = 0; j < fslices; j++)
			{
				f32 v1_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * i)) *
					   sinf(glm_rad(360.0f * j / fslices));
				f32 v1_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * i));
				f32 v1_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * i)) *
					   cosf(glm_rad(360.0f * j / fslices));
				f32 v2_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
					   sinf(glm_rad(360.0f * (j + 1) / fslices));
				f32 v2_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1)));
				f32 v2_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
					   cosf(glm_rad(360.0f * (j + 1) / fslices));
				f32 v3_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
					   sinf(glm_rad(360.0f * j / fslices));
				f32 v3_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1)));
				f32 v3_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
					   cosf(glm_rad(360.0f * j / fslices));
				v3 v1 = v3_new(v1_x, v1_y, v1_z);
				v3 v2 = v3_new(v2_x, v2_y, v2_z);
				v3 v3 = v3_new(v3_x, v3_y, v3_z);
				sm__position_v3(v1);
				sm__position_v3(v2);
				sm__position_v3(v3);
				v1_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * i)) *
				       sinf(glm_rad(360.0f * j / fslices));
				v1_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * i));
				v1_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * i)) *
				       cosf(glm_rad(360.0f * j / fslices));
				v2_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i))) *
				       sinf(glm_rad(360.0f * (j + 1) / fslices));
				v2_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * (i)));
				v2_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i))) *
				       cosf(glm_rad(360.0f * (j + 1) / fslices));
				v3_x = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
				       sinf(glm_rad(360.0f * (j + 1) / fslices));
				v3_y = sinf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1)));
				v3_z = cosf(glm_rad(270 + (180.0f / (frings + 1)) * (i + 1))) *
				       cosf(glm_rad(360.0f * (j + 1) / fslices));
				v1 = v3_new(v1_x, v1_y, v1_z);
				v2 = v3_new(v2_x, v2_y, v2_z);
				v3 = v3_new(v3_x, v3_y, v3_z);
				sm__position_v3(v1);
				sm__position_v3(v2);
				sm__position_v3(v3);
			}
		}
	}
	sm__renderer_end();
	// TODO: texture
	sm__renderer_set_texture(0);
	sm__renderer_m4_pop();
}

void
sm_draw_line_3D(v3 start_pos, v3 end_pos, color c)
{
	// WARNING: Be careful with internal buffer vertex alignment
	// when using GL_LINES or GL_TRIANGLES, data is aligned to fit
	// lines-triangles-quads in the same indexed buffers!!!
	sm__batch_check_limit(8);
	sm__renderer_begin(GL_LINES);
	{
		sm__color(c);
		sm__position_v3(start_pos);
		sm__position_v3(end_pos);
	}
	sm__renderer_end();
}

void
sm_draw_cube_wires(v3 position, f32 width, f32 height, f32 length, color c)
{
	f32 x = 0.0f;
	f32 y = 0.0f;
	f32 z = 0.0f;
	sm__batch_check_limit(36);
	sm__renderer_m4_push();
	glm_translate(RC.state.current_matrix->data, position.data);
	sm__renderer_begin(GL_LINES);
	{
		sm__color(c);
		// Front face
		// ----------------------------------------------------- Bottom
		// line
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z + length / 2)); // Bottom left
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z + length / 2)); // Bottom right
		// Left line
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z + length / 2)); // Bottom right
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z + length / 2)); // Top right
		// Top line
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z + length / 2));							// Top right
		sm__position_v3(v3_new(x - width / 2, y + height / 2, z + length / 2)); // Top left
		// Right line
		sm__position_v3(v3_new(x - width / 2, y + height / 2, z + length / 2)); // Top left
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z + length / 2)); // Bottom left
		// Back face
		// ------------------------------------------------------ Bottom
		// line
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z - length / 2)); // Bottom left
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z - length / 2)); // Bottom right
		// Left line
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z - length / 2)); // Bottom right
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z - length / 2)); // Top right
		// Top line
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z - length / 2));							// Top right
		sm__position_v3(v3_new(x - width / 2, y + height / 2, z - length / 2)); // Top left
		// Right line
		sm__position_v3(v3_new(x - width / 2, y + height / 2, z - length / 2)); // Top left
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z - length / 2)); // Bottom left
		// Top face
		// ------------------------------------------------------- Left
		// line
		sm__position_v3(v3_new(x - width / 2, y + height / 2,
		    z + length / 2)); // Top left front
		sm__position_v3(v3_new(x - width / 2, y + height / 2,
		    z - length / 2)); // Top left back
		// Right line
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z + length / 2)); // Top right front
		sm__position_v3(v3_new(x + width / 2, y + height / 2,
		    z - length / 2)); // Top right back
		// Bottom face
		// --------------------------------------------------- Left line
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z + length / 2)); // Top left front
		sm__position_v3(v3_new(x - width / 2, y - height / 2,
		    z - length / 2)); // Top left back
		//_ Right line
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z + length / 2)); // Top right front
		sm__position_v3(v3_new(x + width / 2, y - height / 2,
		    z - length / 2)); // Top right back
	}
	sm__renderer_end();
	sm__renderer_m4_pop();
}

// Draw capsule wires with the center of its sphere caps at startPos and endPos
void
sm_draw_capsule_wires(v3 start_pos, v3 end_pos, f32 radius, u32 slices, u32 rings, color c)
{
	if (slices < 3) slices = 3;

	v3 dir;
	glm_vec3_sub(end_pos.data, start_pos.data, dir.data);

	// draw a sphere if start and end points are the same
	b8 sphere_case = (dir.x == 0) && (dir.y == 0) && (dir.z == 0);
	if (sphere_case) { dir = v3_new(0.0f, 1.0f, 0.0f); }

	// Construct a basis of the base and the caps:
	v3 b0;
	glm_vec3_normalize_to(dir.data, b0.data);

	v3 b1;
	glm_vec3_ortho(dir.data, b1.data);
	glm_vec3_normalize(b1.data);

	v3 b2;
	glm_vec3_cross(b1.data, dir.data, b2.data);
	glm_vec3_normalize(b2.data);

	v3 cap_center = end_pos;

	f32 base_slice_angle = (2.0f * M_PI) / slices;
	f32 base_ring_angle = M_PI * 0.5f / rings;

	sm__renderer_begin(GL_LINES);
	sm__color(c);

	// render both caps
	for (u32 b = 0; b < 2; b++)
	{
		for (u32 i = 0; i < rings; i++)
		{
			for (u32 j = 0; j < slices; j++)
			{
				// we build up the rings from capCenter in the direction of the 'direction' vector we
				// computed earlier

				// as we iterate through the rings they must be placed higher above the center, the
				// height we need is sin(angle(i)) as we iterate through the rings they must get smaller
				// by the cos(angle(i))

				// compute the four vertices
				f32 ring_sin1 = sinf(base_slice_angle * (j)) * cosf(base_ring_angle * (i + 0));
				f32 ring_cos1 = cosf(base_slice_angle * (j)) * cosf(base_ring_angle * (i + 0));

				f32 x_1 = cap_center.x + (sinf(base_ring_angle * (i + 0)) * b0.x + ring_sin1 * b1.x +
							     ring_cos1 * b2.x) *
							     radius;
				f32 y_1 = cap_center.y + (sinf(base_ring_angle * (i + 0)) * b0.y + ring_sin1 * b1.y +
							     ring_cos1 * b2.y) *
							     radius;
				f32 z_1 = cap_center.z + (sinf(base_ring_angle * (i + 0)) * b0.z + ring_sin1 * b1.z +
							     ring_cos1 * b2.z) *
							     radius;
				v3 w1 = v3_new(x_1, y_1, z_1);

				f32 ringSin2 = sinf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 0));
				f32 ringCos2 = cosf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 0));

				f32 x_2 = cap_center.x +
					  (sinf(base_ring_angle * (i + 0)) * b0.x + ringSin2 * b1.x + ringCos2 * b2.x) *
					      radius;
				f32 y_2 = cap_center.y +
					  (sinf(base_ring_angle * (i + 0)) * b0.y + ringSin2 * b1.y + ringCos2 * b2.y) *
					      radius;
				f32 z_2 = cap_center.z +
					  (sinf(base_ring_angle * (i + 0)) * b0.z + ringSin2 * b1.z + ringCos2 * b2.z) *
					      radius;
				v3 w2 = v3_new(x_2, y_2, z_2);

				f32 ring_sin3 = sinf(base_slice_angle * (j + 0)) * cosf(base_ring_angle * (i + 1));
				f32 ring_cos3 = cosf(base_slice_angle * (j + 0)) * cosf(base_ring_angle * (i + 1));

				f32 x_3 = cap_center.x + (sinf(base_ring_angle * (i + 1)) * b0.x + ring_sin3 * b1.x +
							     ring_cos3 * b2.x) *
							     radius;
				f32 y_3 = cap_center.y + (sinf(base_ring_angle * (i + 1)) * b0.y + ring_sin3 * b1.y +
							     ring_cos3 * b2.y) *
							     radius;
				f32 z_3 = cap_center.z + (sinf(base_ring_angle * (i + 1)) * b0.z + ring_sin3 * b1.z +
							     ring_cos3 * b2.z) *
							     radius;
				v3 w3 = v3_new(x_3, y_3, z_3);

				f32 ring_sin4 = sinf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 1));
				f32 ring_cos4 = cosf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 1));

				f32 x_4 = cap_center.x + (sinf(base_ring_angle * (i + 1)) * b0.x + ring_sin4 * b1.x +
							     ring_cos4 * b2.x) *
							     radius;
				f32 y_4 = cap_center.y + (sinf(base_ring_angle * (i + 1)) * b0.y + ring_sin4 * b1.y +
							     ring_cos4 * b2.y) *
							     radius;
				f32 z_4 = cap_center.z + (sinf(base_ring_angle * (i + 1)) * b0.z + ring_sin4 * b1.z +
							     ring_cos4 * b2.z) *
							     radius;
				v3 w4 = v3_new(x_4, y_4, z_4);

				sm__position_v3(w1);
				sm__position_v3(w2);

				sm__position_v3(w2);
				sm__position_v3(w4);
				// sm__position_v3(w2);
				// sm__position_v3(w3);

				sm__position_v3(w1);
				sm__position_v3(w3);

				// sm__position_v3(w3);
				// sm__position_v3(w4);
			}
		}
		cap_center = start_pos;
		glm_vec3_scale(b0.data, -1.0f, b0.data);
	}
	// render middle
	if (!sphere_case)
	{
		for (u32 j = 0; j < slices; j++)
		{
			// compute the four vertices
			f32 ring_sin1 = sinf(base_slice_angle * (j + 0)) * radius;
			f32 ring_cos1 = cosf(base_slice_angle * (j + 0)) * radius;

			f32 x_1 = start_pos.x + ring_sin1 * b1.x + ring_cos1 * b2.x;
			f32 y_1 = start_pos.y + ring_sin1 * b1.y + ring_cos1 * b2.y;
			f32 z_1 = start_pos.z + ring_sin1 * b1.z + ring_cos1 * b2.z;
			v3 w1 = v3_new(x_1, y_1, z_1);

			f32 ring_sin2 = sinf(base_slice_angle * (j + 1)) * radius;
			f32 ring_cos2 = cosf(base_slice_angle * (j + 1)) * radius;

			f32 x_2 = start_pos.x + ring_sin2 * b1.x + ring_cos2 * b2.x;
			f32 y_2 = start_pos.y + ring_sin2 * b1.y + ring_cos2 * b2.y;
			f32 z_2 = start_pos.z + ring_sin2 * b1.z + ring_cos2 * b2.z;
			v3 w2 = v3_new(x_2, y_2, z_2);

			f32 ring_sin3 = sinf(base_slice_angle * (j + 0)) * radius;
			f32 ring_cos3 = cosf(base_slice_angle * (j + 0)) * radius;

			f32 x_3 = end_pos.x + ring_sin3 * b1.x + ring_cos3 * b2.x;
			f32 y_3 = end_pos.y + ring_sin3 * b1.y + ring_cos3 * b2.y;
			f32 z_3 = end_pos.z + ring_sin3 * b1.z + ring_cos3 * b2.z;
			v3 w3 = v3_new(x_3, y_3, z_3);

			f32 ring_sin4 = sinf(base_slice_angle * (j + 1)) * radius;
			f32 ring_cos4 = cosf(base_slice_angle * (j + 1)) * radius;

			f32 x_4 = end_pos.x + ring_sin4 * b1.x + ring_cos4 * b2.x;
			f32 y_4 = end_pos.y + ring_sin4 * b1.y + ring_cos4 * b2.y;
			f32 z_4 = end_pos.z + ring_sin4 * b1.z + ring_cos4 * b2.z;
			v3 w4 = v3_new(x_4, y_4, z_4);

			sm__position_v3(w1);
			sm__position_v3(w3);

			sm__position_v3(w2);
			sm__position_v3(w4);

			// sm__position_v3(w2);
			// sm__position_v3(w3);
		}
	}
	sm__renderer_end();
}

void
renderer_on_resize(u32 width, u32 height)
{
	glCall(glScissor(0, 0, (i32)width, (i32)height));
	glCall(glViewport(0, 0, (i32)width, (i32)height));

	m4 ortho;
	sm__renderer_m4_mode(MATRIX_MODE_PROJECTION);
	sm__renderer_m4_load_identity();
	glm_ortho(0, (f32)width, (f32)height, 0, -1.0f, 1.0f, ortho.data);
	sm__renderer_m4_mul(ortho.data);

	sm__renderer_m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__renderer_m4_load_identity();
}
