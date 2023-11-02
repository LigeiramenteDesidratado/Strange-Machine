#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smResource.h"
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

#ifdef CGLM_FORCE_DEPTH_ZERO_TO_ONE
#	define RESET_DEPTH_VALUE 0.0f
#else
#	define RESET_DEPTH_VALUE -1.0f
#endif

struct pools
{
	struct handle_pool buffer_pool;
	struct renderer_buffer *buffers;

	struct handle_pool texture_pool;
	struct renderer_texture *textures;

	struct handle_pool sampler_pool;
	struct renderer_sampler *samplers;

	struct handle_pool shader_pool;
	struct renderer_shader *shaders;

	struct handle_pool pipeline_pool;
	struct renderer_pipeline *pipelines;

	struct handle_pool pass_pool;
	struct renderer_pass *passes;
};

static void
sm__renderer_pools_make(struct arena *arena, struct pools *pools)
{
	sm__assert(pools);
	sm__assert(arena);

#define RENDERER_INITIAL_CAPACITY_BUFFERS   64
#define RENDERER_INITIAL_CAPACITY_TEXTURES  64
#define RENDERER_INITIAL_CAPACITY_SAMPLERS  64
#define RENDERER_INITIAL_CAPACITY_SHADERS   64
#define RENDERER_INITIAL_CAPACITY_PIPELINES 64
#define RENDERER_INITIAL_CAPACITY_PASSES    64

	handle_pool_make(arena, &pools->buffer_pool, RENDERER_INITIAL_CAPACITY_BUFFERS);
	pools->buffers = arena_reserve(arena, sizeof(struct renderer_buffer) * RENDERER_INITIAL_CAPACITY_BUFFERS);

	handle_pool_make(arena, &pools->texture_pool, RENDERER_INITIAL_CAPACITY_TEXTURES);
	pools->textures = arena_reserve(arena, sizeof(struct renderer_texture) * RENDERER_INITIAL_CAPACITY_TEXTURES);

	handle_pool_make(arena, &pools->sampler_pool, RENDERER_INITIAL_CAPACITY_SAMPLERS);
	pools->samplers = arena_reserve(arena, sizeof(struct renderer_sampler) * RENDERER_INITIAL_CAPACITY_SAMPLERS);

	handle_pool_make(arena, &pools->shader_pool, RENDERER_INITIAL_CAPACITY_SHADERS);
	pools->shaders = arena_reserve(arena, sizeof(struct renderer_shader) * RENDERER_INITIAL_CAPACITY_SHADERS);

	handle_pool_make(arena, &pools->pipeline_pool, RENDERER_INITIAL_CAPACITY_PIPELINES);
	pools->pipelines = arena_reserve(arena, sizeof(struct renderer_pipeline) * RENDERER_INITIAL_CAPACITY_PIPELINES);

	handle_pool_make(arena, &pools->pass_pool, RENDERER_INITIAL_CAPACITY_PASSES);
	pools->passes = arena_reserve(arena, sizeof(struct renderer_pass) * RENDERER_INITIAL_CAPACITY_PASSES);

#undef RENDERER_INITIAL_CAPACITY_BUFFERS
#undef RENDERER_INITIAL_CAPACITY_TEXTURES
#undef RENDERER_INITIAL_CAPACITY_SAMPLER
#undef RENDERER_INITIAL_CAPACITY_SHADERS
#undef RENDERER_INITIAL_CAPACITY_PIPELINE
#undef RENDERER_INITIAL_CAPACITY_PASSES
}

static void
sm__renderer_pools_release(struct arena *arena, struct pools *pools)
{
	arena_free(arena, pools->passes);
	handle_pool_release(arena, &pools->pass_pool);

	arena_free(arena, pools->pipelines);
	handle_pool_release(arena, &pools->pipeline_pool);

	arena_free(arena, pools->shaders);
	handle_pool_release(arena, &pools->shader_pool);

	arena_free(arena, pools->samplers);
	handle_pool_release(arena, &pools->sampler_pool);

	arena_free(arena, pools->textures);
	handle_pool_release(arena, &pools->texture_pool);

	arena_free(arena, pools->buffers);
	handle_pool_release(arena, &pools->buffer_pool);
}

struct renderer
{
	// batch being used for rendering
	struct arena arena;

	struct pools pools;

	u32 width, height;

	struct
	{
		shader_handle shader;

		pass_handle pass;
		u32 pass_width;
		u32 pass_height;
		b32 in_pass;

		enum store_action store_action_color[MAX_COLOR_ATTACHMENTS];
		enum store_action store_action_depth;

		pipeline_handle pipeline;

		struct
		{
			u32 default_vao;
			u32 default_vbo;
			u32 default_fbo;

			GLuint bind_vertex_buffer, store_bind_vertex_buffer;
			GLuint bind_index_buffer, store_bind_index_buffer;

			struct blend_state blend;
			struct depth_state depth;
			struct rasterizer_state rasterizer;
			struct texture_slot textures[MAX_TEXTURE_SLOTS];
			struct buffer_slot buffers[MAX_BUFFER_SLOTS];
			buffer_handle index_buffer;
		} gl;
	} current;
};

static struct renderer RC;

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

static b32
gl__log_call(str8 file, u32 line)
{
	GLenum err;
	while ((err = glGetError()))
	{
		log__log(LOG_ERRO, file, line, str8_from("[GL Error] ({u3d}): {s}"), err, gl__error_to_string(err));
		return (0);
	}
	return (1);
}

#define glCall(CALL)                                                                       \
	do                                                                                 \
	{                                                                                  \
		gl__log_call(str8_from(sm__file_name), sm__file_line);                     \
		CALL;                                                                      \
		sm__assertf(gl__log_call(str8_from(sm__file_name), sm__file_line), #CALL); \
	} while (0)

static str8
gl__type_to_string(GLenum type)
{
	switch (type)
	{
	case GL_BOOL: return str8_from("GL_BOOL");
	case GL_INT: return str8_from("GL_INT");
	case GL_FLOAT: return str8_from("GL_FLOAT");

	case GL_FLOAT_VEC2: return str8_from("GL_FLOAT_VEC2");
	case GL_FLOAT_VEC3: return str8_from("GL_FLOAT_VEC3");
	case GL_FLOAT_VEC4: return str8_from("GL_FLOAT_VEC4");

	case GL_BOOL_VEC2: return str8_from("GL_BOOL_VEC2");
	case GL_BOOL_VEC3: return str8_from("GL_BOOL_VEC3");
	case GL_BOOL_VEC4: return str8_from("GL_BOOL_VEC4");

	case GL_INT_VEC2: return str8_from("GL_INT_VEC2");
	case GL_INT_VEC3: return str8_from("GL_INT_VEC3");
	case GL_INT_VEC4: return str8_from("GL_INT_VEC4");

	case GL_FLOAT_MAT2: return str8_from("GL_FLOAT_MAT2");
	case GL_FLOAT_MAT3: return str8_from("GL_FLOAT_MAT3");
	case GL_FLOAT_MAT4: return str8_from("GL_FLOAT_MAT4");

	case GL_FLOAT_MAT2x3: return str8_from("GL_FLOAT_MAT2x3");
	case GL_FLOAT_MAT2x4: return str8_from("GL_FLOAT_MAT2x4");
	case GL_FLOAT_MAT3x2: return str8_from("GL_FLOAT_MAT3x2");
	case GL_FLOAT_MAT3x4: return str8_from("GL_FLOAT_MAT3x4");
	case GL_FLOAT_MAT4x2: return str8_from("GL_FLOAT_MAT4x2");
	case GL_FLOAT_MAT4x3: return str8_from("GL_FLOAT_MAT4x3");

	case GL_SAMPLER_1D: return str8_from("GL_SAMPLER_1D");
	case GL_SAMPLER_2D: return str8_from("GL_SAMPLER_2D");
	case GL_SAMPLER_3D: return str8_from("GL_SAMPLER_3D");
	case GL_SAMPLER_CUBE: return str8_from("GL_SAMPLER_CUBE");
	case GL_SAMPLER_1D_SHADOW: return str8_from("GL_SAMPLER_1D_SHADOW");
	case GL_SAMPLER_2D_SHADOW: return str8_from("GL_SAMPLER_2D_SHADOW");

	default: return str8_from("UNKNOWN");
	}
}

static enum shader_type
gl__to_shader_type(GLenum type)
{
	enum shader_type result;

	switch (type)
	{
	case GL_BOOL: result = SHADER_TYPE_B8; break;
	case GL_INT: result = SHADER_TYPE_I32; break;
	case GL_FLOAT: result = SHADER_TYPE_F32; break;

	case GL_FLOAT_VEC2: result = SHADER_TYPE_V2; break;
	case GL_FLOAT_VEC3: result = SHADER_TYPE_V3; break;
	case GL_FLOAT_VEC4: result = SHADER_TYPE_V4; break;

	case GL_BOOL_VEC2: result = SHADER_TYPE_BV2; break;
	case GL_BOOL_VEC3: result = SHADER_TYPE_BV3; break;
	case GL_BOOL_VEC4: result = SHADER_TYPE_BV4; break;

	case GL_INT_VEC2: result = SHADER_TYPE_IV2; break;
	case GL_INT_VEC3: result = SHADER_TYPE_IV3; break;
	case GL_INT_VEC4: result = SHADER_TYPE_IV4; break;

	case GL_FLOAT_MAT2: result = SHADER_TYPE_M2; break;
	case GL_FLOAT_MAT2x3: result = SHADER_TYPE_M2X3; break;
	case GL_FLOAT_MAT2x4: result = SHADER_TYPE_M2X4; break;

	case GL_FLOAT_MAT3x2: result = SHADER_TYPE_M3X2; break;
	case GL_FLOAT_MAT3: result = SHADER_TYPE_M3; break;
	case GL_FLOAT_MAT3x4: result = SHADER_TYPE_M3X4; break;

	case GL_FLOAT_MAT4x2: result = SHADER_TYPE_M4X2; break;
	case GL_FLOAT_MAT4x3: result = SHADER_TYPE_M4X3; break;
	case GL_FLOAT_MAT4: result = SHADER_TYPE_M4; break;

	case GL_SAMPLER_1D: result = SHADER_TYPE_SAMPLER_1D; break;
	case GL_SAMPLER_2D: result = SHADER_TYPE_SAMPLER_2D; break;
	case GL_SAMPLER_3D: result = SHADER_TYPE_SAMPLER_3D; break;

	case GL_SAMPLER_CUBE: result = SHADER_TYPE_SAMPLER_CUBE; break;
	case GL_SAMPLER_1D_SHADOW: result = SHADER_TYPE_SAMPLER_1D_SHADOW; break;
	case GL_SAMPLER_2D_SHADOW: result = SHADER_TYPE_SAMPLER_2D_SHADOW; break;
	}

	return (result);
}

static u32 gl__ctable_type_size[] = {
    [SHADER_TYPE_B8] = sizeof(b8),
    [SHADER_TYPE_I32] = sizeof(i32),
    [SHADER_TYPE_F32] = sizeof(f32),

    [SHADER_TYPE_V2] = sizeof(v2),
    [SHADER_TYPE_V3] = sizeof(v3),
    [SHADER_TYPE_V4] = sizeof(v4),

    [SHADER_TYPE_BV2] = sizeof(bv2),
    [SHADER_TYPE_BV3] = sizeof(bv3),
    [SHADER_TYPE_BV4] = sizeof(bv4),

    [SHADER_TYPE_IV2] = sizeof(iv2),
    [SHADER_TYPE_IV3] = sizeof(iv3),
    [SHADER_TYPE_IV4] = sizeof(iv4),

    [SHADER_TYPE_M2] = 0,   // TODO
    [SHADER_TYPE_M2X3] = 0, // TODO
    [SHADER_TYPE_M2X4] = 0, // TODO

    [SHADER_TYPE_M3X2] = 0, // TODO
    [SHADER_TYPE_M3] = 0,   // TODO
    [SHADER_TYPE_M3X4] = 0, // TODO

    [SHADER_TYPE_M4X2] = 0, // TODO
    [SHADER_TYPE_M4X3] = 0, // TODO
    [SHADER_TYPE_M4] = sizeof(m4),

    [SHADER_TYPE_SAMPLER_1D] = 0, // TODO
    [SHADER_TYPE_SAMPLER_2D] = 0, // TODO
    [SHADER_TYPE_SAMPLER_3D] = 0, // TODO

    [SHADER_TYPE_SAMPLER_CUBE] = 0,	 // TODO
    [SHADER_TYPE_SAMPLER_1D_SHADOW] = 0, // TODO
    [SHADER_TYPE_SAMPLER_2D_SHADOW] = 0, // TODO
};

static str8 sm__ctable_shader_type_str8[] = {
    [SHADER_TYPE_B8] = str8_from("SHADER_TYPE_B8"),
    [SHADER_TYPE_I32] = str8_from("SHADER_TYPE_I32"),
    [SHADER_TYPE_F32] = str8_from("SHADER_TYPE_F32"),

    [SHADER_TYPE_V2] = str8_from("SHADER_TYPE_V2"),
    [SHADER_TYPE_V3] = str8_from("SHADER_TYPE_V3"),
    [SHADER_TYPE_V4] = str8_from("SHADER_TYPE_V4"),

    [SHADER_TYPE_BV2] = str8_from("SHADER_TYPE_BV2"),
    [SHADER_TYPE_BV3] = str8_from("SHADER_TYPE_BV3"),
    [SHADER_TYPE_BV4] = str8_from("SHADER_TYPE_BV4"),

    [SHADER_TYPE_IV2] = str8_from("SHADER_TYPE_IV2"),
    [SHADER_TYPE_IV3] = str8_from("SHADER_TYPE_IV3"),
    [SHADER_TYPE_IV4] = str8_from("SHADER_TYPE_IV4"),

    [SHADER_TYPE_M2] = str8_from("SHADER_TYPE_M2"),
    [SHADER_TYPE_M2X3] = str8_from("SHADER_TYPE_M2X3"),
    [SHADER_TYPE_M2X4] = str8_from("SHADER_TYPE_M2X4"),

    [SHADER_TYPE_M3X2] = str8_from("SHADER_TYPE_M3X2"),
    [SHADER_TYPE_M3] = str8_from("SHADER_TYPE_M3"),
    [SHADER_TYPE_M3X4] = str8_from("SHADER_TYPE_M3X4"),

    [SHADER_TYPE_M4X2] = str8_from("SHADER_TYPE_M4X2"),
    [SHADER_TYPE_M4X3] = str8_from("SHADER_TYPE_M4X3"),
    [SHADER_TYPE_M4] = str8_from("SHADER_TYPE_M4"),

    [SHADER_TYPE_SAMPLER_1D] = str8_from("SHADER_TYPE_SAMPLER_1D"),
    [SHADER_TYPE_SAMPLER_2D] = str8_from("SHADER_TYPE_SAMPLER_2D"),
    [SHADER_TYPE_SAMPLER_3D] = str8_from("SHADER_TYPE_SAMPLER_3D"),

    [SHADER_TYPE_SAMPLER_CUBE] = str8_from("SHADER_TYPE_SAMPLER_CUBE"),
    [SHADER_TYPE_SAMPLER_1D_SHADOW] = str8_from("SHADER_TYPE_SAMPLER_1D_SHADOW"),
    [SHADER_TYPE_SAMPLER_2D_SHADOW] = str8_from("SHADER_TYPE_SAMPLER_2D_SHADOW"),
};

static GLenum
gl__ctable_vertex_format_type(enum vertex_format fmt)
{
	switch (fmt)
	{
	case VERTEX_FORMAT_FLOAT:
	case VERTEX_FORMAT_FLOAT2:
	case VERTEX_FORMAT_FLOAT3:
	case VERTEX_FORMAT_FLOAT4: return GL_FLOAT;
	case VERTEX_FORMAT_BYTE4:
	case VERTEX_FORMAT_BYTE4N: return GL_BYTE;
	case VERTEX_FORMAT_UBYTE4:
	case VERTEX_FORMAT_UBYTE4N: return GL_UNSIGNED_BYTE;
	case VERTEX_FORMAT_SHORT2:
	case VERTEX_FORMAT_SHORT2N:
	case VERTEX_FORMAT_SHORT4:
	case VERTEX_FORMAT_SHORT4N: return GL_SHORT;
	case VERTEX_FORMAT_USHORT2N:
	case VERTEX_FORMAT_USHORT4N: return GL_UNSIGNED_SHORT;
	case VERTEX_FORMAT_UINT10_N2: return GL_UNSIGNED_INT_2_10_10_10_REV;
	// case VERTEX_FORMAT_HALF2:
	// case VERTEX_FORMAT_HALF4: return GL_HALF_FLOAT;
	default: sm__unreachable(); return 0;
	}
}

static GLboolean
gl__vertex_format_normalized(enum vertex_format fmt)
{
	switch (fmt)
	{
	case VERTEX_FORMAT_BYTE4N:
	case VERTEX_FORMAT_UBYTE4N:
	case VERTEX_FORMAT_SHORT2N:
	case VERTEX_FORMAT_USHORT2N:
	case VERTEX_FORMAT_SHORT4N:
	case VERTEX_FORMAT_USHORT4N:
	case VERTEX_FORMAT_UINT10_N2: return GL_TRUE;
	default: return GL_FALSE;
	}
}

static u32 gl__ctable_vertex_format_to_byte_size[VERTEX_FORMAT_MAX] = {
    [VERTEX_FORMAT_INVALID] = 0,
    [VERTEX_FORMAT_FLOAT] = 4,
    [VERTEX_FORMAT_FLOAT2] = 8,
    [VERTEX_FORMAT_FLOAT3] = 12,
    [VERTEX_FORMAT_FLOAT4] = 16,
    [VERTEX_FORMAT_BYTE4] = 4,
    [VERTEX_FORMAT_BYTE4N] = 4,
    [VERTEX_FORMAT_UBYTE4] = 4,
    [VERTEX_FORMAT_UBYTE4N] = 4,
    [VERTEX_FORMAT_SHORT2] = 4,
    [VERTEX_FORMAT_SHORT2N] = 4,
    [VERTEX_FORMAT_USHORT2N] = 4,
    [VERTEX_FORMAT_SHORT4] = 8,
    [VERTEX_FORMAT_SHORT4N] = 8,
    [VERTEX_FORMAT_USHORT4N] = 8,
    [VERTEX_FORMAT_UINT10_N2] = 4,
    [VERTEX_FORMAT_HALF2] = 4,
    [VERTEX_FORMAT_HALF4] = 8,
};

GLint gl__ctable_vertex_format_components_count[VERTEX_FORMAT_MAX] = {
    [VERTEX_FORMAT_FLOAT] = 1,
    [VERTEX_FORMAT_FLOAT2] = 2,
    [VERTEX_FORMAT_FLOAT3] = 3,
    [VERTEX_FORMAT_FLOAT4] = 4,
    [VERTEX_FORMAT_BYTE4] = 4,
    [VERTEX_FORMAT_BYTE4N] = 4,
    [VERTEX_FORMAT_UBYTE4] = 4,
    [VERTEX_FORMAT_UBYTE4N] = 4,
    [VERTEX_FORMAT_SHORT2] = 2,
    [VERTEX_FORMAT_SHORT2N] = 2,
    [VERTEX_FORMAT_USHORT2N] = 2,
    [VERTEX_FORMAT_SHORT4] = 4,
    [VERTEX_FORMAT_SHORT4N] = 4,
    [VERTEX_FORMAT_USHORT4N] = 4,
    [VERTEX_FORMAT_UINT10_N2] = 4,
    [VERTEX_FORMAT_HALF2] = 2,
    [VERTEX_FORMAT_HALF4] = 4,
};

static void
gl__renderer_set_uniform(i32 location, u32 size, enum shader_type type, void *value)
{
	switch (type)
	{
	case SHADER_TYPE_B8:
	case SHADER_TYPE_I32: glCall(glUniform1iv(location, size, value)); break;
	case SHADER_TYPE_F32: glCall(glUniform1fv(location, size, value)); break;

	case SHADER_TYPE_V2: glCall(glUniform2fv(location, size, value)); break;
	case SHADER_TYPE_V3: glCall(glUniform3fv(location, size, value)); break;
	case SHADER_TYPE_V4: glCall(glUniform4fv(location, size, value)); break;

	case SHADER_TYPE_BV2:
	case SHADER_TYPE_IV2: glCall(glUniform2iv(location, size, value)); break;
	case SHADER_TYPE_BV3:
	case SHADER_TYPE_IV3: glCall(glUniform3iv(location, size, value)); break;
	case SHADER_TYPE_BV4:
	case SHADER_TYPE_IV4: glCall(glUniform4iv(location, size, value)); break;

	case SHADER_TYPE_M2: glCall(glUniformMatrix2fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M2X3: glCall(glUniformMatrix2x3fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M2X4: glCall(glUniformMatrix2x4fv(location, size, GL_FALSE, value)); break;

	case SHADER_TYPE_M3X2: glCall(glUniformMatrix3x2fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M3: glCall(glUniformMatrix3fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M3X4: glCall(glUniformMatrix3x4fv(location, size, GL_FALSE, value)); break;

	case SHADER_TYPE_M4X2: glCall(glUniformMatrix4x2fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M4X3: glCall(glUniformMatrix4x3fv(location, size, GL_FALSE, value)); break;
	case SHADER_TYPE_M4: glCall(glUniformMatrix4fv(location, size, GL_FALSE, value)); break;

	case SHADER_TYPE_SAMPLER_1D:
	case SHADER_TYPE_SAMPLER_2D:
	case SHADER_TYPE_SAMPLER_3D:

	case SHADER_TYPE_SAMPLER_CUBE:
	case SHADER_TYPE_SAMPLER_1D_SHADOW:
	case SHADER_TYPE_SAMPLER_2D_SHADOW: glCall(glUniform1iv(location, size, value)); break;
	default: break;
	}
}

static void
gl__print_extensions(void)
{
	const u8 *extensions = glGetString(GL_EXTENSIONS);
	if (!extensions)
	{
		printf("Error: Unable to get OpenGL extensions.\n");
	}
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

static GLuint
gl__renderer_shader_compile_vert(const str8 vertex)
{
	GLuint result;
	glCall(result = glCreateShader(GL_VERTEX_SHADER));
	const i8 *v_source = (i8 *)vertex.data;
	glCall(glShaderSource(result, 1, &v_source, NULL));
	glCall(glCompileShader(result));
	GLint success = 0;
	glCall(glGetShaderiv(result, GL_COMPILE_STATUS, &success));
	if (success != GL_TRUE)
	{
		i8 info_log[2 * 512];
		glCall(glGetShaderInfoLog(result, 2 * 512, NULL, info_log));
		log_error(str8_from("vertex compilation failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(result));
		return 0;
	}

	return (result);
}

static GLuint
gl__renderer_shader_compile_frag(const str8 fragment)
{
	GLuint result;
	glCall(result = glCreateShader(GL_FRAGMENT_SHADER));
	const char *f_source = (i8 *)fragment.data;
	glCall(glShaderSource(result, 1, &f_source, NULL));
	glCall(glCompileShader(result));
	GLint success = 0;
	glCall(glGetShaderiv(result, GL_COMPILE_STATUS, &success));
	if (!success)
	{
		char info_log[2 * 512];
		glCall(glGetShaderInfoLog(result, 2 * 512, NULL, info_log));
		log_error(str8_from("fragment compilation failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(result));
		return 0;
	}
	return (result);
}

static b32
gl__renderer_shader_link(struct renderer_shader *shader)
{
	glCall(glAttachShader(shader->gl_shader_program_handle, shader->gl_vs_handle));
	glCall(glAttachShader(shader->gl_shader_program_handle, shader->gl_fs_handle));
	glCall(glLinkProgram(shader->gl_shader_program_handle));

	GLint success = 0;
	glCall(glGetProgramiv(shader->gl_shader_program_handle, GL_LINK_STATUS, &success));
	if (!success)
	{
		i8 info_log[2 * 512];
		glCall(glGetShaderInfoLog(shader->gl_shader_program_handle, 2 * 512, NULL, info_log));
		log_error(str8_from("shader linking failed.\n\t{s}"), str8_from_cstr_stack(info_log));

		return (0);
	}
	log_info(str8_from("[{s}] compiled and linked shaders successfully"), shader->label);

	return (1);
}

static void
gl__renderer_pipeline_set_blend_mode(enum blend_mode mode)
{
	switch (mode)
	{
	case BLEND_MODE_ALPHA:
		{
			glCall(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
			glCall(glBlendEquation(GL_FUNC_ADD));
		}
		break;
	case BLEND_MODE_ADDITIVE:
		{
			glCall(glBlendFunc(GL_SRC_ALPHA, GL_ONE));
			glCall(glBlendEquation(GL_FUNC_ADD));
		}
		break;
	case BLEND_MODE_MULTIPLIED:
		{
			glCall(glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA));
			glCall(glBlendEquation(GL_FUNC_ADD));
		}
		break;
	case BLEND_MODE_ADD_COLORS:
		{
			glCall(glBlendFunc(GL_ONE, GL_ONE));
			glCall(glBlendEquation(GL_FUNC_ADD));
		}
		break;
	case BLEND_MODE_SUBTRACT_COLORS:
		{
			glCall(glBlendFunc(GL_ONE, GL_ONE));
			glCall(glBlendEquation(GL_FUNC_SUBTRACT));
		}
		break;
	case BLEND_MODE_ALPHA_PREMULTIPLY:
		{
			glCall(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
			glCall(glBlendEquation(GL_FUNC_ADD));
		}
		break;
	case BLEND_MODE_CUSTOM:
		{
			// NOTE: Using GL blend src/dst factors and GL equation configured with
			// rlSetBlendFactors()

			// sm_gl_blend_func(RLGL.state.gl_blend_src_factor,RLGL.state.gl_blend_dst_factor);
			// sm_gl_blend_equation(RLGL.state.gl_blend_equation);
		}
		break;
	default:
		{
			log_warn(str8_from("[{u3d}] UNKNOW BLEND MODE"), mode);
			break;
		};
	}

#if 0
	static str8 ctable_blend_str8[SM__BLEND_MAX] = {
	    str8_from("ALPHA"),
	    str8_from("ADDITIVE"),
	    str8_from("MULTIPLIED"),
	    str8_from("ADD_COLORS"),
	    str8_from("SUBTRACT_COLORS"),
	    str8_from("ALPHA_PREMULTIPLY"),
	    str8_from("CUSTOM"),
	};

	log_trace(str8_from("[{s}] blend mode set"), ctable_blend_str8[mode]);
#endif
}

static str8 sm__ctable_blend_mode_str8[BLEND_MODE_MAX] = {
    [BLEND_MODE_DEFAULT] = str8_from("BLEND_MODE_DEFAULT"),
    [BLEND_MODE_ALPHA] = str8_from("BLEND_MODE_ALPHA"),
    [BLEND_MODE_ADDITIVE] = str8_from("BLEND_MODE_ADDITIVE"),
    [BLEND_MODE_MULTIPLIED] = str8_from("BLEND_MODE_MULTIPLIED"),
    [BLEND_MODE_ADD_COLORS] = str8_from("BLEND_MODE_ADD_COLORS"),
    [BLEND_MODE_SUBTRACT_COLORS] = str8_from("BLEND_MODE_SUBTRACT_COLORS"),
    [BLEND_MODE_ALPHA_PREMULTIPLY] = str8_from("BLEND_MODE_ALPHA_PREMULTIPLY"),
    [BLEND_MODE_CUSTOM] = str8_from("BLEND_MODE_CUSTOM"),
};

static str8 sm__ctable_depth_func_str8[DEPTH_FUNC_MAX] = {
    [DEPTH_FUNC_DEFAULT] = str8_from("DEPTH_FUNC_DEFAULT"),
    [DEPTH_FUNC_NEVER] = str8_from("DEPTH_FUNC_NEVER"),
    [DEPTH_FUNC_LESS] = str8_from("DEPTH_FUNC_LESS"),
    [DEPTH_FUNC_EQUAL] = str8_from("DEPTH_FUNC_EQUAL"),
    [DEPTH_FUNC_LEQUAL] = str8_from("DEPTH_FUNC_LEQUAL"),
    [DEPTH_FUNC_GREATER] = str8_from("DEPTH_FUNC_GREATER"),
    [DEPTH_FUNC_NOTEQUAL] = str8_from("DEPTH_FUNC_NOTEQUAL"),
    [DEPTH_FUNC_GEQUAL] = str8_from("DEPTH_FUNC_GEQUAL"),
    [DEPTH_FUNC_ALWAYS] = str8_from("DEPTH_FUNC_ALWAYS"),
};

static str8 sm__ctable_cull_mode_str8[CULL_MODE_MAX] = {
    [CULL_MODE_DEFAULT] = str8_from("CULL_MODE_DEFAULT"),
    [CULL_MODE_FRONT] = str8_from("CULL_MODE_FRONT"),
    [CULL_MODE_BACK] = str8_from("CULL_MODE_BACK"),
    [CULL_MODE_FRONT_AND_BACK] = str8_from("CULL_MODE_FRONT_AND_BACK"),
};

static str8 sm__ctable_winding_mode_str8[WINDING_MODE_MAX] = {
    [WINDING_MODE_DEFAULT] = str8_from("WINDING_MODE_DEFAULT"),
    [WINDING_MODE_CLOCK_WISE] = str8_from("WINDING_MODE_CLOCK_WISE"),
    [WINDING_MODE_COUNTER_CLOCK_WISE] = str8_from("WINDING_MODE_COUNTER_CLOCK_WISE"),
};

static str8 sm__cable_polygon_mode_str8[POLYGON_MODE_MAX] = {
    [POLYGON_MODE_DEFAULT] = str8_from("POLYGON_MODE_DEFAULT"),
    [POLYGON_MODE_POINT] = str8_from("POLYGON_MODE_POINT"),
    [POLYGON_MODE_LINE] = str8_from("POLYGON_MODE_LINE"),
    [POLYGON_MODE_FILL] = str8_from("POLYGON_MODE_FILL"),
};

static void
gl__renderer_get_texture_formats(
    enum texture_pixel_format pixel_format, u32 *gl_internal_format, u32 *gl_format, u32 *gl_type)
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
	case TEXTURE_PIXELFORMAT_DEPTH:
		{
			*gl_internal_format = GL_DEPTH_COMPONENT32;
			*gl_format = GL_DEPTH_COMPONENT;
			*gl_type = GL_FLOAT;
		}
		break;
	case TEXTURE_PIXELFORMAT_DEPTH_STENCIL:
		{
			*gl_internal_format = GL_DEPTH24_STENCIL8;
			*gl_format = GL_DEPTH_STENCIL;
			*gl_type = GL_UNSIGNED_INT_24_8;
		}
		break;
	default: sm__unreachable(); break;
	}
}

static void
gl__renderer_texture_create(struct renderer_texture *texture)
{
	// sm__assert(texture->data || texture->resource_handle.id != INVALID_HANDLE);

	u32 handle = 0;
	u32 width;
	u32 height;
	enum texture_pixel_format pixel_format;
	// enum texture_usage usage;
	u8 *data;

	if (texture->resource_handle.id != INVALID_HANDLE)
	{
		struct sm__resource_image *image = resource_image_at(texture->resource_handle);
		width = image->width;
		height = image->height;
		pixel_format = (enum texture_pixel_format)image->pixel_format;
		// usage = TEXTURE_USAGE_IMMUTABLE;
		data = image->data;
	}
	else
	{
		width = texture->width;
		height = texture->height;
		pixel_format = texture->pixel_format;
		// usage = texture->usage;
		data = texture->data;
	}

	u32 gl_internal_format, gl_format, gl_type;
	gl__renderer_get_texture_formats(pixel_format, &gl_internal_format, &gl_format, &gl_type);

	glCall(glGenTextures(1, &handle));
	glCall(glBindTexture(GL_TEXTURE_2D, handle));

	glCall(glTexImage2D(
	    GL_TEXTURE_2D, 0, (i32)gl_internal_format, (i32)width, (i32)height, 0, gl_format, gl_type, data));

#if 1
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
#endif
	glCall(glBindTexture(GL_TEXTURE_2D, 0));

	texture->gl_handle = handle;
}

static void
gl__renderer_pass_create(struct renderer_pass *pass)
{
	sm__assert(pass);

	// store current framebuffer binding (restored at end of function)
	GLuint gl_orig_fb;
	glCall(glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&gl_orig_fb));

	// create a framebuffer object
	glCall(glGenFramebuffers(1, &pass->gl_handle));
	glCall(glBindFramebuffer(GL_FRAMEBUFFER, pass->gl_handle));

	for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
	{
		handle_t texture_id = pass->color_attachments[i].id;
		if (texture_id != INVALID_HANDLE)
		{
			struct renderer_texture *texture = renderer_texture_at((texture_handle){texture_id});
			glCall(glFramebufferTexture2D(
			    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, texture->gl_handle, 0));
		}
	}

	handle_t depth_stencil_id = pass->depth_stencil_attachment.id;
	if (depth_stencil_id != INVALID_HANDLE)
	{
		struct renderer_texture *texture = renderer_texture_at((texture_handle){depth_stencil_id});
		GLenum att_type = texture->pixel_format == TEXTURE_PIXELFORMAT_DEPTH_STENCIL
				      ? GL_DEPTH_STENCIL_ATTACHMENT
				      : GL_DEPTH_ATTACHMENT;
		glCall(glFramebufferTexture2D(GL_FRAMEBUFFER, att_type, GL_TEXTURE_2D, texture->gl_handle, 0));
	}

	GLenum status;
	glCall(status = glCheckFramebufferStatus(GL_FRAMEBUFFER));

	sm__assert(status == GL_FRAMEBUFFER_COMPLETE);

	log_trace(str8_from("pass successfully created!"));

	// restore original framebuffer binding
	glCall(glBindFramebuffer(GL_FRAMEBUFFER, gl_orig_fb));
}

static void
gl__shader_cache_actives(struct renderer_shader *shader)
{
	GLint count = 0;

	GLint size;  // size of the variable
	GLenum type; // type of the variable (float, vec3 or mat4, etc)
	GLint location;

	const GLsizei bufSize = 64; // maximum name length
	GLchar _name[bufSize];	    // variable name in GLSL
	GLsizei length;		    // name length

	glCall(glUseProgram(shader->gl_shader_program_handle));

	glCall(glGetProgramiv(shader->gl_shader_program_handle, GL_ACTIVE_ATTRIBUTES, &count));
	shader->attributes_count = count;
	shader->attributes = arena_reserve(&RC.arena, count * sizeof(struct shader_attribute));

	for (i32 i = 0; i < count; ++i)
	{
		glCall(glGetActiveAttrib(shader->gl_shader_program_handle, i, bufSize, &length, &size, &type, _name));
		glCall(location = glGetAttribLocation(shader->gl_shader_program_handle, _name));
		sm__assert(location != -1);

		str8 name = (str8){.idata = _name, .size = length};

		shader->attributes[i].name = str8_dup(&RC.arena, name);
		shader->attributes[i].size = size;
		shader->attributes[i].type = gl__to_shader_type(type);
		shader->attributes[i].location = location;
	}

	glCall(glGetProgramiv(shader->gl_shader_program_handle, GL_ACTIVE_UNIFORMS, &count));

	u32 n_uniforms = 0, n_samplers = 0;
	shader->uniforms = arena_reserve(&RC.arena, count * sizeof(struct shader_uniform));
	shader->samplers = arena_reserve(&RC.arena, count * sizeof(struct shader_sampler));
	// shader->uniforms_count = count;

	for (i32 i = 0; i < count; ++i)
	{
		glCall(glGetActiveUniform(shader->gl_shader_program_handle, i, bufSize, &length, &size, &type, _name));
		glCall(location = glGetUniformLocation(shader->gl_shader_program_handle, _name));
		sm__assert(location != -1);

		if (strncmp(_name, "gl_", 3) == 0)
		{
			continue;
		}

		i8 *bracket = strchr(_name, '[');
		b32 is_first = 0;

		if (bracket == 0 || (bracket[1] == '0' && bracket[2] == ']'))
		{
			if (bracket)
			{
				sm__assert(bracket[3] == '\0'); // array of structs not supported yet
				*bracket = '\0';
				length = (GLint)(bracket - _name);
			}
			is_first = 1;
		}

		if (type >= GL_SAMPLER_1D && type <= GL_SAMPLER_2D_SHADOW)
		{
			glCall(glUniform1i(location, n_samplers));
			shader->samplers[n_samplers].name = str8_from_cstr(&RC.arena, _name);
			shader->samplers[n_samplers].type = gl__to_shader_type(type);
			shader->samplers[n_samplers].location = n_samplers;
			n_samplers++;
		}
		else
		{
			str8 name = (str8){.idata = _name, .size = length};
			if (is_first)
			{
				shader->uniforms[n_uniforms].name = str8_dup(&RC.arena, name);
				shader->uniforms[n_uniforms].size = size;
				shader->uniforms[n_uniforms].type = gl__to_shader_type(type);
				shader->uniforms[n_uniforms].location = location;
				n_uniforms++;
			}
			else if (bracket != 0 && bracket[1] > '0')
			{
				*bracket = '\0';
				for (i32 u = n_uniforms - 1; u >= 0; u--)
				{
					if (str8_eq(shader->uniforms[u].name, name))
					{
						u32 index = (u32)(atoi(bracket + 1) + 1);
						if (index > shader->uniforms[u].size)
						{
							shader->uniforms[u].size = index;
						}
					}
				}
			}
		}
	}

	glCall(glUseProgram(0));

	// shader->uniforms = arena_resize(arena, shader->uniforms, n_uniforms);
	// shader->samplers = arena_resize(arena, shader->samplers, n_samplers);

	shader->uniforms_count = n_uniforms;
	shader->samplers_count = n_samplers;

	for (u32 i = 0; i < n_uniforms; ++i)
	{
		struct shader_uniform *uni = shader->uniforms + i;

		u32 array_size = uni->size * gl__ctable_type_size[uni->type];
		shader->uniforms[i].data = arena_reserve(&RC.arena, array_size);
		memset(uni->data, 0x0, array_size);
		// uni->dirty = 0;
	}

	log_trace(str8_from("uniforms: {s}"), shader->label);
	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		struct shader_uniform *uni = shader->uniforms + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, size: {i3d}, type: {s}"), uni->name, uni->location,
		    uni->size, sm__ctable_shader_type_str8[uni->type]);
	}

	log_trace(str8_from("samplers: {s}"), shader->label);
	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		struct shader_sampler *samp = shader->samplers + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, type: {s}"), samp->name, samp->location,
		    sm__ctable_shader_type_str8[samp->type]);
	}

	log_trace(str8_from("attributes: {s}"), shader->label);
	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		struct shader_attribute *attr = shader->attributes + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, size: {i3d}, type: {s}"), attr->name, attr->location,
		    attr->size, sm__ctable_shader_type_str8[attr->type]);
	}
}

static void
gl__renderer_shader_create(struct renderer_shader *shader)
{
	if (shader->gl_shader_program_handle == 0)
	{
		if (shader->vs.handle.id != INVALID_HANDLE)
		{
			struct sm__resource_text *vs = resource_text_at(shader->vs.handle);
			shader->gl_vs_handle = gl__renderer_shader_compile_vert(vs->data);
		}
		else
		{
			shader->gl_vs_handle = gl__renderer_shader_compile_vert(shader->vs.source);
		}
		if (shader->fs.handle.id != INVALID_HANDLE)
		{
			struct sm__resource_text *fs = resource_text_at(shader->fs.handle);
			shader->gl_fs_handle = gl__renderer_shader_compile_frag(fs->data);
		}
		else
		{
			shader->gl_fs_handle = gl__renderer_shader_compile_frag(shader->fs.source);
		}

		sm__assert(shader->gl_fs_handle);
		sm__assert(shader->gl_vs_handle);

		glCall(shader->gl_shader_program_handle = glCreateProgram());

		if (!gl__renderer_shader_link(shader))
		{
			glCall(glDeleteProgram(shader->gl_shader_program_handle));
			glCall(glDeleteShader(shader->gl_fs_handle));
			glCall(glDeleteShader(shader->gl_vs_handle));
			log_error(str8_from("error linking program shader"));

			return;
		}

		gl__shader_cache_actives(shader);
	}
}

static void
gl__renderer_sampler_create(struct renderer_sampler *sampler)
{
	glCall(glGenSamplers(1, &sampler->gl_handle));

	static GLint ctable_filter[FILTER_MAX] = {
	    [FILTER_DEFAULT] = GL_NEAREST,
	    [FILTER_NONE] = GL_NEAREST,
	    [FILTER_NEAREST] = GL_NEAREST,
	    [FILTER_LINEAR] = GL_LINEAR,
	};
	const GLint gl_min_filter = ctable_filter[sampler->min_filter];
	const GLint gl_mag_filter = ctable_filter[sampler->mag_filter];

	static GLint ctable_wrap[WRAP_MAX] = {

	    [WRAP_DEFAULT] = GL_REPEAT,
	    [WRAP_REPEAT] = GL_REPEAT,
	    [WRAP_CLAMP_TO_EDGE] = GL_CLAMP_TO_EDGE,
	    [WRAP_CLAMP_TO_BORDER] = GL_CLAMP_TO_BORDER,
	    [WRAP_MIRRORED_REPEAT] = GL_MIRRORED_REPEAT,
	};

	GLint wrap_u = ctable_wrap[sampler->wrap_u];
	GLint wrap_v = ctable_wrap[sampler->wrap_v];
	GLint wrap_w = ctable_wrap[sampler->wrap_w];

	glSamplerParameteri(sampler->gl_handle, GL_TEXTURE_MIN_FILTER, gl_min_filter);
	glSamplerParameteri(sampler->gl_handle, GL_TEXTURE_MAG_FILTER, gl_mag_filter);

	// GL spec has strange defaults for mipmap min/max lod: -1000 to +1000
	f32 min_lod = glm_clamp(sampler->min_lod, 0.0f, 1000.0f);
	f32 max_lod = glm_clamp(sampler->max_lod, 0.0f, 1000.0f);
	glSamplerParameterf(sampler->gl_handle, GL_TEXTURE_MIN_LOD, min_lod);
	glSamplerParameterf(sampler->gl_handle, GL_TEXTURE_MAX_LOD, max_lod);
	glSamplerParameteri(sampler->gl_handle, GL_TEXTURE_WRAP_S, wrap_u);
	glSamplerParameteri(sampler->gl_handle, GL_TEXTURE_WRAP_T, wrap_v);
	glSamplerParameteri(sampler->gl_handle, GL_TEXTURE_WRAP_R, wrap_w);

	f32 border[4];
	switch (sampler->border_color)
	{
	case BORDER_COLOR_TRANSPARENT_BLACK:
		{
			border[0] = 0.0f, border[1] = 0.0f, border[2] = 0.0f, border[3] = 0.0f;
		}
		break;
	case BORDER_COLOR_OPAQUE_WHITE:
		{
			border[0] = 1.0f, border[1] = 1.0f, border[2] = 1.0f, border[3] = 1.0f;
		}
		break;
	default:
		{
			border[0] = 0.0f, border[1] = 0.0f, border[2] = 0.0f, border[3] = 1.0f;
		}
		break;
	}
	glSamplerParameterfv(sampler->gl_handle, GL_TEXTURE_BORDER_COLOR, border);
}

static void
sm__renderer_buffer_bind(GLenum target, GLuint buffer)
{
	if (target == GL_ARRAY_BUFFER)
	{
		if (RC.current.gl.bind_vertex_buffer != buffer)
		{
			glCall(glBindBuffer(GL_ARRAY_BUFFER, buffer));
			RC.current.gl.bind_vertex_buffer = buffer;
		}
	}
	else
	{
		if (RC.current.gl.bind_index_buffer != buffer)
		{
			glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer));
			RC.current.gl.bind_index_buffer = buffer;
		}
	}
}

static void
sm__renderer_buffer_store(GLenum target)
{
	if (target == GL_ARRAY_BUFFER)
	{
		RC.current.gl.store_bind_vertex_buffer = RC.current.gl.bind_vertex_buffer;
	}
	else
	{
		RC.current.gl.store_bind_index_buffer = RC.current.gl.bind_index_buffer;
	}
}

static void
sm__renderer_buffer_restore(GLenum target)
{
	if (target == GL_ARRAY_BUFFER)
	{
		sm__renderer_buffer_bind(GL_ARRAY_BUFFER, RC.current.gl.store_bind_vertex_buffer);
		RC.current.gl.store_bind_vertex_buffer = 0;
	}
	else
	{
		sm__renderer_buffer_bind(GL_ELEMENT_ARRAY_BUFFER, RC.current.gl.store_bind_index_buffer);
		RC.current.gl.store_bind_index_buffer = 0;
	}
}

static void
gl__renderer_buffer_create(struct renderer_buffer *buffer)
{
	static GLenum ctable_usage[BUFFER_USAGE_MAX] = {
	    [BUFFER_USAGE_DEFAULT] = GL_STATIC_DRAW,
	    [BUFFER_USAGE_IMMUTABLE] = GL_STATIC_DRAW,
	    [BUFFER_USAGE_DYNAMIC] = GL_DYNAMIC_DRAW,
	    [BUFFER_USAGE_STREAM] = GL_STREAM_DRAW,
	};

	static GLenum ctable_target[BUFFER_TYPE_MAX] = {
	    [BUFFER_TYPE_DEFAULT] = GL_ARRAY_BUFFER,
	    [BUFFER_TYPE_VERTEXBUFFER] = GL_ARRAY_BUFFER,
	    [BUFFER_TYPE_INDEXBUFFER] = GL_ELEMENT_ARRAY_BUFFER,
	};

	GLenum usage = ctable_usage[buffer->usage];
	GLenum target = ctable_target[buffer->buffer_type];

	glCall(glGenBuffers(1, &buffer->gl_handle));

	sm__renderer_buffer_store(target);
	sm__renderer_buffer_bind(target, buffer->gl_handle);

	glCall(glBufferData(target, buffer->size, 0, usage));
	if (buffer->usage == BUFFER_USAGE_IMMUTABLE)
	{
		sm__assert(buffer->data);
		glCall(glBufferSubData(target, 0, buffer->size, buffer->data));
	}
	sm__renderer_buffer_restore(target);
}

static buffer_handle
sm__renderer_buffer_alloc(void)
{
	buffer_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.buffer_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_buffer_validate(sm__maybe_unused const struct renderer_buffer_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_buffer_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_buffer_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_buffer_desc.label not set");

	sm__assertf(desc->buffer_type >= BUFFER_TYPE_DEFAULT && desc->buffer_type < BUFFER_TYPE_MAX,
	    "renderer_buffer_desc.buffer_type invalid value");
	sm__assertf(desc->usage >= BUFFER_USAGE_DEFAULT && desc->usage < BUFFER_USAGE_MAX,
	    "renderer_buffer_desc.usage invalid value");
	sm__assertf(desc->data, "renderer_buffer_desc.data not set");
	sm__assertf(desc->size > 0, "renderer_buffer_desc.size not set");

	return (result);
#endif
}

static struct renderer_buffer_desc
sm__renderer_buffer_defaults(const struct renderer_buffer_desc *desc)
{
	struct renderer_buffer_desc result;

	result = *desc;

	result.buffer_type = result.buffer_type == BUFFER_TYPE_DEFAULT ? BUFFER_TYPE_VERTEXBUFFER : result.buffer_type;
	result.usage = result.usage == BUFFER_USAGE_DEFAULT ? BUFFER_USAGE_IMMUTABLE : result.usage;

	return (result);
}

struct renderer_buffer *
renderer_buffer_at(buffer_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.buffers[slot_index];
}

buffer_handle
renderer_buffer_make(const struct renderer_buffer_desc *desc)
{
	buffer_handle result = sm__renderer_buffer_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_buffer *buffer_at = renderer_buffer_at(result);

		if (sm__renderer_buffer_validate(desc))
		{
			struct renderer_buffer_desc desc_def = sm__renderer_buffer_defaults(desc);

			buffer_at->slot.id = result.id;
			buffer_at->label = desc_def.label;

			buffer_at->buffer_type = desc_def.buffer_type;
			buffer_at->usage = desc_def.usage;
			buffer_at->size = desc_def.size;
			buffer_at->data = desc_def.data;

			gl__renderer_buffer_create(buffer_at);
		}
	}

	return (result);
}

static texture_handle
sm__renderer_texture_alloc(void)
{
	texture_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.texture_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_texture_validate(sm__maybe_unused const struct renderer_texture_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_texture_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_texture_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_texture_desc.label not set");

	if (desc->handle.id != INVALID_HANDLE)
	{
		sm__assertf(desc->width == 0, "renderer_texture_desc.width and .resource_handle are ambiguos");
		sm__assertf(desc->height == 0, "renderer_texture_desc.height and .resource_handle are ambiguos");
		sm__assertf(desc->usage == TEXTURE_USAGE_DEFAULT,
		    "renderer_texture_desc.usage and .resource_handle are ambiguos");
		sm__assertf(desc->pixel_format == TEXTURE_PIXELFORMAT_DEFAULT,
		    "renderer_texture_desc.pixel_format and .resource_handle are ambiguos");
		sm__assertf(desc->data == 0, "renderer_texture_desc.data and .resource_handle are ambiguos");
	}
	else
	{
		sm__assertf(desc->width > 0, "renderer_texture_desc.width must be > 0");
		sm__assertf(desc->height > 0, "renderer_texture_desc.height must be > 0");
		sm__assertf(desc->usage >= TEXTURE_USAGE_DEFAULT && desc->usage < TEXTURE_USAGE_MAX,
		    "renderer_texture_desc.usage: invalid value");
		sm__assertf(
		    desc->pixel_format >= TEXTURE_PIXELFORMAT_DEFAULT && desc->pixel_format < TEXTURE_PIXELFORMAT_MAX,
		    "renderer_texture_desc.pixel_format: invalid pixel format");

		if (desc->usage == TEXTURE_USAGE_IMMUTABLE)
		{
			sm__assertf(desc->data != 0, "renderer_texture_desc.data not set");
		}
		else
		{
			sm__assertf(desc->data == 0,
			    "renderer_texture_desc.data set with but not marked as TEXTURE_USAGE_IMMUTABLE");
		}
	}

	return (result);
#endif
}

static struct renderer_texture_desc
sm__renderer_texture_defaults(const struct renderer_texture_desc *desc)
{
	struct renderer_texture_desc result;

	result = *desc;

	if (desc->handle.id == INVALID_HANDLE)
	{
		result.pixel_format = (result.pixel_format == TEXTURE_PIXELFORMAT_DEFAULT)
					  ? TEXTURE_PIXELFORMAT_UNCOMPRESSED_R8G8B8
					  : result.pixel_format;
		result.usage = (result.usage == TEXTURE_USAGE_DEFAULT) ? TEXTURE_USAGE_IMMUTABLE : result.usage;
	}

	return (result);
}

struct renderer_texture *
renderer_texture_at(texture_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.textures[slot_index];
}

texture_handle
renderer_texture_make(const struct renderer_texture_desc *desc)
{
	texture_handle result = sm__renderer_texture_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_texture *texture_at = renderer_texture_at(result);

		if (sm__renderer_texture_validate(desc))
		{
			struct renderer_texture_desc desc_def = sm__renderer_texture_defaults(desc);
			texture_at->slot.id = result.id;
			texture_at->label = desc_def.label;

			if (desc_def.handle.id != INVALID_HANDLE)
			{
				texture_at->resource_handle.id = desc_def.handle.id;
			}
			else
			{
				texture_at->resource_handle.id = desc_def.handle.id;
				texture_at->width = desc_def.width;
				texture_at->height = desc_def.height;
				texture_at->pixel_format = desc_def.pixel_format;
				texture_at->usage = desc_def.usage;
				texture_at->data = desc_def.data;
			}

			gl__renderer_texture_create(texture_at);
		}
	}

	return (result);
}

static sampler_handle
sm__renderer_sampler_alloc(void)
{
	sampler_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.sampler_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_sampler_validate(sm__maybe_unused const struct renderer_sampler_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_sampler_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_sampler_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_sampler_desc.label not set");

	sm__assertf(desc->min_filter >= FILTER_DEFAULT && desc->min_filter < FILTER_MAX,
	    "renderer_sampler_desc.min_filter invalid value");
	sm__assertf(desc->mag_filter >= FILTER_DEFAULT && desc->mag_filter < FILTER_MAX,
	    "renderer_sampler_desc.mag_filter invalid value");

	sm__assertf(
	    desc->wrap_u >= WRAP_DEFAULT && desc->wrap_u < WRAP_MAX, "renderer_sampler_desc.wrap_u invalid value");
	sm__assertf(
	    desc->wrap_v >= WRAP_DEFAULT && desc->wrap_v < WRAP_MAX, "renderer_sampler_desc.wrap_v invalid value");
	sm__assertf(
	    desc->wrap_w >= WRAP_DEFAULT && desc->wrap_w < WRAP_MAX, "renderer_sampler_desc.wrap_w invalid value");

	sm__assertf(!isnanf(desc->min_lod) && !isinff(desc->min_lod), "renderer_sampler_desc.min_lod invalid value");
	sm__assertf(!isnanf(desc->max_lod) && !isinff(desc->max_lod), "renderer_sampler_desc.max_lod invalid value");

	sm__assertf(desc->border_color >= BORDER_COLOR_DEFAULT && desc->border_color < BORDER_COLOR_MAX,
	    "renderer_sampler_desc.border_color invalid value");

	return (result);
#endif
}

static struct renderer_sampler_desc
sm__renderer_sampler_defaults(const struct renderer_sampler_desc *desc)
{
	struct renderer_sampler_desc result;

	result = *desc;

	result.min_filter = (result.min_filter == FILTER_DEFAULT) ? FILTER_NEAREST : result.min_filter;
	result.mag_filter = (result.mag_filter == FILTER_DEFAULT) ? FILTER_NEAREST : result.mag_filter;

	result.wrap_u = (result.wrap_u == WRAP_DEFAULT) ? WRAP_REPEAT : result.wrap_u;
	result.wrap_v = (result.wrap_v == WRAP_DEFAULT) ? WRAP_REPEAT : result.wrap_v;
	result.wrap_w = (result.wrap_w == WRAP_DEFAULT) ? WRAP_REPEAT : result.wrap_w;

	result.max_lod = (result.max_lod == 0.0f) ? FLT_MAX : result.max_lod;

	result.border_color =
	    (result.border_color == BORDER_COLOR_DEFAULT) ? BORDER_COLOR_OPAQUE_WHITE : result.border_color;

	return (result);
}

struct renderer_sampler *
renderer_sampler_at(sampler_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.samplers[slot_index];
}

sampler_handle
renderer_sampler_make(const struct renderer_sampler_desc *desc)
{
	sampler_handle result = sm__renderer_sampler_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_sampler *sampler_at = renderer_sampler_at(result);

		if (sm__renderer_sampler_validate(desc))
		{
			struct renderer_sampler_desc desc_def = sm__renderer_sampler_defaults(desc);

			sampler_at->slot.id = result.id;
			sampler_at->label = desc_def.label;

			sampler_at->min_filter = desc_def.min_filter;
			sampler_at->mag_filter = desc_def.mag_filter;
			sampler_at->wrap_u = desc_def.wrap_u;
			sampler_at->wrap_v = desc_def.wrap_v;
			sampler_at->wrap_w = desc_def.wrap_w;
			sampler_at->min_lod = desc_def.min_lod;
			sampler_at->max_lod = desc_def.max_lod;
			sampler_at->border_color = desc_def.border_color;

			gl__renderer_sampler_create(sampler_at);
		}
	}

	return (result);
}

static pass_handle
sm__renderer_pass_alloc(void)
{
	pass_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.pass_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_pass_validate(sm__maybe_unused const struct renderer_pass_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	b32 result = 1;
	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_pass_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_pass_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_pass_desc.label not set");

	sm__assertf(
	    desc->color_attachments[0].id != INVALID_HANDLE || desc->depth_stencil_attachment.id != INVALID_HANDLE,
	    "renderer_pass_desc must have at least 1 attachment");

	i32 width = -1;
	i32 height = -1;
	b32 has_gap = 0;
	for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
	{
		if (desc->color_attachments[i].id != INVALID_HANDLE)
		{
			b32 is_valid = handle_valid(&RC.pools.texture_pool, desc->color_attachments[i].id);
			sm__assertf(is_valid, "renderer_pass_desc.color_attachments.id does not have a valid handle");
			sm__assert(!has_gap);

			struct renderer_texture *texture = renderer_texture_at(desc->color_attachments[i]);
			if (width == -1 && height == -1)
			{
				width = texture->width;
				height = texture->height;
			}
			else
			{
				sm__assert(width > -1 && height > -1);
				sm__assert((u32)width == texture->width);
				sm__assert((u32)height == texture->height);
			}
		}
		else
		{
			has_gap = 1;
		}
	}

	if (desc->depth_stencil_attachment.id != INVALID_HANDLE)
	{
		b32 is_valid = handle_valid(&RC.pools.texture_pool, desc->depth_stencil_attachment.id);
		sm__assertf(is_valid, "renderer_pass_desc.depth_stencil_attachment.id does not have a valid handle");

		struct renderer_texture *texture = renderer_texture_at(desc->depth_stencil_attachment);
		if (width == -1 && height == -1)
		{
			width = texture->width;
			height = texture->height;
		}
		else
		{
			sm__assert(width > -1 && height > -1);
			sm__assert((u32)width == texture->width);
			sm__assert((u32)height == texture->height);
		}
	}
	sm__assert(width > 0 && height > 0);

	return (result);
#endif
}

static struct renderer_pass_desc
sm__renderer_pass_defaults(const struct renderer_pass_desc *desc)
{
	struct renderer_pass_desc result;

	result = *desc;

	return (result);
}

struct renderer_pass *
renderer_pass_at(pass_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.passes[slot_index];
}

pass_handle
renderer_pass_make(const struct renderer_pass_desc *desc)
{
	pass_handle result = sm__renderer_pass_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_pass *pass_at = renderer_pass_at(result);

		if (sm__renderer_pass_validate(desc))
		{
			struct renderer_pass_desc desc_def = sm__renderer_pass_defaults(desc);

			pass_at->slot.id = result.id;
			pass_at->label = desc_def.label;

			u32 width = 0, height = 0;
			for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
			{
				pass_at->color_attachments[i].id = desc_def.color_attachments[i].id;
				if (width == 0)
				{
					struct renderer_texture *tex =
					    renderer_texture_at(pass_at->color_attachments[i]);
					width = tex->width;
					height = tex->height;
				}
			}
			pass_at->depth_stencil_attachment.id = desc_def.depth_stencil_attachment.id;
			if (width == 0)
			{
				struct renderer_texture *tex = renderer_texture_at(pass_at->depth_stencil_attachment);
				width = tex->width;
				height = tex->height;
			}

			sm__assert(width != 0 && height != 0);

			pass_at->width = width;
			pass_at->height = height;

			gl__renderer_pass_create(pass_at);
		}
	}

	return (result);
}

static void
sm__renderer_pass_action_resolve(const struct renderer_pass_action *from, struct renderer_pass_action *to)
{
	sm__assert(from && to);
	*to = *from;

	for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
	{
		if (to->colors[i].load_action == LOAD_ACTION_DEFAULT)
		{
			to->colors[i].load_action = LOAD_ACTION_CLEAR;
			to->colors[i].clear_value.r = 127;
			to->colors[i].clear_value.g = 127;
			to->colors[i].clear_value.b = 127;
			to->colors[i].clear_value.a = 127;
		}

		if (to->colors[i].store_action == STORE_ACTION_DEFAULT)
		{
			to->colors[i].store_action = STORE_ACTION_STORE;
		}
	}

	if (to->depth.load_action == LOAD_ACTION_DEFAULT)
	{
		to->depth.load_action = LOAD_ACTION_CLEAR;
		to->depth.clear_value = 1.0f;
	}
	if (to->depth.store_action == STORE_ACTION_DEFAULT)
	{
		to->depth.store_action = STORE_ACTION_DONTCARE;
	}
	// if (to->stencil.load_action == LOAD_ACTION_DEFAULT)
	// {
	// 	to->stencil.load_action = LOADACTION_CLEAR;
	// 	to->stencil.clear_value = DEFAULT_CLEAR_STENCIL;
	// }
	// if (to->stencil.store_action == _STOREACTION_DEFAULT) { to->stencil.store_action =
	// STOREACTION_DONTCARE; }
}

void
renderer_pass_begin(pass_handle pass, const struct renderer_pass_action *pass_action)
{
	struct renderer_pass_action action;
	sm__renderer_pass_action_resolve(pass_action, &action);

	u32 width = 0;
	u32 height = 0;
	struct renderer_pass *pass_at = 0;
	if (pass.id != INVALID_HANDLE)
	{
		pass_at = renderer_pass_at(pass);
		sm__assert(pass_at->gl_handle);
		glCall(glBindFramebuffer(GL_FRAMEBUFFER, pass_at->gl_handle));

		width = pass_at->width;
		height = pass_at->height;
		RC.current.pass = pass;
	}
	else
	{
		glCall(glBindFramebuffer(GL_FRAMEBUFFER, RC.current.gl.default_fbo));
		width = RC.width;
		height = RC.height;
		RC.current.pass.id = INVALID_HANDLE;
	}

	RC.current.in_pass = 1;
	RC.current.pass_width = width;
	RC.current.pass_height = height;

	glCall(glViewport(0, 0, width, height));
	glCall(glScissor(0, 0, width, height));

	for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
	{
		if (LOAD_ACTION_CLEAR == action.colors[i].load_action)
		{
			v4 c = color_to_v4(action.colors[i].clear_value);
			if (pass.id != INVALID_HANDLE)
			{
				glCall(glDrawBuffer(GL_COLOR_ATTACHMENT0 + i));
				glCall(glClearColor(c.r, c.g, c.b, c.a));
				glCall(glClear(GL_COLOR_BUFFER_BIT));
			}
			else
			{
				glCall(glClearColor(c.r, c.g, c.b, c.a));
				glCall(glClear(GL_COLOR_BUFFER_BIT));
				// glCall(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
				break;
			}
		}
	}
	// TODO: fix this
	if (pass.id != INVALID_HANDLE)
	{
		glCall(glDrawBuffer(GL_COLOR_ATTACHMENT0));
	}

	if (action.depth.load_action == LOAD_ACTION_CLEAR)
	{
		glCall(glClearDepth(action.depth.clear_value));
		glCall(glClear(GL_DEPTH_BUFFER_BIT));
	}

	for (u32 i = 0; i < MAX_COLOR_ATTACHMENTS; ++i)
	{
		RC.current.store_action_color[i] = action.colors[i].store_action;
	}

	RC.current.store_action_depth = action.depth.store_action;
}

void
renderer_pass_end(void)
{
	RC.current.pass.id = INVALID_HANDLE;
	RC.current.pass_width = 0;
	RC.current.pass_height = 0;

	glCall(glBindFramebuffer(GL_FRAMEBUFFER, RC.current.gl.default_fbo));
	RC.current.in_pass = 0;
}

static shader_handle
sm__renderer_shader_alloc(void)
{
	shader_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.shader_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_shader_validate(sm__maybe_unused const struct renderer_shader_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_shader_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_shader_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_shader_desc.label not set");

	sm__assertf(!(desc->vs.handle.id != INVALID_HANDLE && desc->vs.source.size > 0),
	    "renderer_shader_desc.vs.handle and .source are ambiguos");
	sm__assertf(!(desc->fs.handle.id != INVALID_HANDLE && desc->fs.source.size > 0),
	    "renderer_shader_desc.fs.handle and .source are ambiguos");

	sm__assertf(!(desc->vs.handle.id == INVALID_HANDLE && desc->vs.source.size == 0),
	    "renderer_shader_desc.vs.handle and .source are ambiguos");
	sm__assertf(!(desc->fs.handle.id == INVALID_HANDLE && desc->fs.source.size == 0),
	    "renderer_shader_desc.fs.handle and .source are ambiguos");

	return (1);
#endif
}

struct renderer_shader *
renderer_shader_at(shader_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.shaders[slot_index];
}

shader_handle
renderer_shader_make(const struct renderer_shader_desc *desc)
{
	shader_handle result = sm__renderer_shader_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_shader *shader_at = renderer_shader_at(result);

		if (sm__renderer_shader_validate(desc))
		{
			shader_at->slot.id = result.id;
			shader_at->label = desc->label;

			if (desc->vs.handle.id != INVALID_HANDLE)
			{
				shader_at->vs.handle = desc->vs.handle;
			}
			else
			{
				shader_at->vs.source = desc->vs.source;
			}

			if (desc->fs.handle.id != INVALID_HANDLE)
			{
				shader_at->fs.handle = desc->fs.handle;
			}
			else
			{
				shader_at->fs.source = desc->fs.source;
			}

			gl__renderer_shader_create(shader_at);
		}
	}

	return (result);
}

static struct shader_attribute
sm__renderer_shader_get_attribute(struct renderer_shader *shader, str8 attribute)
{
	struct shader_attribute result = {.location = -1};

	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		if (str8_eq(shader->attributes[i].name, attribute))
		{
			sm__assert(shader->attributes[i].location != -1);
			result = shader->attributes[i];
			break;
		}
	}

	return (result);
}

static struct shader_uniform
sm__renderer_shader_get_uniform(struct renderer_shader *shader, str8 attribute)
{
	struct shader_uniform result = {.location = -1};

	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		if (str8_eq(shader->uniforms[i].name, attribute))
		{
			sm__assert(shader->uniforms[i].location != -1);
			result = shader->uniforms[i];
			break;
		}
	}

	return (result);
}

static struct shader_sampler
sm__renderer_shader_get_sampler(struct renderer_shader *shader, str8 sampler)
{
	struct shader_sampler result = {.location = -1};

	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		if (str8_eq(shader->samplers[i].name, sampler))
		{
			sm__assert(shader->samplers[i].location != -1);
			result = shader->samplers[i];
			break;
		}
	}

	return (result);
}

static i32
sm__renderer_shader_get_attribute_slot(struct renderer_shader *shader, str8 attribute, b32 sm__assert)
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

	if (sm__assert)
	{
		sm__assert(result != -1);
	}

	return (result);
}

static i32
sm__renderer_shader_get_uniform_loc(struct renderer_shader *shader, str8 uniform, b32 sm__assert)
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

	if (sm__assert)
	{
		sm__assert(result != -1);
	}

	return (result);
}

static i32
sm__renderer_shader_get_sampler_slot(struct renderer_shader *shader, str8 sampler, b32 sm__assert)
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

	if (sm__assert)
	{
		sm__assert(result != -1);
	}

	return (result);
}

static pipeline_handle
sm__renderer_pipeline_alloc(void)
{
	pipeline_handle result;

	handle_t handle = handle_new(&RC.arena, &RC.pools.pipeline_pool);
	result.id = handle;

	return result;
}

static b32
sm__renderer_pipeline_validate(sm__maybe_unused const struct renderer_pipeline_desc *desc)
{
#if !defined(SM_DEBUG)
	return (1);
#else

	sm__assert(desc);
	sm__assertf(desc->_start_canary == 0, "renderer_pipeline_desc not initialized");
	sm__assertf(desc->_end_canary == 0, "renderer_pipeline_desc not initialized");
	sm__assertf(desc->label.size > 0, "renderer_pipeline_desc.label not set");

	sm__assertf((desc->shader.id != INVALID_HANDLE), "renderer_pipeline_desc.shader not set");

	// Blend
	sm__assertf((desc->blend.enable >= STATE_DEFAULT && desc->blend.enable < STATE_MAX),
	    "renderer_pipeline_desc.blend.enable invalid value");
	sm__assertf((desc->blend.mode >= BLEND_MODE_DEFAULT && desc->blend.mode < BLEND_MODE_MAX),
	    "renderer_pipeline_desc.blend.mode invalid value");

	// Depth
	sm__assertf((desc->depth.enable >= STATE_DEFAULT && desc->depth.enable < STATE_MAX),
	    "renderer_pipeline_desc.depth.enable invalid value");
	sm__assertf((desc->depth.depth_func >= DEPTH_FUNC_DEFAULT && desc->depth.depth_func < DEPTH_FUNC_MAX),
	    "renderer_pipeline_desc.depth.depth_func invalid value");

	// Rasterizer
	sm__assertf((desc->rasterizer.cull_enable >= STATE_DEFAULT && desc->rasterizer.cull_enable < STATE_MAX),
	    "renderer_pipeline_desc.rasterizer.cull_enable invalid value");
	sm__assertf((desc->rasterizer.cull_mode >= CULL_MODE_DEFAULT && desc->rasterizer.cull_mode < CULL_MODE_MAX),
	    "renderer_pipeline_desc.rasterizer.cull_mode invalid value");

	sm__assertf(
	    (desc->rasterizer.winding_mode >= WINDING_MODE_DEFAULT && desc->rasterizer.winding_mode < WINDING_MODE_MAX),
	    "renderer_pipeline_desc.rasterizer.winding_mode invalid value");

	sm__assertf((desc->rasterizer.scissor >= STATE_DEFAULT && desc->rasterizer.scissor < STATE_MAX),
	    "renderer_pipeline_desc.rasterizer.scissor invalid value");

	sm__assertf(
	    (desc->rasterizer.polygon_mode >= POLYGON_MODE_DEFAULT && desc->rasterizer.polygon_mode < POLYGON_MODE_MAX),
	    "renderer_pipeline_desc.rasterizer.polygon_mode invalid value");

	sm__assertf(!isnanf(desc->rasterizer.line_width) && !isinff(desc->rasterizer.line_width) &&
			desc->rasterizer.line_width >= 0.0f,
	    "renderer_pipeline_desc.rasterizer.line_width has no valid float point value");

	struct renderer_shader *shader_at = renderer_shader_at(desc->shader);
	for (u32 i = 0; i < shader_at->attributes_count; ++i)
	{
		const struct shader_attribute *attr = shader_at->attributes + i;
		b32 found = 0;
		for (u32 attr_index = 0; attr_index < MAX_VERTEX_ATTRIBUTES; ++attr_index)
		{
			const struct vertex_attr_state *attr_state = desc->layout.attrs + attr_index;
			if (str8_eq(attr->name, attr_state->name))
			{
				found = 1;
				break;
			}
		}
		sm__assertf(found, "renderer_pipeline_desc.vertex_attr_state not found in shader");
	}

	for (u32 buf_index = 0; buf_index < MAX_VERTEX_ATTRIBUTES; ++buf_index)
	{
		const struct vertex_buffer_layout_state *buffer_state = desc->layout.buffers + buf_index;
		if (buffer_state->name.size == 0)
		{
			continue;
		}

		b32 found = 0;
		for (u32 i = 0; i < shader_at->attributes_count; ++i)
		{
			const struct shader_attribute *attr = shader_at->attributes + i;
			if (str8_eq(attr->name, buffer_state->name))
			{
				found = 1;
				break;
			}
		}
		sm__assertf(found, "renderer_pipeline_desc.vertex_buffer_layout_state not found in shader");
	}

	return (1);
#endif
}

static struct renderer_pipeline_desc
sm__renderer_pipeline_defaults(const struct renderer_pipeline_desc *desc)
{
	struct renderer_pipeline_desc result;

	result = *desc;

	result.blend.enable = result.blend.enable == STATE_DEFAULT ? STATE_FALSE : result.blend.enable;
	result.blend.mode = result.blend.mode == BLEND_MODE_DEFAULT ? BLEND_MODE_ALPHA : result.blend.mode;

	result.depth.enable = result.depth.enable == STATE_DEFAULT ? STATE_FALSE : result.depth.enable;
	result.depth.depth_func =
	    result.depth.depth_func == DEPTH_FUNC_DEFAULT ? DEPTH_FUNC_LEQUAL : result.depth.depth_func;

	result.rasterizer.cull_enable =
	    result.rasterizer.cull_enable == STATE_DEFAULT ? STATE_TRUE : result.rasterizer.cull_enable;
	result.rasterizer.cull_mode =
	    result.rasterizer.cull_mode == CULL_MODE_DEFAULT ? CULL_MODE_BACK : result.rasterizer.cull_mode;

	result.rasterizer.winding_mode = result.rasterizer.winding_mode == WINDING_MODE_DEFAULT
					     ? WINDING_MODE_COUNTER_CLOCK_WISE
					     : result.rasterizer.winding_mode;

	result.rasterizer.scissor =
	    result.rasterizer.scissor == STATE_DEFAULT ? STATE_FALSE : result.rasterizer.scissor;

	result.rasterizer.polygon_mode =
	    result.rasterizer.polygon_mode == POLYGON_MODE_DEFAULT ? POLYGON_MODE_FILL : result.rasterizer.polygon_mode;

	result.rasterizer.line_width = result.rasterizer.line_width == 0.0f ? 1.0f : result.rasterizer.line_width;

	// resolve vertex layout strides and offsets
	u32 auto_offset[MAX_VERTEX_BUFFERS] = {0};
	b32 use_auto_offset = 1;
	for (u32 attr_index = 0; attr_index < MAX_VERTEX_ATTRIBUTES; ++attr_index)
	{
		// to use computed offsets, *all* attr offsets must be 0
		if (result.layout.attrs[attr_index].offset != 0)
		{
			use_auto_offset = 0;
		}
	}

	for (u32 attr_index = 0; attr_index < MAX_VERTEX_ATTRIBUTES; ++attr_index)
	{
		struct vertex_attr_state *attr_state = &result.layout.attrs[attr_index];
		if (attr_state->format == VERTEX_FORMAT_INVALID)
		{
			break;
		}
		sm__assert(attr_state->buffer_index < MAX_VERTEX_BUFFERS);
		if (use_auto_offset)
		{
			attr_state->offset = auto_offset[attr_state->buffer_index];
		}
		auto_offset[attr_state->buffer_index] += gl__ctable_vertex_format_to_byte_size[attr_state->format];
	}

	// compute vertex strides if needed
	for (u32 buf_index = 0; buf_index < MAX_VERTEX_BUFFERS; ++buf_index)
	{
		struct vertex_buffer_layout_state *buffer_state = &result.layout.buffers[buf_index];
		if (buffer_state->stride == 0)
		{
			buffer_state->stride = auto_offset[buf_index];
		}
	}

	return (result);
}

struct renderer_pipeline *
renderer_pipeline_at(pipeline_handle handle)
{
	sm__assert(INVALID_HANDLE != handle.id);
	u32 slot_index = handle_index(handle.id);

	return &RC.pools.pipelines[slot_index];
}

pipeline_handle
renderer_pipeline_make(const struct renderer_pipeline_desc *desc)
{
	pipeline_handle result = sm__renderer_pipeline_alloc();
	if (result.id != INVALID_HANDLE)
	{
		struct renderer_pipeline *pipeline_at = renderer_pipeline_at(result);

		if (sm__renderer_pipeline_validate(desc))
		{
			struct renderer_pipeline_desc desc_def = sm__renderer_pipeline_defaults(desc);

			pipeline_at->slot.id = result.id;
			pipeline_at->label = desc_def.label;

			pipeline_at->shader = desc_def.shader;

			pipeline_at->depth = desc_def.depth;
			pipeline_at->blend = desc_def.blend;
			pipeline_at->rasterizer = desc_def.rasterizer;

			struct renderer_shader *shader_at = renderer_shader_at(desc_def.shader);

			for (u32 i = 0; i < MAX_VERTEX_ATTRIBUTES; ++i)
			{
				// struct gl__vertex_attr_desc *gl_attr = pipeline_at->attrs + i;
				// struct vertex_buffer_layout_state *buf_state = desc_def.layout.buffers + i;
				struct vertex_attr_state *attr_state = desc_def.layout.attrs + i;
				struct vertex_buffer_layout_state *buffer_state =
				    desc_def.layout.buffers + attr_state->buffer_index;

				if (attr_state->name.size > 0)
				{
					i32 loc =
					    sm__renderer_shader_get_attribute_slot(shader_at, attr_state->name, 1);
					sm__assert(loc < MAX_VERTEX_ATTRIBUTES);

					struct gl__vertex_attributes *gl_attr = pipeline_at->attrs + loc;
					gl_attr->stride = buffer_state->stride;
					gl_attr->offset = attr_state->offset;
					gl_attr->type = gl__ctable_vertex_format_type(attr_state->format);
					gl_attr->size =
					    (u8)gl__ctable_vertex_format_components_count[attr_state->format];
					gl_attr->normalized = gl__vertex_format_normalized(attr_state->format);
				}
			}
		}
	}

	return (result);
}

static void
sm__renderer_blend_apply(const struct blend_state state)
{
	struct blend_state current = RC.current.gl.blend;
	struct blend_state selected = state;

	sm__assert(current.enable != 0);
	sm__assert(current.mode != 0);

	if (current.enable != selected.enable)
	{
		if (selected.enable == STATE_FALSE)
		{
			if (current.enable == STATE_TRUE)
			{
				glCall(glDisable(GL_BLEND));
				RC.current.gl.blend.enable = selected.enable;
			}
		}
		else if (current.enable == STATE_TRUE)
		{
			if (current.enable == STATE_FALSE)
			{
				glCall(glEnable(GL_BLEND));
				RC.current.gl.blend.enable = selected.enable;
			}
		}
	}

	if (current.mode != selected.mode)
	{
		gl__renderer_pipeline_set_blend_mode(selected.mode);
		RC.current.gl.blend.mode = selected.mode;
	}
}

static void
sm__renderer_depth_apply(const struct depth_state state)
{
	struct depth_state current = RC.current.gl.depth;
	struct depth_state selected = state;
	sm__assert(current.enable != 0);
	sm__assert(current.depth_func != 0);

	if (current.enable != selected.enable)
	{
		if (selected.enable == STATE_FALSE)
		{
			if (current.enable == STATE_TRUE)
			{
				glCall(glDisable(GL_DEPTH_TEST));
				RC.current.gl.depth.enable = selected.enable;
			}
		}
		else if (selected.enable == STATE_TRUE)
		{
			if (current.enable == STATE_FALSE)
			{
				glCall(glEnable(GL_DEPTH_TEST));
				RC.current.gl.depth.enable = selected.enable;
			}
		}
	}

	if (current.depth_func != selected.depth_func)
	{
		static GLenum ctable_depth_func[DEPTH_FUNC_MAX] = {
		    [DEPTH_FUNC_NEVER] = GL_NEVER,
		    [DEPTH_FUNC_LESS] = GL_LESS,
		    [DEPTH_FUNC_EQUAL] = GL_EQUAL,
		    [DEPTH_FUNC_LEQUAL] = GL_LEQUAL,
		    [DEPTH_FUNC_GREATER] = GL_GREATER,
		    [DEPTH_FUNC_NOTEQUAL] = GL_NOTEQUAL,
		    [DEPTH_FUNC_GEQUAL] = GL_GEQUAL,
		    [DEPTH_FUNC_ALWAYS] = GL_ALWAYS,
		};

		glCall(glDepthFunc(ctable_depth_func[selected.depth_func]));
		RC.current.gl.depth.depth_func = selected.depth_func;
	}
}

static void
sm__renderer_rasterizer_apply(const struct rasterizer_state state)
{
	struct rasterizer_state current = RC.current.gl.rasterizer;
	struct rasterizer_state selected = state;

	sm__assert(current.cull_enable != 0);
	sm__assert(current.cull_mode != 0);
	sm__assert(current.winding_mode != 0);
	sm__assert(current.scissor != 0);
	sm__assert(current.polygon_mode != 0);
	sm__assert(current.line_width != 0);

	// Culling
	if (current.cull_enable != selected.cull_enable)
	{
		if (selected.cull_enable == STATE_FALSE)
		{
			if (current.cull_enable == STATE_TRUE)
			{
				glCall(glDisable(GL_CULL_FACE));
				RC.current.gl.rasterizer.cull_enable = selected.cull_enable;
			}
		}
		else if (selected.cull_enable == STATE_TRUE)
		{
			if (current.cull_enable == STATE_FALSE)
			{
				glCall(glEnable(GL_CULL_FACE));
				RC.current.gl.rasterizer.cull_enable = selected.cull_enable;
			}
		}
	}

	if (current.cull_mode != selected.cull_mode)
	{
		sm__assert(selected.cull_mode < CULL_MODE_MAX);
		static GLenum ctable_cull[CULL_MODE_MAX] = {
		    [CULL_MODE_FRONT] = GL_FRONT,
		    [CULL_MODE_BACK] = GL_BACK,
		    [CULL_MODE_FRONT_AND_BACK] = GL_FRONT_AND_BACK,
		};
		glCall(glCullFace(ctable_cull[selected.cull_mode]));
		RC.current.gl.rasterizer.cull_mode = selected.cull_mode;
	}

	// Winding mode
	if (current.winding_mode != selected.winding_mode)
	{
		sm__assert(selected.winding_mode < WINDING_MODE_MAX);
		static GLenum ctable_winding[WINDING_MODE_MAX] = {
		    [WINDING_MODE_CLOCK_WISE] = GL_CW,
		    [WINDING_MODE_COUNTER_CLOCK_WISE] = GL_CCW,
		};

		glCall(glFrontFace(ctable_winding[selected.winding_mode]));
		RC.current.gl.rasterizer.winding_mode = selected.winding_mode;
	}

	// Polygon mode
	if (current.polygon_mode != selected.polygon_mode)
	{
		sm__assert(selected.polygon_mode < POLYGON_MODE_MAX);
		static GLenum ctable_polygon[] = {
		    [POLYGON_MODE_POINT] = GL_POINT,
		    [POLYGON_MODE_LINE] = GL_LINE,
		    [POLYGON_MODE_FILL] = GL_FILL,
		};
		glCall(glPolygonMode(GL_FRONT_AND_BACK, ctable_polygon[selected.polygon_mode]));
		RC.current.gl.rasterizer.polygon_mode = selected.polygon_mode;
	}

	// Scissor
	if (current.scissor != selected.scissor)
	{
		if (selected.scissor == STATE_TRUE)
		{
			if (current.scissor == STATE_FALSE)
			{
				glCall(glEnable(GL_SCISSOR_TEST));
				RC.current.gl.rasterizer.scissor = selected.scissor;
			}
		}
		else if (selected.scissor == STATE_FALSE)
		{
			if (current.scissor == STATE_TRUE)
			{
				glCall(glDisable(GL_SCISSOR_TEST));
				RC.current.gl.rasterizer.scissor = selected.scissor;
			}
		}
	}

	// Line width
	if (current.line_width != selected.line_width)
	{
		glCall(glLineWidth(selected.line_width));
		RC.current.gl.rasterizer.line_width = selected.line_width;
	}
}

static void
sm__renderer_shader_apply(shader_handle shader)
{
	sm__assert(shader.id != INVALID_HANDLE);
	if (RC.current.shader.id != shader.id)
	{
		RC.current.shader = shader;

		struct renderer_shader *s = renderer_shader_at(shader);
		glCall(glUseProgram(s->gl_shader_program_handle));

		// for (u32 i = 0; i < s->uniforms_count; ++i)
		// {
		// 	struct shader_uniform *uni = s->uniforms + i;
		// 	{
		// 		if (uni->dirty)
		// 		{
		// 			gl__renderer_set_uniform(uni->location, uni->size, uni->type, uni->data);
		// 			uni->dirty = 0;
		// 		}
		// 	}
		// }
	}
}

void
renderer_pipiline_apply(pipeline_handle pipeline)
{
	if (RC.current.pipeline.id != pipeline.id)
	{
		RC.current.pipeline = pipeline;
		struct renderer_pipeline *pipe = renderer_pipeline_at(pipeline);

		sm__renderer_blend_apply(pipe->blend);
		sm__renderer_depth_apply(pipe->depth);
		sm__renderer_rasterizer_apply(pipe->rasterizer);
		sm__renderer_shader_apply(pipe->shader);
	}
}

void
renderer_shader_set_uniform(str8 name, void *value, u32 size, u32 count)
{
	sm__assert(count);

	struct renderer_shader *shader = renderer_shader_at(RC.current.shader);

	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		struct shader_uniform *uni = shader->uniforms + i;

		if (str8_eq(name, uni->name))
		{
			sm__assert(gl__ctable_type_size[uni->type] == size);
			sm__assert(count >= 1 && count <= uni->size);

			if (memcmp(uni->data, value, size * count))
			{
				memcpy(uni->data, value, size * count);
				// uni->dirty = 1;
			}
			return;
		}
	}

	log_error(str8_from("[{s}] constant not found: {s}"), shader->label, name);
}

static b32
sm__renderer_bindings_validate(struct renderer_bindings *bindings)
{
#if !defined(SM_DEBUG)
	return (1);
#else
	sm__assertf(bindings->_start_canary == 0, "renderer_bindings not initialized");
	sm__assertf(bindings->_end_canary == 0, "renderer_bindings not initialized");
	// a pipeline object must have been applied
	sm__assertf(RC.current.pipeline.id != INVALID_HANDLE, "current.pipeline.id not set");
	struct renderer_pipeline *pip = renderer_pipeline_at(RC.current.pipeline);
	sm__assertf(pip != 0, "renderer_apply_bindings: currently applied pipeline object no longer alive");
	sm__assertf(pip->shader.id != INVALID_HANDLE, "current.pipeline.shader not set");

	struct renderer_shader *shader = renderer_shader_at(pip->shader);

	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		struct shader_attribute *shad_attr = shader->attributes + i;

		b32 found = 0;
		for (u32 j = 0; j < MAX_BUFFER_SLOTS; ++j)
		{
			struct buffer_slot *abind = bindings->buffers + j;
			if (str8_eq(abind->name, shad_attr->name))
			{
				sm__assertf(abind->name.size > 0, "bindings->vertex_buffer.name not set");
				sm__assertf(
				    abind->buffer.id != INVALID_HANDLE, "bindings->vertex_buffer.buffer not set");
				struct renderer_buffer *buf = renderer_buffer_at(abind->buffer);
				sm__assertf(buf != 0, "bindings->vertex_buffers[i]: vertex buffer no longer alive");

				found = 1;
				break;
			}
		}
		sm__assertf(found, "bindings->vertex_buffer not found");
	}

	if (bindings->index_buffer.id != INVALID_HANDLE)
	{
		// buffer in index-buffer-slot must be of type BUFFERTYPE_INDEXBUFFER
		struct renderer_buffer *buf = renderer_buffer_at(bindings->index_buffer);
		sm__assertf(buf != 0, "bindings->index_buffer: index buffer no longer alive");
	}

	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		struct shader_uniform *shad_uni = shader->uniforms + i;

		b32 found = 0;
		for (u32 j = 0; j < MAX_UNIFORM_SLOTS; ++j)
		{
			struct uniform_const *abind = bindings->uniforms + j;
			if (str8_eq(abind->name, shad_uni->name))
			{
				sm__assertf(abind->name.size > 0, "bindings->uniform.name not set");
				sm__assertf(abind->data, "bindings->uniform.data not set");
				sm__assertf(abind->type > SHADER_TYPE_INVALID && abind->type < SHADER_TYPE_MAX,
				    "bindings->uniform.type invalid value");

				found = 1;
				break;
			}
		}
		sm__assertf(found, "bindings->uniform not found");
	}

	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		struct shader_sampler *shad_sampler = shader->samplers + i;

		b32 found = 0;
		for (u32 j = 0; j < MAX_TEXTURE_SLOTS; ++j)
		{
			struct texture_slot *bind = bindings->textures + j;
			if (str8_eq(bind->name, shad_sampler->name))
			{
				sm__assertf(bind->name.size > 0, "bindings->texture.name not set");
				sm__assertf(bind->texture.id != INVALID_HANDLE, "bindings->texture.texture not set");
				sm__assertf(bind->sampler.id != INVALID_HANDLE, "bindings->texture.sampler not set");
				found = 1;
				break;
			}
		}
		sm__assertf(found, "bindings->texture not found");
	}

	return 1;
#endif
}

static struct renderer_bindings
sm__renderer_bindings_defaults(const struct renderer_bindings *desc)
{
	struct renderer_bindings result;

	result = *desc;

	for (u32 i = 0; i < MAX_UNIFORM_SLOTS; ++i)
	{
		struct uniform_const *ubind = result.uniforms + i;
		ubind->count = (ubind->count == 0) ? 1 : ubind->count;
	}

	return (result);
}

void
renderer_bindings_apply(struct renderer_bindings *bindings)
{
	sm__assert(bindings);

	if (!sm__renderer_bindings_validate(bindings))
	{
		return;
	}

	struct renderer_bindings bind = sm__renderer_bindings_defaults(bindings);

	struct renderer_shader *current_shd = renderer_shader_at(RC.current.shader);
	struct renderer_pipeline *current_pip = renderer_pipeline_at(RC.current.pipeline);

	struct texture_slot rearranged_textures[MAX_TEXTURE_SLOTS] = {0};
	for (u32 slot = 0; slot < MAX_TEXTURE_SLOTS; ++slot)
	{
		struct texture_slot *unit = bind.textures + slot;
		if (unit->name.size == 0)
		{
			continue;
		}
		i32 loc = sm__renderer_shader_get_sampler_slot(current_shd, unit->name, 1);
		sm__assert(loc < MAX_TEXTURE_SLOTS);

		rearranged_textures[loc] = *unit;
	}

	for (u32 slot = 0; slot < MAX_TEXTURE_SLOTS; ++slot)
	{
		struct texture_slot *selected = rearranged_textures + slot;
		struct texture_slot *current = RC.current.gl.textures + slot;

		if (selected->texture.id != current->texture.id)
		{
			glCall(glActiveTexture(GL_TEXTURE0 + slot));
			if (selected->texture.id == INVALID_HANDLE)
			{
				glCall(glDisable(GL_TEXTURE_2D));
				glCall(glBindTexture(GL_TEXTURE_2D, 0));
			}
			else
			{
				if (current->texture.id == INVALID_HANDLE)
				{
					glCall(glEnable(GL_TEXTURE_2D));
				}
				struct renderer_texture *texture_at = renderer_texture_at(selected->texture);
				glCall(glBindTexture(GL_TEXTURE_2D, texture_at->gl_handle));
			}

			current->texture = selected->texture;
		}

		if (selected->sampler.id != current->sampler.id)
		{
			if (selected->sampler.id == INVALID_HANDLE)
			{
				glCall(glBindSampler(slot, 0));
			}
			else
			{
				struct renderer_sampler *sampler_at = renderer_sampler_at(selected->sampler);
				glCall(glBindSampler(slot, sampler_at->gl_handle));
			}

			current->sampler = selected->sampler;
		}

		if (!str8_eq(selected->name, current->name))
		{
			// TODO: must copy?
			current->name = selected->name;
		}
	}

	struct buffer_slot rearranged_buffers[MAX_BUFFER_SLOTS] = {0};
	for (u32 i = 0; i < MAX_BUFFER_SLOTS; ++i)
	{
		struct buffer_slot *buf = bind.buffers + i;
		if (buf->name.size == 0)
		{
			continue;
		}
		i32 loc = sm__renderer_shader_get_attribute_slot(current_shd, buf->name, 1);
		sm__assert(loc < MAX_BUFFER_SLOTS);

		rearranged_buffers[loc] = *buf;
	}

	for (u32 slot = 0; slot < MAX_BUFFER_SLOTS; ++slot)
	{
		struct buffer_slot *selected = rearranged_buffers + slot;
		struct buffer_slot *current = RC.current.gl.buffers + slot;
		const struct gl__vertex_attributes *gl_attr = current_pip->attrs + slot;

		if (selected->buffer.id != current->buffer.id)
		{
			if (selected->buffer.id == INVALID_HANDLE)
			{
				glCall(glDisableVertexAttribArray(slot));
			}
			else
			{
				if (current->buffer.id == INVALID_HANDLE)
				{
					glCall(glEnableVertexAttribArray(slot));
				}
				struct renderer_buffer *buf = renderer_buffer_at(selected->buffer);
				sm__renderer_buffer_bind(GL_ARRAY_BUFFER, buf->gl_handle);
				glCall(glVertexAttribPointer(slot, (GLint)gl_attr->size, (GLenum)gl_attr->type,
				    (GLboolean)gl_attr->normalized, gl_attr->stride,
				    (const GLvoid *)(GLintptr)gl_attr->offset));
			}

			current->buffer = selected->buffer;
		}

		if (!str8_eq(selected->name, current->name))
		{
			current->name = selected->name;
		}
	}

	if (RC.current.gl.index_buffer.id != bind.index_buffer.id)
	{
		if (bind.index_buffer.id != INVALID_HANDLE)
		{
			struct renderer_buffer *buf = renderer_buffer_at(bind.index_buffer);
			// glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->gl_handle));
			sm__renderer_buffer_bind(GL_ELEMENT_ARRAY_BUFFER, buf->gl_handle);

			RC.current.gl.index_buffer = bind.index_buffer;
		}
		else
		{
			if (RC.current.gl.index_buffer.id != INVALID_HANDLE)
			{
				// glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
				sm__renderer_buffer_bind(GL_ELEMENT_ARRAY_BUFFER, 0);
			}
			RC.current.gl.index_buffer.id = INVALID_HANDLE;
		}
	}

	for (u32 i = 0; i < current_shd->uniforms_count; ++i)
	{
		struct shader_uniform *current = current_shd->uniforms + i;

		b32 found = 0;
		for (u32 u = 0; u < MAX_UNIFORM_SLOTS; ++u)
		{
			struct uniform_const *selected = bind.uniforms + u;
			if (str8_eq(selected->name, current->name))
			{
				u32 bs = gl__ctable_type_size[selected->type];
				sm__assert(selected->count >= 1 && selected->count <= current->size);
				sm__assert(selected->type == current->type);

				if (memcmp(current->data, selected->data, bs * selected->count))
				{
					memcpy(current->data, selected->data, bs * selected->count);
					gl__renderer_set_uniform(
					    current->location, current->size, current->type, current->data);
				}

				found = 1;
				break;
			}
		}

		sm__assertf(found, "uniform not found in shader");
	}
}

static void
sm__before_draw(void)
{
	// clang-format off
	log_trace(str8_from("* shader  : {u3d}"), RC.current.shader.id);
	log_trace(str8_from("* pipeline: {u3d}"), RC.current.pipeline.id);
	log_trace(str8_from("* pass    : {u3d}"), RC.current.pass.id);
	log_trace(str8_from("	- pass width  : {u3d}"), RC.current.pass_width);
	log_trace(str8_from("	- pass height : {u3d}"), RC.current.pass_height);
	log_trace(str8_from("	- enable      : {b}"), (b8)RC.current.in_pass);
	log_trace(str8_from("* gl      :"));
	log_trace(str8_from("	- default_vao : {u3d}"), RC.current.gl.default_vao);
	log_trace(str8_from("	- default_vbo : {u3d}"), RC.current.gl.default_vbo);
	log_trace(str8_from("	- default_fbo : {u3d}"), RC.current.gl.default_fbo);
	log_trace(str8_from("	* blend       :"));
	log_trace(str8_from("		- enable      : {b}"), state_bool_to_b8(RC.current.gl.blend.enable));
	log_trace(str8_from("		- mode        : {s}"), sm__ctable_blend_mode_str8[RC.current.gl.blend.mode]);
	log_trace(str8_from("	* depth       :"));
	log_trace(str8_from("		- enable      : {b}"), state_bool_to_b8(RC.current.gl.depth.enable));
	log_trace(str8_from("		- depth_func  : {s}"), sm__ctable_depth_func_str8[RC.current.gl.depth.depth_func]);
	log_trace(str8_from("	* rasterazer  :"));
	log_trace(str8_from("		- scissor     : {b}"), state_bool_to_b8(RC.current.gl.rasterizer.scissor));
	log_trace(str8_from("		- line_width  : {f}"), RC.current.gl.rasterizer.line_width);
	log_trace(str8_from("		- cull_enable : {b}"), state_bool_to_b8(RC.current.gl.rasterizer.cull_enable));
	log_trace(str8_from("		- cull_enable : {s}"), sm__ctable_cull_mode_str8[RC.current.gl.rasterizer.cull_mode]);
	log_trace(str8_from("		- winding_mode: {s}"), sm__ctable_winding_mode_str8[RC.current.gl.rasterizer.winding_mode]);
	log_trace(str8_from("		- polygon_mode: {s}"), sm__cable_polygon_mode_str8[RC.current.gl.rasterizer.polygon_mode]);

	log_trace(str8_from("	* textures    :"));
	for (u32 i = 0; i < MAX_TEXTURE_SLOTS; ++i)
	{
	struct texture_slot *slot = RC.current.gl.textures +i;
	if (slot->name.size == 0)
	{
		log_trace(str8_from("		- [{u3d}] EMPTY"), i);
	}
	else
	{
		log_trace(str8_from("		- [{u3d}] {s}, texture: {u3d}, sampler: {u3d}"), i, slot->name, slot->texture.id, slot->sampler.id);
	}
	}


	log_trace(str8_from("	* buffers     :"));
	for (u32 i = 0; i < MAX_TEXTURE_SLOTS; ++i)
	{
	struct buffer_slot *slot = RC.current.gl.buffers +i;
	if (slot->name.size == 0)
	{
		log_trace(str8_from("		- [{u3d}] EMPTY"), i);
	}
	else
	{
		log_trace(str8_from("		- [{u3d}] {s}, buffer: {u3d}"), i, slot->name, slot->buffer.id);
	}
	}
	log_trace(str8_from("	* index buffer: "));
	log_trace(str8_from("		- [0] {u3d}"), RC.current.gl.index_buffer.id);

	struct renderer_pipeline *pip = renderer_pipeline_at(RC.current.pipeline);

	log_trace(str8_from("	* vertex attr :"));
	for (u32 i = 0; i < MAX_TEXTURE_SLOTS; ++i)
	{
		struct gl__vertex_attributes *attr = pip->attrs + i;
		log_trace(str8_from("		- [{u3d}] size: {u8d}, type: {u32}, norm: {b}, offset: {i3d}"), i, attr->size, attr->type, (b8)attr->normalized, attr->offset);
	}

	// clang-format on
}

void
renderer_draw(u32 num_elements)
{
	// sm__before_draw();
	if (RC.current.gl.index_buffer.id != INVALID_HANDLE)
	{
		glCall(glDrawElements(GL_TRIANGLES, num_elements, GL_UNSIGNED_INT, 0));
	}
	else
	{
		glCall(glDrawArrays(GL_TRIANGLES, 0, num_elements));
	}
}

static void
sm__renderer_default_state(void)
{
	// Blending mode
	RC.current.gl.blend = (struct blend_state){.enable = STATE_FALSE, .mode = BLEND_MODE_ALPHA};
	glCall(glDisable(GL_BLEND));
	gl__renderer_pipeline_set_blend_mode(RC.current.gl.blend.mode);

	// Depth testing
	RC.current.gl.depth = (struct depth_state){.enable = STATE_FALSE, .depth_func = DEPTH_FUNC_LEQUAL};
	glCall(glDisable(GL_DEPTH_TEST));
	glCall(glDepthFunc(GL_LEQUAL));

	// Rasterizer
	RC.current.gl.rasterizer = (struct rasterizer_state){
	    .cull_enable = STATE_TRUE,
	    .cull_mode = CULL_MODE_BACK,

	    .winding_mode = WINDING_MODE_COUNTER_CLOCK_WISE,

	    .scissor = STATE_FALSE,
	    .polygon_mode = POLYGON_MODE_FILL,
	    .line_width = 1.0f,
	};
	glCall(glEnable(GL_CULL_FACE));
	glCall(glCullFace(GL_BACK)); // Cull the back face

	glCall(glFrontFace(GL_CCW)); // front face are defined counter clockwise (default)

	glCall(glDisable(GL_SCISSOR_TEST));
	glCall(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
	glCall(glLineWidth(1.0f));
}

b32
renderer_init(u32 framebuffer_width, u32 framebuffer_height)
{
	struct buf renderer_base_memory = base_memory_reserve(MB(1));
	arena_make(&RC.arena, renderer_base_memory);
	arena_validate(&RC.arena);

	// sm__renderer_batch_make(&RC.arena, &RC.batch);
	sm__renderer_pools_make(&RC.arena, &RC.pools);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&RC.current.gl.default_fbo);
	RC.width = framebuffer_width;
	RC.height = framebuffer_height;

	sm__renderer_default_state();
	glCall(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	glCall(glClearDepth(1.0));
	glCall(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

	return (1);
}

void
renderer_teardown(void)
{
	// sm__framebuffer_release(&RC.framebuffer);
	// sm__batch_release(&RC.arena, &RC.batch);
}

#if 0
void
draw_rectangle(v2 position, v2 wh, f32 rotation, color c)
{
	v2 origin = v2_new(wh.width / 2, wh.height / 2);
	v2 top_left, top_right, bottom_left, bottom_right;

	if (rotation == 0.0f)
	{
		f32 x = position.x - origin.x;
		f32 y = position.y - origin.y;
		top_left = v2_new(x, y);
		top_right = v2_new(x + wh.x, y);
		bottom_left = v2_new(x, y + wh.y);
		bottom_right = v2_new(x + wh.x, y + wh.y);
	}
	else
	{
		f32 sin_rotation = sinf(glm_rad(rotation));
		f32 cos_rotation = cosf(glm_rad(rotation));

		f32 x = position.x;
		f32 y = position.y;

		f32 dx = -origin.x;
		f32 dy = -origin.y;

		top_left.x = x + dx * cos_rotation - dy * sin_rotation;
		top_left.y = y + dx * sin_rotation + dy * cos_rotation;

		top_right.x = x + (dx + wh.width) * cos_rotation - dy * sin_rotation;
		top_right.y = y + (dx + wh.width) * sin_rotation + dy * cos_rotation;

		bottom_left.x = x + dx * cos_rotation - (dy + wh.height) * sin_rotation;
		bottom_left.y = y + dx * sin_rotation + (dy + wh.height) * cos_rotation;

		bottom_right.x = x + (dx + wh.width) * cos_rotation - (dy + wh.height) * sin_rotation;
		bottom_right.y = y + (dx + wh.width) * sin_rotation + (dy + wh.height) * cos_rotation;
	}
	sm__batch_overflow(4);

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
}

void
draw_rectangle_uv(v2 position, v2 wh, v2 uv[4], b8 vertical_flip, color c)
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
	sm__batch_overflow(4);
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
}

void
draw_circle(v2 center, f32 radius, f32 start_angle, f32 end_angle, u32 segments, color c)
{
	if (radius <= 0.0f) { radius = 0.1f; } // Avoid div by zero

	// Function expects (endAngle > startAngle)
	if (end_angle < start_angle) { glm_swapf(&start_angle, &end_angle); }

	u32 min_segments = (u32)ceilf((end_angle - start_angle) / 90.0f);
	if (segments < min_segments)
	{
		// Calculate the maximum angle between segments based on the
		// error rate (usually 0.5f)
		f32 th = acosf(2 * powf(1 - 0.5f / radius, 2) - 1);
		segments = (u32)((end_angle - start_angle) * ceilf(2 * GLM_PIf / th) / 360.0f);
		if (segments <= 0) segments = min_segments;
	}
	f32 step_length = (end_angle - start_angle) / (f32)segments;
	f32 angle = start_angle;
	sm__batch_overflow(4 * segments / 2);

	sm__renderer_begin(GL_QUADS);
	{
		v2 uv_coords[] = {
		    v2_new(0.000000, 0.000000),
		    v2_new(1.000000, 0.000000),
		    v2_new(1.000000, 1.000000),
		    v2_new(0.000000, 1.000000),
		};
		// NOTE: Every QUAD actually represents two segments
		for (u32 i = 0; i < segments / 2; ++i)
		{
			sm__color(c);
			sm__uv_v2(uv_coords[0]);
			sm__position_2f(center.x, center.y);

			sm__uv_v2(uv_coords[1]);
			sm__position_2f(center.x + cosf(glm_rad(angle + step_length * 2.0f)) * radius,
			    center.y + sinf(glm_rad(angle + step_length * 2.0f)) * radius);

			sm__uv_v2(uv_coords[2]);
			sm__position_2f(center.x + cosf(glm_rad(angle + step_length)) * radius,
			    center.y + sinf(glm_rad(angle + step_length)) * radius);

			sm__uv_v2(uv_coords[3]);
			sm__position_2f(
			    center.x + cosf(glm_rad(angle)) * radius, center.y + sinf(glm_rad(angle)) * radius);

			angle += (step_length * 2.0f);
		}

		// NOTE: In case number of segments is odd, we add one last piece to the cake
		if (segments % 2)
		{
			sm__color(c);

			sm__uv_v2(uv_coords[0]);
			sm__position_2f(center.x, center.y);

			sm__uv_v2(uv_coords[1]);
			sm__position_2f(center.x + cosf(glm_rad(angle + step_length)) * radius,
			    center.y + sinf(glm_rad(angle + step_length)) * radius);

			sm__uv_v2(uv_coords[2]);
			sm__position_2f(
			    center.x + cosf(glm_rad(angle)) * radius, center.y + sinf(glm_rad(angle)) * radius);

			sm__uv_v2(uv_coords[3]);
			sm__position_2f(center.x, center.y);
		}
	}
	sm__renderer_end();
}

enum
{
	ATLAS_WIDTH = 128,
	ATLAS_HEIGHT = 128
};

#	include "renderer/toshibasat8x8.h"

static u32
sm__get_sprite_index(struct atlas_sprite *sprite_desc, u32 sprite_count, i32 char_value)
{
	u32 result = 0;

	for (u32 i = 0; i < sprite_count; ++i)
	{
		if (char_value == sprite_desc[i].charValue)
		{
			result = i;
			break;
		}
	}

	return (result);
}

void
draw_text(str8 text, v2 pos, i32 font_size, color c)
{
	sm__batch_overflow(4 * text.size);

	struct resource *resource = resource_get_by_label(str8_from("toshibasat8x8"));
	sm__assert(resource && resource->type == RESOURCE_IMAGE && resource->image_data);
	struct image_resource *image = resource->image_data;

	if (image->texture_handle == 0) { renderer_upload_texture(image); }

	u32 win_width = RC.framebuffer.width;

	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);

		f32 advance_x = 0;
		f32 advance_y = 0;
		for (const i8 *p = text.idata; *p; ++p)
		{
			if ((*p & 0xc0) == 0x80) { continue; }
			i32 chr = MIN(*p, 127);

			struct atlas_sprite sprite =
			    desc_atlas[sm__get_sprite_index(desc_atlas, ATLAS_ATLAS_SPRITE_COUNT, chr)];

			v2 origin = v2_zero();

			f32 scale = (f32)font_size / ATLAS_ATLAS_FONT_SIZE;

			f32 x = (pos.x - origin.x) + advance_x + (f32)sprite.charOffsetX;
			f32 y = (pos.y - origin.y) + advance_y + (f32)sprite.charOffsetY;

			f32 w = (f32)sprite.sourceWidth * scale;
			f32 h = (f32)sprite.sourceHeight * scale;

			v2 dest_top_left = v2_new(x, y);
			v2 dest_top_right = v2_new((x + w), y);
			v2 dest_bottom_left = v2_new(x, (y + h));
			v2 dest_bottom_right = v2_new((x + w), (y + h));

			if (sprite.charAdvanceX == 0) { advance_x += (f32)sprite.sourceWidth * scale; }
			else { advance_x += (f32)sprite.charAdvanceX * scale; }
			if (advance_x + 16 * scale >= win_width)
			{
				advance_x = 0;
				advance_y += 16 * scale;
			}

			f32 texture_x = (f32)sprite.positionX / (f32)ATLAS_WIDTH;
			f32 texture_y = (f32)sprite.positionY / (f32)ATLAS_HEIGHT;
			f32 texture_w = (f32)sprite.sourceWidth / (f32)ATLAS_WIDTH;
			f32 texture_h = (f32)sprite.sourceHeight / (f32)ATLAS_HEIGHT;

			sm__uv_v2(v2_new(texture_x, texture_y));
			sm__position_v2(dest_top_left);

			sm__uv_v2(v2_new(texture_x, texture_y + texture_h));
			sm__position_v2(dest_bottom_left);

			sm__uv_v2(v2_new(texture_x + texture_w, texture_y + texture_h));
			sm__position_v2(dest_bottom_right);

			sm__uv_v2(v2_new(texture_x + texture_w, texture_y));
			sm__position_v2(dest_top_right);
		}
	}
	sm__renderer_end();
}

// void
// draw_text_billboard_3d(v3 camp_position, str8 text, v3 pos, f32 font_size, color c)
// {
// 	sm__batch_overflow(4 * text.size);
//
// 	struct resource *resource = resource_get_by_name(str8_from("toshibasat8x8"));
// 	sm__assert(resource && resource->type == RESOURCE_IMAGE && resource->image_data);
// 	struct image_resource *image = resource->image_data;
//
// 	if (image->texture_handle == 0) { renderer_upload_texture(image); }
// 	sm__renderer_set_texture(image->texture_handle);
//
// 	sm__renderer_m4_push();
//
// 	trs t = trs_identity();
// 	t.position.v3 = pos;
// 	glm_quat_forp(t.position.v3.data, camp_position.data, v3_up().data, t.rotation.data);
//
// 	sm__renderer_trs_mul(t);
//
// 	sm__renderer_begin(GL_QUADS);
// 	{
// 		sm__color(c);
//
// 		f32 advance_x = 0;
// 		for (const i8 *p = text.idata; *p; ++p)
// 		{
// 			if ((*p & 0xc0) == 0x80) { continue; }
// 			i32 chr = MIN(*p, 127);
//
// 			struct atlas_sprite sprite =
// 			    desc_atlas[sm___get_sprite_index(desc_atlas, ATLAS_ATLAS_SPRITE_COUNT, chr)];
//
// 			// v2 origin = v2_zero();
//
// 			f32 scale = (f32)font_size / ATLAS_ATLAS_FONT_SIZE;
//
// 			f32 x = advance_x + (f32)sprite.charOffsetX * scale;
// 			f32 y = sprite.charOffsetY * scale;
//
// 			f32 w = (f32)sprite.sourceWidth * scale;
// 			f32 h = (f32)sprite.sourceHeight * scale;
//
// 			v2 dest_top_left = v2_new(x, y);
// 			v2 dest_bottom_left = v2_new(x, (y + h));
// 			v2 dest_bottom_right = v2_new((x + w), (y + h));
// 			v2 dest_top_right = v2_new((x + w), y);
//
// 			if (sprite.charAdvanceX == 0) { advance_x += (f32)sprite.sourceWidth * scale; }
// 			else { advance_x += (f32)sprite.charAdvanceX * scale; }
//
// 			f32 texture_x = (f32)sprite.positionX / (f32)ATLAS_WIDTH;
// 			f32 texture_y = (f32)sprite.positionY / (f32)ATLAS_HEIGHT;
// 			f32 texture_w = (f32)sprite.sourceWidth / (f32)ATLAS_WIDTH;
// 			f32 texture_h = (f32)sprite.sourceHeight / (f32)ATLAS_HEIGHT;
//
// 			sm__uv_v2(v2_new(texture_x, texture_y));
// 			sm__position_v2(dest_top_left);
//
// 			sm__uv_v2(v2_new(texture_x, texture_y + texture_h));
// 			sm__position_v2(dest_bottom_left);
//
// 			sm__uv_v2(v2_new(texture_x + texture_w, texture_y + texture_h));
// 			sm__position_v2(dest_bottom_right);
//
// 			sm__uv_v2(v2_new(texture_x + texture_w, texture_y));
// 			sm__position_v2(dest_top_right);
// 		}
// 	}
// 	sm__renderer_end();
// 	sm__renderer_set_texture(0);
// 	sm__renderer_m4_pop();
// }

// void
// draw_text_billboard_3d(camera_component *camera, str8 text, f32 font_size, color c)
// {
// 	sm__batch_overflow(4 * text.size);
//
// 	struct resource *resource = resource_get_by_name(str8_from("toshibasat8x8"));
// 	sm__assert(resource && resource->type == RESOURCE_IMAGE && resource->image_data);
// 	struct image_resource *image = resource->image_data;
//
// 	if (image->texture_handle == 0) { renderer_upload_texture(image); }
//
// 	sm__m4_push();
//
// 	// trs t = trs_identity();
// 	// t.position.v3 = pos;
// 	// glm_quat_forp(t.position.v3.data, camp_position.data, v3_up().data, t.rotation.data);
//
// 	m4 view = camera_get_view(camera);
// 	sm__m4_mul(view.data);
//
// 	// sm__renderer_trs_mul(t);
//
// 	sm__renderer_begin(GL_QUADS);
// 	{
// 		sm__color(c);
//
// 		f32 advance_x = 0;
// 		for (const i8 *p = text.idata; *p; ++p)
// 		{
// 			if ((*p & 0xc0) == 0x80) { continue; }
// 			i32 chr = MIN(*p, 127);
//
// 			struct atlas_sprite sprite =
// 			    desc_atlas[sm__get_sprite_index(desc_atlas, ATLAS_ATLAS_SPRITE_COUNT, chr)];
//
// 			// v2 origin = v2_zero();
//
// 			f32 scale = (f32)font_size / ATLAS_ATLAS_FONT_SIZE;
//
// 			f32 x = advance_x + (f32)sprite.charOffsetX * scale;
// 			f32 y = sprite.charOffsetY * scale;
//
// 			f32 w = (f32)sprite.sourceWidth * scale;
// 			f32 h = (f32)sprite.sourceHeight * scale;
//
// 			v2 dest_top_left = v2_new(x, y);
// 			v2 dest_bottom_left = v2_new(x, (y + h));
// 			v2 dest_bottom_right = v2_new((x + w), (y + h));
// 			v2 dest_top_right = v2_new((x + w), y);
//
// 			if (sprite.charAdvanceX == 0) { advance_x += (f32)sprite.sourceWidth * scale; }
// 			else { advance_x += (f32)sprite.charAdvanceX * scale; }
//
// 			f32 texture_x = (f32)sprite.positionX / (f32)ATLAS_WIDTH;
// 			f32 texture_y = (f32)sprite.positionY / (f32)ATLAS_HEIGHT;
// 			f32 texture_w = (f32)sprite.sourceWidth / (f32)ATLAS_WIDTH;
// 			f32 texture_h = (f32)sprite.sourceHeight / (f32)ATLAS_HEIGHT;
//
// 			sm__uv_v2(v2_new(texture_x, texture_y));
// 			sm__position_v2(dest_top_left);
//
// 			sm__uv_v2(v2_new(texture_x, texture_y + texture_h));
// 			sm__position_v2(dest_bottom_left);
//
// 			sm__uv_v2(v2_new(texture_x + texture_w, texture_y + texture_h));
// 			sm__position_v2(dest_bottom_right);
//
// 			sm__uv_v2(v2_new(texture_x + texture_w, texture_y));
// 			sm__position_v2(dest_top_right);
// 		}
// 	}
// 	sm__renderer_end();
// 	sm__m4_pop();
// }

void
draw_grid(i32 slices, f32 spacing)
{
	i32 half_slices = slices / 2;

	sm__batch_overflow((u32)(slices + 2) * 4);

	sm__renderer_begin(GL_LINES);
	for (int i = -half_slices; i <= half_slices; ++i)
	{
		if (i == 0) { sm__color_v4(v4_new(0.5f, 0.5f, 0.5f, 1.0f)); }
		else { sm__color_v4(v4_new(0.75f, 0.75f, 0.75f, 1.0f)); }
		sm__position_v3(v3_new((float)i * spacing, 0.0f, (float)-half_slices * spacing));
		sm__position_v3(v3_new((float)i * spacing, 0.0f, (float)half_slices * spacing));
		sm__position_v3(v3_new((float)-half_slices * spacing, 0.0f, (float)i * spacing));
		sm__position_v3(v3_new((float)half_slices * spacing, 0.0f, (float)i * spacing));
	}
	sm__renderer_end();
}

void
draw_sphere(trs t, u32 rings, u32 slices, color c)
{
	u32 num_vertex = (rings + 2) * slices * 6;
	sm__batch_overflow(num_vertex);

	sm__m4_push();

	sm__renderer_trs_mul(t);

	sm__renderer_begin(GL_TRIANGLES);
	{
		sm__color(c);
		f32 frings = (f32)rings;
		f32 fslices = (f32)slices;
		for (f32 i = 0; i < (frings + 2); ++i)
		{
			for (f32 j = 0; j < fslices; ++j)
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
				v3 v_1 = v3_new(v1_x, v1_y, v1_z);
				v3 v_2 = v3_new(v2_x, v2_y, v2_z);
				v3 v_3 = v3_new(v3_x, v3_y, v3_z);
				sm__position_v3(v_1);
				sm__position_v3(v_2);
				sm__position_v3(v_3);
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
				v_1 = v3_new(v1_x, v1_y, v1_z);
				v_2 = v3_new(v2_x, v2_y, v2_z);
				v_3 = v3_new(v3_x, v3_y, v3_z);
				sm__position_v3(v_1);
				sm__position_v3(v_2);
				sm__position_v3(v_3);
			}
		}
	}
	sm__renderer_end();

	sm__m4_pop();
}

void
draw_line_3D(v3 start_pos, v3 end_pos, color c)
{
	// WARNING: Be careful with internal buffer vertex alignment
	// when using GL_LINES or GL_TRIANGLES, data is aligned to fit
	// lines-triangles-quads in the same indexed buffers!!!
	sm__batch_overflow(8);
	sm__renderer_begin(GL_LINES);
	{
		sm__color(c);
		sm__position_v3(start_pos);
		sm__position_v3(end_pos);
	}
	sm__renderer_end();
}

// Draw cube
// NOTE: Cube position is the center position
void
draw_cube(trs t, color c)
{
	f32 x = 0.0f;
	f32 y = 0.0f;
	f32 z = 0.0f;

	sm__m4_push();

	// NOTE: Transformation is applied in inverse order (scale -> rotate -> translate)
	sm__renderer_translate(t.translation.v3);
	sm__renderer_rotate(t.rotation);
	// sm__renderer_scale(t.scale);   // NOTE: Vertices are directly scaled on definition

	sm__renderer_begin(GL_TRIANGLES);

	sm__color(c);

	// Front face
	sm__color(color_from_hex(0x0000FFFF));
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Left
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Right
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left

	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Right
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Right

	// Back face
	sm__color(color_from_hex(0x0000FFFF));
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Left
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Left
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right

	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Right
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Left

	// Top face
	sm__color(color_from_hex(0x00FF00FF));
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Left
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Bottom Left
	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Bottom Right

	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Right
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Left
	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Bottom Right

	// Bottom face
	sm__color(color_from_hex(0x00FF00FF));
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Top Left
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Right
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Left

	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Top Right
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Right
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Top Left

	// Right face
	sm__color(color_from_hex(0xFF0000FF));
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right
	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Right
	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left

	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Left
	sm__position_3f(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right
	sm__position_3f(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left

	// Left face
	sm__color(color_from_hex(0xFF0000FF));
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2); // Top Right

	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2); // Bottom Left
	sm__position_3f(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2); // Top Left
	sm__position_3f(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2); // Bottom Right

	sm__renderer_end();
	sm__m4_pop();
}

void
draw_cube_wires(trs t, color c)
{
	f32 x = 0.0f;
	f32 y = 0.0f;
	f32 z = 0.0f;

	sm__batch_overflow(36);
	sm__m4_push();

	sm__renderer_translate(t.translation.v3);
	sm__renderer_rotate(t.rotation);

	sm__renderer_begin(GL_LINES);
	{
		sm__color(c);
		// Front face
		// ----------------------------------------------------- Bottom
		// line
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));

		// Left line
		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));

		// Top line
		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));

		// Right line
		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));

		// Back face
		// ------------------------------------------------------ Bottom
		// line
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));
		// Left line

		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		// Top line

		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		// Right line

		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));
		// Top face
		// ------------------------------------------------------- Left
		// line
		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		// Right line

		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y + t.S.height / 2, z - t.S.length / 2));
		// Bottom face
		// --------------------------------------------------- Left line
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x - t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));

		//_ Right line
		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z + t.S.length / 2));
		sm__position_v3(v3_new(x + t.S.width / 2, y - t.S.height / 2, z - t.S.length / 2));
	}
	sm__renderer_end();
	sm__m4_pop();
}

void
draw_aabb(struct aabb box, color c)
{
	trs t = trs_identity();

	v3 dim = {0};
	dim.x = fabsf(box.max.x - box.min.x);
	dim.y = fabsf(box.max.y - box.min.y);
	dim.z = fabsf(box.max.z - box.min.z);

	t.translation.v3 = v3_new(box.min.x + dim.x / 2.0f, box.min.y + dim.y / 2.0f, box.min.z + dim.z / 2.0f);
	t.scale = dim;

	draw_cube_wires(t, c);
}

// Draw capsule wires with the center of its sphere caps at startPos and endPos
void
draw_capsule_wires(v3 start_pos, v3 end_pos, f32 radius, u32 slices, u32 rings, color c)
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

	f32 base_slice_angle = (2.0f * GLM_PIf) / (f32)slices;
	f32 base_ring_angle = GLM_PIf * 0.5f / (f32)rings;

	sm__renderer_begin(GL_LINES);
	sm__color(c);

	// render both caps
	u32 caps_count = 0;
caps:
	for (u32 ring = 0; ring < rings; ++ring)
	{
		for (u32 slice = 0; slice < slices; ++slice)
		{
			const f32 i = (f32)ring;
			const f32 j = (f32)slice;
			// we build up the rings from capCenter in the direction of the 'direction' vector
			// we computed earlier

			// as we iterate through the rings they must be placed higher above the center, the
			// height we need is sin(angle(i)) as we iterate through the rings they must get
			// smaller by the cos(angle(i))

			// compute the four vertices
			f32 ring_sin1 = sinf(base_slice_angle * (j)) * cosf(base_ring_angle * (i + 0));
			f32 ring_cos1 = cosf(base_slice_angle * (j)) * cosf(base_ring_angle * (i + 0));

			f32 x_1 =
			    cap_center.x +
			    (sinf(base_ring_angle * (i + 0)) * b0.x + ring_sin1 * b1.x + ring_cos1 * b2.x) * radius;
			f32 y_1 =
			    cap_center.y +
			    (sinf(base_ring_angle * (i + 0)) * b0.y + ring_sin1 * b1.y + ring_cos1 * b2.y) * radius;
			f32 z_1 =
			    cap_center.z +
			    (sinf(base_ring_angle * (i + 0)) * b0.z + ring_sin1 * b1.z + ring_cos1 * b2.z) * radius;
			v3 w1 = v3_new(x_1, y_1, z_1);

			f32 ring_sin2 = sinf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 0));
			f32 ring_cos2 = cosf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 0));

			f32 x_2 =
			    cap_center.x +
			    (sinf(base_ring_angle * (i + 0)) * b0.x + ring_sin2 * b1.x + ring_cos2 * b2.x) * radius;
			f32 y_2 =
			    cap_center.y +
			    (sinf(base_ring_angle * (i + 0)) * b0.y + ring_sin2 * b1.y + ring_cos2 * b2.y) * radius;
			f32 z_2 =
			    cap_center.z +
			    (sinf(base_ring_angle * (i + 0)) * b0.z + ring_sin2 * b1.z + ring_cos2 * b2.z) * radius;
			v3 w2 = v3_new(x_2, y_2, z_2);

			f32 ring_sin3 = sinf(base_slice_angle * (j + 0)) * cosf(base_ring_angle * (i + 1));
			f32 ring_cos3 = cosf(base_slice_angle * (j + 0)) * cosf(base_ring_angle * (i + 1));

			f32 x_3 =
			    cap_center.x +
			    (sinf(base_ring_angle * (i + 1)) * b0.x + ring_sin3 * b1.x + ring_cos3 * b2.x) * radius;
			f32 y_3 =
			    cap_center.y +
			    (sinf(base_ring_angle * (i + 1)) * b0.y + ring_sin3 * b1.y + ring_cos3 * b2.y) * radius;
			f32 z_3 =
			    cap_center.z +
			    (sinf(base_ring_angle * (i + 1)) * b0.z + ring_sin3 * b1.z + ring_cos3 * b2.z) * radius;
			v3 w3 = v3_new(x_3, y_3, z_3);

			f32 ring_sin4 = sinf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 1));
			f32 ring_cos4 = cosf(base_slice_angle * (j + 1)) * cosf(base_ring_angle * (i + 1));

			f32 x_4 =
			    cap_center.x +
			    (sinf(base_ring_angle * (i + 1)) * b0.x + ring_sin4 * b1.x + ring_cos4 * b2.x) * radius;
			f32 y_4 =
			    cap_center.y +
			    (sinf(base_ring_angle * (i + 1)) * b0.y + ring_sin4 * b1.y + ring_cos4 * b2.y) * radius;
			f32 z_4 =
			    cap_center.z +
			    (sinf(base_ring_angle * (i + 1)) * b0.z + ring_sin4 * b1.z + ring_cos4 * b2.z) * radius;
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

	if (++caps_count < 2) { goto caps; }

	// render middle
	if (!sphere_case)
	{
		for (u32 uj = 0; uj < slices; ++uj)
		{
			const f32 j = (f32)uj;
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
draw_billboard(m4 view, v3 position, v2 size, color c)
{
	// Get view matrix from camera

	// Calculate right and up vectors from view matrix
	// v3 up = v3_new(0.0f, 1.0f, 0.0f);
	v3 up = view.v3.up;
	up.z = -up.z;
	v3 right = view.v3.right;
	right.z = -right.z;

	// Scale right and up vectors by size
	v3 right_scaled, up_scaled;
	glm_vec3_scale(right.data, size.x / 2.0f, right_scaled.data);
	glm_vec3_scale(up.data, size.y / 2.0f, up_scaled.data);

	// Calculate corner points of billboard
	v3 top_left, top_right, bottom_right, bottom_left;
	glm_vec3_add(right_scaled.data, up_scaled.data, top_right.data);
	glm_vec3_sub(right_scaled.data, up_scaled.data, bottom_right.data);
	glm_vec3_scale(top_right.data, -1.0f, bottom_left.data);
	glm_vec3_scale(bottom_right.data, -1.0f, top_left.data);

	// Translate corner points to position
	glm_vec3_add(position.data, top_left.data, top_left.data);
	glm_vec3_add(position.data, top_right.data, top_right.data);
	glm_vec3_add(position.data, bottom_right.data, bottom_right.data);
	glm_vec3_add(position.data, bottom_left.data, bottom_left.data);

	sm__batch_overflow(8);

	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);

		sm__uv_v2(v2_new(0.0f, 0.0f));
		sm__position_v3(top_left);

		sm__uv_v2(v2_new(0.0f, 1.0f));
		sm__position_v3(bottom_left);

		sm__uv_v2(v2_new(1.0f, 1.0f));
		sm__position_v3(bottom_right);

		sm__uv_v2(v2_new(1.0f, 0.0f));
		sm__position_v3(top_right);
	}
	sm__renderer_end();
}

void
draw_plane(trs transform, color c)
{
	sm__m4_push();

	transform.scale.y = 1.0f;
	sm__renderer_trs_mul(transform);

	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);

		sm__position_3f(-0.5f, 0.5f, 0.f);
		sm__position_3f(-0.5f, -0.5f, 0.f);
		sm__position_3f(0.5f, -0.5f, 0.f);
		sm__position_3f(0.5f, 0.5f, 0.f);
		sm__renderer_end();
	}

	sm__m4_pop();
}

void
draw_ray(struct ray ray, color c)
{
	f32 s = 10000.0f;

	sm__renderer_begin(GL_LINES);
	sm__color(c);

	sm__position_3f(ray.position.x, ray.position.y, ray.position.z);
	sm__position_3f(ray.position.x + ray.direction.x * s, ray.position.y + ray.direction.y * s,
	    ray.position.z + ray.direction.z * s);
	sm__renderer_end();
}
#endif
