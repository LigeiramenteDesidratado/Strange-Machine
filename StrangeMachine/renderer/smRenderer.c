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

#ifdef CGLM_FORCE_DEPTH_ZERO_TO_ONE
#	define RESET_DEPTH_VALUE 0.0f
#else
#	define RESET_DEPTH_VALUE -1.0f
#endif

struct vertex_buffer
{
	u32 element_count; // Number of elements in the buffer (QUADS)
	v3 *positions;
	v2 *uvs;
	color *colors;
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
};

struct batch
{
	struct vertex_buffer vertex_buffer;
#define MAX_BATCH_DRAW_CALLS 256
	u32 draws_len;
	struct draw_call *draws;	// Draw calls array, depends on textureId
	f32 current_clip_control_depth; // Current depth value for next draw
};

struct framebuffer
{
	struct
	{
		REF(struct shader_resource) shader;
		u32 vao;
		u32 vbo;
	} screen;

	u32 fbo;
	u32 width, height;

	u32 color_rt;
	u32 depth_rt;
};

static struct batch sm__batch_make(void);
static void sm__batch_release(struct batch *batch);

struct renderer
{
	// batch being used for rendering
	struct batch batch;
	struct dyn_buf mem_renderer;
	struct arena arena;
	struct framebuffer framebuffer;

	struct
	{
		u32 vertex_counter; // Number of vertex in the buffer
		v2 v_uv;	    // UV coordinates
		color v_color;

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

		u32 stack_counter;
		m4 stack[32]; // Matrix stack for push/pop operations

		REF(struct shader_resource) default_shader;
		REF(struct resource) default_texture_ref;

#define MAX_TEXTUREUNIT 8
		array(REF(struct resource)) textures;
		struct resource *current_textures[MAX_TEXTUREUNIT], *selected_textures[MAX_TEXTUREUNIT];

		array(REF(struct shader_resource)) shaders;
		REF(struct shader_resource) current_shader, *selected_shader;

		enum
		{
			DIRTY_BLEND = BIT(0),
			DIRTY_DEPTH = BIT(1),
			DIRTY_RASTERIZER = BIT(2),

			// enforce 32-bit size enum
			SM__DIRTY__ENFORCE_ENUM_SIZE = 0x7fffffff
		} dirty;
		struct blend_state current_blend, selected_blend;
		struct depth_state current_depth, selected_depth;
		struct rasterizer_state current_rasterizer, selected_rasterizer;

	} state;

	color clear_color;
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

#define glCall(CALL)                                \
	do {                                        \
		gl__log_call();                     \
		CALL;                               \
		sm__assertf(gl__log_call(), #CALL); \
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

static void
gl__set_uniform(i32 location, u32 size, enum shader_type type, void *value)
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

static GLuint
gl__shader_compile_vert(const str8 vertex)
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
gl__shader_compile_frag(const str8 fragment)
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

static b8
gl__shader_link(struct shader_resource *shader)
{
	glCall(glAttachShader(shader->program, shader->vertex->id));
	glCall(glAttachShader(shader->program, shader->fragment->id));
	glCall(glLinkProgram(shader->program));

	GLint success = 0;
	glCall(glGetProgramiv(shader->program, GL_LINK_STATUS, &success));
	if (!success)
	{
		i8 info_log[2 * 512];
		glCall(glGetShaderInfoLog(shader->program, 2 * 512, NULL, info_log));
		log_error(str8_from("shader linking failed.\n\t{s}"),
		    (str8){.idata = info_log, .size = (u32)strlen(info_log)});
		glCall(glDeleteShader(shader->vertex->id));
		glCall(glDeleteShader(shader->fragment->id));

		return (false);
	}
	log_info(str8_from("[{s}] compiled and linked shaders successfully"), shader->name);

	return (true);
}

static void
gl__shader_cache_actives(struct arena *arena, struct shader_resource *shader)
{
	GLint count = 0;

	GLint size;  // size of the variable
	GLenum type; // type of the variable (float, vec3 or mat4, etc)
	GLint location;

	const GLsizei bufSize = 64; // maximum name length
	GLchar _name[bufSize];	    // variable name in GLSL
	GLsizei length;		    // name length

	glCall(glUseProgram(shader->program));

	glCall(glGetProgramiv(shader->program, GL_ACTIVE_ATTRIBUTES, &count));
	shader->attributes_count = count;
	shader->attributes = arena_reserve(arena, count * sizeof(struct shader_attribute));

	for (i32 i = 0; i < count; ++i)
	{
		glCall(glGetActiveAttrib(shader->program, i, bufSize, &length, &size, &type, _name));
		glCall(location = glGetAttribLocation(shader->program, _name));
		sm__assert(location != -1);

		str8 name = (str8){.idata = _name, .size = length};

		shader->attributes[i].name = str8_dup(arena, name);
		shader->attributes[i].size = size;
		shader->attributes[i].type = gl__to_shader_type(type);
		shader->attributes[i].location = location;
	}

	glCall(glGetProgramiv(shader->program, GL_ACTIVE_UNIFORMS, &count));

	u32 n_uniforms = 0, n_samplers = 0;
	shader->uniforms = arena_reserve(arena, count * sizeof(struct shader_uniform));
	shader->samplers = arena_reserve(arena, count * sizeof(struct shader_sampler));
	// shader->uniforms_count = count;

	for (i32 i = 0; i < count; ++i)
	{
		glCall(glGetActiveUniform(shader->program, i, bufSize, &length, &size, &type, _name));
		glCall(location = glGetUniformLocation(shader->program, _name));
		sm__assert(location != -1);

		if (strncmp(_name, "gl_", 3) == 0) { continue; }

		i8 *bracket = strchr(_name, '[');
		b8 is_first = false;

		if (bracket == 0 || (bracket[1] == '0' && bracket[2] == ']'))
		{
			if (bracket)
			{
				sm__assert(bracket[3] == '\0'); // array of structs not supported yet
				*bracket = '\0';
				length = (GLint)(bracket - _name);
			}
			is_first = true;
		}

		if (type >= GL_SAMPLER_1D && type <= GL_SAMPLER_2D_SHADOW)
		{
			glCall(glUniform1i(location, n_samplers));
			shader->samplers[n_samplers].name = str8_from_cstr(arena, _name);
			shader->samplers[n_samplers].type = gl__to_shader_type(type);
			shader->samplers[n_samplers].location = n_samplers;
			n_samplers++;
		}
		else
		{
			str8 name = (str8){.idata = _name, .size = length};
			if (is_first)
			{
				shader->uniforms[n_uniforms].name = str8_dup(arena, name);
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
						i32 index = atoi(bracket + 1) + 1;
						if ((u32)index > shader->uniforms[u].size)
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
		shader->uniforms[i].data = arena_reserve(arena, array_size);
		memset(uni->data, 0x0, array_size);
		uni->dirty = false;
	}

	log_trace(str8_from("uniforms: {s}"), shader->name);
	for (u32 i = 0; i < shader->uniforms_count; ++i)
	{
		struct shader_uniform *uni = shader->uniforms + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, size: {i3d}, type: {s}"), uni->name, uni->location,
		    uni->size, gl__type_to_string(uni->type));
	}

	log_trace(str8_from("samplers: {s}"), shader->name);
	for (u32 i = 0; i < shader->samplers_count; ++i)
	{
		struct shader_sampler *samp = shader->samplers + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, type: {s}"), samp->name, samp->location,
		    gl__type_to_string(samp->type));
	}

	log_trace(str8_from("attributes: {s}"), shader->name);
	for (u32 i = 0; i < shader->attributes_count; ++i)
	{
		struct shader_attribute *attr = shader->attributes + i;
		log_trace(str8_from("\tname: {s}, loc: {i3d}, size: {i3d}, type: {s}"), attr->name, attr->location,
		    attr->size, gl__type_to_string(attr->type));
	}
}

static b8
gl__set_blend_mode(enum blend_mode mode)
{
	b8 result = true;
	switch (mode)
	{
	case BLEND_MODE_ALPHA:
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
		}
		break;
	case BLEND_MODE_ADDITIVE:
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
		}
		break;
	case BLEND_MODE_MULTIPLIED:
		{
			glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
		}
		break;
	case BLEND_MODE_ADD_COLORS:
		{
			glBlendFunc(GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
		}
		break;
	case BLEND_MODE_SUBTRACT_COLORS:
		{
			glBlendFunc(GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_SUBTRACT);
		}
		break;
	case BLEND_MODE_ALPHA_PREMULTIPLY:
		{
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
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
			log_warn(str8_from("[{u3d}] UNKOWN BLEND MODE"), mode);
			result = false;
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
	return (result);
}

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

static void
gl__upload_texture(image_resource *image)
{
	sm__assert(image->texture_handle == 0);
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

static struct renderer RC;

static void sm__renderer_begin(u32 mode);
static void sm__renderer_end(void);

static b8 sm__batch_overflow(u32 vertices);
static void sm__batch_draw(void);

static void sm__position_v3(v3 position);
static void sm__position_3f(f32 x, f32 y, f32 z);
static void sm__position_v2(v2 position);
static void sm__position_2f(f32 x, f32 y);

static void sm__uv_v2(v2 uv);
static void sm__uv_2f(f32 x, f32 y);
static void sm__color_v4(v4 color);
static void sm__color_4ub(u8 r, u8 g, u8 b, u8 a);
static void sm__color(color color);

static void sm__m4_load_identity(void);
static void sm__m4_mul(mat4 mat);
static void sm__m4_mode(enum matrix_mode mode);
static void sm__m4_push(void);
static void sm__m4_pop(void);

static void
sm__renderer_default_state(void)
{
	// Blending mode
	RC.state.current_blend = (struct blend_state){.enable = STATE_TRUE, .mode = BLEND_MODE_ALPHA};
	glCall(glEnable(GL_BLEND));
	gl__set_blend_mode(RC.state.current_blend.mode);

	// Depth testing
	RC.state.current_depth = (struct depth_state){.enable = STATE_TRUE, .depth_func = DEPTH_FUNC_LEQUAL};
	glCall(glEnable(GL_DEPTH_TEST));
	glCall(glDepthFunc(GL_LEQUAL));

	// Rasterizer
	RC.state.current_rasterizer = (struct rasterizer_state){
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

static void
sm__renderer_commit_shader(void)
{
	if (RC.state.current_shader != RC.state.selected_shader)
	{
		struct shader_resource *shader = RC.state.selected_shader;

		if (shader == 0) { glCall(glUseProgram(0)); }
		else { glCall(glUseProgram(shader->program)); }

		RC.state.current_shader = shader;
	}
}

static void
sm__renderer_commit_uniforms(void)
{
	for (u32 i = 0; i < RC.state.current_shader->uniforms_count; ++i)
	{
		struct shader_uniform *uni = RC.state.current_shader->uniforms + i;
		{
			if (uni->dirty)
			{
				gl__set_uniform(uni->location, uni->size, uni->type, uni->data);
				uni->dirty = false;
			}
		}
	}
}

static void
sm__renderer_commit_textures(void)
{
	for (uint i = 0; i < MAX_TEXTUREUNIT; ++i)
	{
		struct resource *selected_resource = RC.state.selected_textures[i];
		struct resource *current_resource = RC.state.current_textures[i];

		if (selected_resource != current_resource)
		{
			glCall(glActiveTexture(GL_TEXTURE0 + i));
			if (selected_resource == 0)
			{
				glCall(glDisable(GL_TEXTURE_2D));
				glCall(glBindTexture(GL_TEXTURE_2D, 0));
			}
			else
			{
				if (current_resource == 0) { glCall(glEnable(GL_TEXTURE_2D)); }
				// else if (textures[texture].glTarget != textures[currTex].glTarget)
				// {
				// 	glDisable(textures[currTex].glTarget);
				// 	glEnable(textures[texture].glTarget);
				// }
				struct image_resource *selected = selected_resource->image_data;
				glCall(glBindTexture(GL_TEXTURE_2D, selected->texture_handle));
			}

			RC.state.current_textures[i] = RC.state.selected_textures[i];
		}
	}
}

static void
sm__renderer_commit_blend(void)
{
	if (RC.state.dirty & DIRTY_BLEND)
	{
		struct blend_state state = RC.state.selected_blend;

		if (state.enable)
		{
			if (state.enable == STATE_FALSE)
			{
				if (RC.state.current_blend.enable == STATE_TRUE)
				{
					glCall(glDisable(GL_BLEND));
					RC.state.current_blend.enable = state.enable;
				}
			}
			else
			{
				if (RC.state.current_blend.enable == STATE_FALSE)
				{
					glCall(glEnable(GL_BLEND));
					RC.state.current_blend.enable = state.enable;
				}

				if (state.mode && (state.mode != RC.state.current_blend.mode))
				{
					sm__assert(state.mode < SM__BLEND_MODE_MAX);
					gl__set_blend_mode(state.mode);
					RC.state.current_blend.mode = state.mode;
				}
			}
		}

		RC.state.dirty &= ~(u32)DIRTY_BLEND;
	}
}

static void
sm__renderer_commit_depth(void)
{
	if (RC.state.dirty & DIRTY_DEPTH)
	{
		struct depth_state state = RC.state.selected_depth;

		if (state.enable)
		{
			if (state.enable == STATE_FALSE)
			{
				if (RC.state.current_depth.enable == STATE_TRUE)
				{
					glCall(glDisable(GL_DEPTH_TEST));
					RC.state.current_depth.enable = state.enable;
				}
			}
			else
			{
				if (RC.state.current_depth.enable == STATE_FALSE)
				{
					glCall(glEnable(GL_DEPTH_TEST));
					RC.state.current_depth.enable = state.enable;
				}

				if (state.depth_func && (RC.state.current_depth.depth_func != state.depth_func))
				{
					static GLenum ctable_depth_func[] = {
					    [DEPTH_FUNC_NEVER] = GL_NEVER,
					    [DEPTH_FUNC_LESS] = GL_LESS,
					    [DEPTH_FUNC_EQUAL] = GL_EQUAL,
					    [DEPTH_FUNC_LEQUAL] = GL_LEQUAL,
					    [DEPTH_FUNC_GREATER] = GL_GREATER,
					    [DEPTH_FUNC_NOTEQUAL] = GL_NOTEQUAL,
					    [DEPTH_FUNC_GEQUAL] = GL_GEQUAL,
					    [DEPTH_FUNC_ALWAYS] = GL_ALWAYS,
					};

					glCall(glDepthFunc(ctable_depth_func[state.depth_func]));
					RC.state.current_depth.depth_func = state.depth_func;
				}
			}
		}

		RC.state.dirty &= ~(u32)DIRTY_DEPTH;
	}
}

static void
sm__renderer_commit_rasterizer(void)
{
	if (RC.state.dirty & DIRTY_RASTERIZER)
	{
		struct rasterizer_state state = RC.state.selected_rasterizer;

		// Culling
		if (state.cull_enable)
		{
			if (state.cull_enable == STATE_FALSE)
			{
				if (RC.state.current_rasterizer.cull_enable == STATE_TRUE)
				{
					glCall(glDisable(GL_CULL_FACE));
					RC.state.current_rasterizer.cull_enable = state.cull_enable;
				}
			}
			else
			{
				if (RC.state.current_rasterizer.cull_enable == STATE_FALSE)
				{
					glCall(glEnable(GL_CULL_FACE));
					RC.state.current_rasterizer.cull_enable = state.cull_enable;
				}

				if (state.cull_mode && (RC.state.current_rasterizer.cull_mode != state.cull_mode))
				{
					sm__assert(state.cull_mode < SM__CULL_MODE_MAX);
					static GLenum ctable_cull[] = {
					    [CULL_MODE_FRONT] = GL_FRONT,
					    [CULL_MODE_BACK] = GL_BACK,
					    [CULL_MODE_FRONT_AND_BACK] = GL_FRONT_AND_BACK,
					};
					glCall(glCullFace(ctable_cull[state.cull_mode]));
					RC.state.current_rasterizer.cull_mode = state.cull_mode;
				}
			}
		}

		// Winding mode
		if (state.winding_mode && (RC.state.current_rasterizer.winding_mode != state.winding_mode))
		{
			sm__assert(state.winding_mode < SM__WINDING_MODE_MAX);
			static GLenum ctable_winding[] = {
			    [WINDING_MODE_CLOCK_WISE] = GL_CW,
			    [WINDING_MODE_COUNTER_CLOCK_WISE] = GL_CCW,
			};

			glCall(glFrontFace(ctable_winding[state.winding_mode]));
			RC.state.current_rasterizer.winding_mode = state.winding_mode;
		}

		// Polygon mode
		if (state.polygon_mode && (RC.state.current_rasterizer.polygon_mode != state.polygon_mode))
		{
			sm__assert(state.polygon_mode < SM__POLYGON_MODE_MAX);
			static GLenum ctable_polygon[] = {
			    [POLYGON_MODE_POINT] = GL_POINT,
			    [POLYGON_MODE_LINE] = GL_LINE,
			    [POLYGON_MODE_FILL] = GL_FILL,
			};
			glCall(glPolygonMode(GL_FRONT_AND_BACK, ctable_polygon[state.polygon_mode]));
			RC.state.current_rasterizer.polygon_mode = state.polygon_mode;
		}

		// Scissor
		if (state.scissor)
		{
			if (state.scissor == STATE_TRUE)
			{
				if (RC.state.current_rasterizer.scissor == STATE_FALSE)
				{
					glCall(glEnable(GL_SCISSOR_TEST));
					RC.state.current_rasterizer.scissor = state.scissor;
				}
			}
			else
			{
				if (RC.state.current_rasterizer.scissor == STATE_TRUE)
				{
					glCall(glDisable(GL_SCISSOR_TEST));
					RC.state.current_rasterizer.scissor = state.scissor;
				}
			}
		}

		if (state.line_width && RC.state.current_rasterizer.line_width != state.line_width)
		{
			glCall(glLineWidth(state.line_width));
			RC.state.current_rasterizer.line_width = state.line_width;
		}

		RC.state.dirty &= ~(u32)DIRTY_RASTERIZER;
	}
}

void
renderer_state_commit(void)
{
	sm__renderer_commit_shader();
	sm__renderer_commit_uniforms();
	sm__renderer_commit_textures();
	sm__renderer_commit_blend();
	sm__renderer_commit_depth();
	sm__renderer_commit_rasterizer();
}

void
renderer_state_clear(void)
{
	RC.state.selected_shader = 0;

	for (u32 i = 0; i < MAX_TEXTUREUNIT; ++i) { RC.state.selected_textures[i] = RC.state.default_texture_ref; }
}

void
renderer_state_set_defaults(void)
{
	RC.state.current_shader = 0;

	for (u32 i = 0; i < MAX_TEXTUREUNIT; ++i) { RC.state.current_textures[i] = 0; }
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
	gl__upload_texture(image);
}

void
renderer_texture_add(str8 texture_name)
{
	struct resource *resource = resource_get_by_name(texture_name);
	sm__assert(resource);
	sm__assert(resource->type == RESOURCE_IMAGE);

	if (resource->image_data->texture_handle) { return; }

	resource = resource_make_reference(resource);
	gl__upload_texture(resource->image_data);

	array_push(&RC.arena, RC.state.textures, resource);
}

void
renderer_texture_set(str8 texture_name, str8 sampler_name)
{
	i32 loc = shader_resource_get_sampler_loc(RC.state.selected_shader, sampler_name, true);

	for (u32 i = 0; i < array_len(RC.state.textures); ++i)
	{
		struct resource *texture_resource = RC.state.textures[i];

		if (str8_eq(texture_name, texture_resource->name))
		{
			RC.state.selected_textures[loc] = texture_resource;
			return;
		}
	}

	RC.state.selected_textures[loc] = RC.state.default_texture_ref;
	// log_error(str8_from("[{s}] texture not found"), texture_name);
}

void
renderer_shader_add(str8 shader_name, str8 vertex, str8 fragment)
{
	struct shader_resource *shader = load_shader(shader_name, vertex, fragment);
	renderer_upload_shader(shader);

	array_push(&RC.arena, RC.state.shaders, shader);
}

void
renderer_shader_set(str8 shader_name)
{
	// if (str8_eq(shader_name, RC.state.current_shader->name)) { return; }

	for (u32 i = 0; i < array_len(RC.state.shaders); ++i)
	{
		struct shader_resource *shader = RC.state.shaders[i];
		if (str8_eq(shader_name, shader->name))
		{
			RC.state.selected_shader = shader;
			return;
		}
	}

	log_error(str8_from("[{s}] shader not found"), shader_name);
	sm__unreachable();
}

void
renderer_shader_set_uniform(str8 name, void *value, u32 size, u32 count)
{
	sm__assert(count);

	for (u32 i = 0; i < RC.state.selected_shader->uniforms_count; ++i)
	{
		struct shader_uniform *uni = RC.state.selected_shader->uniforms + i;

		if (str8_eq(name, uni->name))
		{
			sm__assert(gl__ctable_type_size[uni->type] == size);
			sm__assert(count >= 1 && count <= uni->size);

			if (memcmp(uni->data, value, size * count))
			{
				memcpy(uni->data, value, size * count);
				uni->dirty = true;
			}
			return;
		}
	}

	sm__unreachable();
}

void
renderer_blend_set(struct blend_state blend)
{
	if ((blend.enable && RC.state.current_blend.enable != blend.enable) ||
	    (blend.mode && RC.state.current_blend.mode != blend.mode))
	{
		RC.state.selected_blend = blend;
		RC.state.dirty |= DIRTY_BLEND;
	}
}

void
renderer_depth_set(struct depth_state depth)
{
	if ((depth.enable && RC.state.current_depth.enable != depth.enable) ||
	    (depth.depth_func && RC.state.current_depth.depth_func != depth.depth_func))
	{
		RC.state.selected_depth = depth;
		RC.state.dirty |= DIRTY_DEPTH;
	}
}

void
renderer_rasterizer_set(struct rasterizer_state rasterizer)
{
	if (rasterizer.cull_mode && RC.state.current_rasterizer.cull_mode != rasterizer.cull_mode) { goto dirty; }
	if (rasterizer.cull_enable && RC.state.current_rasterizer.cull_enable != rasterizer.cull_enable) { goto dirty; }
	if (rasterizer.winding_mode && RC.state.current_rasterizer.winding_mode != rasterizer.winding_mode)
	{
		goto dirty;
	}
	if (rasterizer.scissor && RC.state.current_rasterizer.scissor != rasterizer.scissor) { goto dirty; }
	if (rasterizer.polygon_mode && RC.state.current_rasterizer.polygon_mode != rasterizer.polygon_mode)
	{
		goto dirty;
	}
	if (rasterizer.line_width && RC.state.current_rasterizer.line_width != rasterizer.line_width) { goto dirty; }

	return;

dirty:
	RC.state.selected_rasterizer = rasterizer;
	RC.state.dirty |= DIRTY_RASTERIZER;
}

void
renderer_upload_shader(struct shader_resource *shader)
{
	if (shader->program == 0)
	{
		if (shader->vertex->id == 0) { shader->vertex->id = gl__shader_compile_vert(shader->vertex->vertex); }
		if (shader->fragment->id == 0)
		{
			shader->fragment->id = gl__shader_compile_frag(shader->fragment->fragment);
		}
		sm__assert(shader->vertex->id);
		sm__assert(shader->fragment->id);
		glCall(shader->program = glCreateProgram());
		if (!gl__shader_link(shader))
		{
			glCall(glDeleteProgram(shader->program));
			glCall(glDeleteShader(shader->fragment->id));
			glCall(glDeleteShader(shader->vertex->id));
			log_error(str8_from("error linking program shader"));

			return;
		}

		struct arena *arena = resource_get_arena();
		gl__shader_cache_actives(arena, shader);
	}
}

static void
sm__renderer_load_defaults(void)
{
	renderer_shader_add(
	    str8_from("default"), str8_from("shaders/default.vertex"), str8_from("shaders/default.fragment"));

	for (u32 i = 0; i < array_len(RC.state.shaders); ++i)
	{
		struct shader_resource *shader = RC.state.shaders[i];
		if (str8_eq(shader->name, str8_from("default")))
		{
			RC.state.default_shader = shader;
			break;
		}
	}

	struct resource *default_image = resource_get_default_image();
	array_push(&RC.arena, RC.state.textures, default_image);
	RC.state.default_texture_ref = *array_last_item(RC.state.textures);
}

static struct framebuffer
sm__framebuffer_make(u32 framebuffer_width, u32 framebuffer_height)
{
	struct framebuffer result;

	result.width = framebuffer_width;
	result.height = framebuffer_height;

	glCall(glViewport(0, 0, framebuffer_width, framebuffer_height));

	{
		// clang-format off
		static f32 rectangle_vertices[] = 
		{
			// Coords    // texCoords
			 1.0f, -1.0f,  1.0f, 0.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			-1.0f,  1.0f,  0.0f, 1.0f,

			 1.0f,  1.0f,  1.0f, 1.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			-1.0f,  1.0f,  0.0f, 1.0f
		};
		// clang-format on

		struct shader_resource *framebuffer_shader = load_shader(str8_from("framebuffer"),
		    str8_from("shaders/framebuffer.vertex"), str8_from("shaders/framebuffer.fragment"));
		result.screen.shader = framebuffer_shader;
		renderer_upload_shader(framebuffer_shader);

		glCall(glGenVertexArrays(1, &result.screen.vao));
		glCall(glBindVertexArray(result.screen.vao));

		glCall(glGenBuffers(1, &result.screen.vbo));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, result.screen.vbo));
		glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_vertices), &rectangle_vertices, GL_STATIC_DRAW));

		i32 loc = shader_resource_get_attribute_loc(framebuffer_shader, str8_from("a_position"), true);
		glCall(glEnableVertexAttribArray(loc));
		glCall(glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), 0));

		loc = shader_resource_get_attribute_loc(framebuffer_shader, str8_from("a_uv"), true);
		glCall(glEnableVertexAttribArray(loc));
		glCall(glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void *)(2 * sizeof(f32))));

		glCall(glBindVertexArray(0));

		// just in case
		glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));
		glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

		glCall(glUseProgram(result.screen.shader->program));
		i32 a_loc = shader_resource_get_sampler_loc(result.screen.shader, str8_from("u_framebuffer"), true);
		glCall(glUniform1i(a_loc, 0));
		glCall(glUseProgram(0));
	}

	// RGBA8 2D texture, 24 bit depth texture, 256x256
	glCall(glGenTextures(1, &result.color_rt));
	glCall(glBindTexture(GL_TEXTURE_2D, result.color_rt));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	// NULL means reserve texture memory, but texels are undefined
	glCall(
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
	//-------------------------
	glCall(glGenFramebuffers(1, &result.fbo));
	glCall(glBindFramebuffer(GL_FRAMEBUFFER, result.fbo));
	// Attach 2D texture to this FBO
	glCall(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, result.color_rt, 0));
	//-------------------------
	glCall(glGenRenderbuffers(1, &result.depth_rt));
	glCall(glBindRenderbuffer(GL_RENDERBUFFER, result.depth_rt));
	glCall(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, result.width, result.height));
	//-------------------------
	// Attach depth buffer to FBO
	glCall(
	    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, result.depth_rt));

	//-------------------------
	// Does the GPU support current FBO configuration?
	GLenum status;
	glCall(status = glCheckFramebufferStatus(GL_FRAMEBUFFER));
	switch (status)
	{
	case GL_FRAMEBUFFER_COMPLETE:
		{
			log_trace(str8_from("FBO successfully created!"));
		}
		break;
	default:
		{
			sm__unreachable();
		}
	}

	glCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));

	return (result);
}

static void
sm__framebuffer_release(struct framebuffer *framebuffer)
{
	glCall(glDeleteTextures(1, &framebuffer->color_rt));
	glCall(glDeleteRenderbuffers(1, &framebuffer->depth_rt));

	glCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	glCall(glDeleteFramebuffers(1, &framebuffer->fbo));
}

u32
renderer_get_framebuffer_width(void)
{
	u32 result;

	result = RC.framebuffer.width;

	return (result);
}

u32
renderer_get_framebuffer_height(void)
{
	u32 result;

	result = RC.framebuffer.height;

	return (result);
}

b8
renderer_init(u32 framebuffer_width, u32 framebuffer_height)
{
	struct buf m_base = base_memory_begin();
	RC.mem_renderer = (struct dyn_buf){
	    .data = m_base.data,
	    .cap = m_base.size,
	    .len = 0,
	};

	RC.batch = sm__batch_make();
	RC.framebuffer = sm__framebuffer_make(framebuffer_width, framebuffer_height);

	// init stack matrices
	glm_mat4_identity_array(&RC.state.stack[0].data, ARRAY_SIZE(RC.state.stack));

	RC.state.modelview = m4_identity();
	RC.state.projection = m4_identity();
	RC.state.current_matrix = &RC.state.modelview;
	RC.state.transform_required = false;
	RC.state.transform = m4_identity();

	RC.clear_color = cWHITE;

	sm__renderer_default_state();
	glCall(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	glCall(glClearDepth(1.0));
	glCall(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

	base_memory_end(RC.mem_renderer.len);

	struct buf renderer_base_memory = base_memory_reserve(MB(1));
	arena_make(&RC.arena, renderer_base_memory);
	arena_validate(&RC.arena);

	array_set_cap(&RC.arena, RC.state.textures, 60);
	memset(RC.state.textures, 0x0, sizeof(struct resource *) * array_cap(RC.state.textures));

	array_set_cap(&RC.arena, RC.state.shaders, 60);
	memset(RC.state.shaders, 0x0, sizeof(struct shader_resource *) * array_cap(RC.state.shaders));

	sm__renderer_load_defaults();

	return (true);
}

void
renderer_teardown(void)
{
	sm__framebuffer_release(&RC.framebuffer);
	sm__batch_release(&RC.batch);
}

void
renderer_start_frame(void)
{
	glCall(glBindFramebuffer(GL_FRAMEBUFFER, RC.framebuffer.fbo));
	// renderer_on_resize(RC.framebuffer.width, RC.framebuffer.height);

	sm__m4_load_identity();

	renderer_on_resize(RC.framebuffer.width, RC.framebuffer.height);
}

void
renderer_finish_frame(void)
{
	sm__batch_draw();

	glCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));

	glCall(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
	glCall(glClear(GL_COLOR_BUFFER_BIT));
	glCall(glViewport(0, 0, core_get_window_width(), core_get_window_height()));

	glCall(glUseProgram(RC.framebuffer.screen.shader->program));

	glCall(glBindVertexArray(RC.framebuffer.screen.vao));

	b8 depth_enabled = RC.state.current_depth.enable;
	if (depth_enabled) { glCall(glDisable(GL_DEPTH_TEST)); }
	b8 cull_enabled = RC.state.current_rasterizer.cull_enable;
	if (cull_enabled) { glCall(glDisable(GL_CULL_FACE)); }

	glCall(glBindTexture(GL_TEXTURE_2D, RC.framebuffer.color_rt));
	glCall(glDrawArrays(GL_TRIANGLES, 0, 6));

	if (depth_enabled) { glCall(glEnable(GL_DEPTH_TEST)); }
	if (cull_enabled) { glCall(glEnable(GL_CULL_FACE)); }

	glCall(glBindVertexArray(0));
	glCall(glUseProgram(0));
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
	vertex_buffer.positions = (v3 *)(RC.mem_renderer.data + RC.mem_renderer.len);
	RC.mem_renderer.len += sizeof(v3) * VIRTICES * QUAD_SIZE;

	vertex_buffer.uvs = (v2 *)(RC.mem_renderer.data + RC.mem_renderer.len);
	RC.mem_renderer.len += sizeof(v2) * VIRTICES * QUAD_SIZE;

	vertex_buffer.colors = (color *)(RC.mem_renderer.data + RC.mem_renderer.len);
	RC.mem_renderer.len += sizeof(color) * VIRTICES * QUAD_SIZE;

	vertex_buffer.indices = (u32 *)(RC.mem_renderer.data + RC.mem_renderer.len);
	RC.mem_renderer.len += sizeof(u32) * VIRTICES * INDICES_PER_QUAD;

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

	struct shader_resource *shader = load_shader(
	    str8_from("default"), str8_from("shaders/default.vertex"), str8_from("shaders/default.fragment"));
	RC.state.default_shader = shader;
	renderer_upload_shader(shader);

	RC.state.default_texture_ref = resource_get_default_image();
	struct image_resource *default_image = RC.state.default_texture_ref->image_data;
	if (!default_image->texture_handle) { renderer_upload_texture(default_image); }

	glCall(glGenVertexArrays(1, &vertex_buffer.vao));
	glCall(glBindVertexArray(vertex_buffer.vao));

	// positions
	glCall(glGenBuffers(1, &vertex_buffer.vbos[0]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[0]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(v3) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));

	i32 loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_position"), true);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 3, GL_FLOAT, false, 0, 0));

	// uvs
	glCall(glGenBuffers(1, &vertex_buffer.vbos[1]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[1]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(v2) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));

	loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_uv"), true);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 2, GL_FLOAT, false, 0, 0));

	// colors
	glCall(glGenBuffers(1, &vertex_buffer.vbos[2]));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbos[2]));
	glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(color) * VIRTICES * QUAD_SIZE, 0, GL_DYNAMIC_DRAW));

	loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_color"), true);
	glCall(glEnableVertexAttribArray(loc));
	glCall(glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, true, 0, 0));

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

	result.draws = (struct draw_call *)(RC.mem_renderer.data + RC.mem_renderer.len);
	RC.mem_renderer.len += (sizeof(struct draw_call) * MAX_BATCH_DRAW_CALLS);

	for (u32 i = 0; i < MAX_BATCH_DRAW_CALLS; ++i)
	{
		result.draws[i].mode = GL_QUADS;
		result.draws[i].vertex_counter = 0;
		result.draws[i].vertex_alignment = 0;
	}

	result.draws_len = 1;
	result.current_clip_control_depth = RESET_DEPTH_VALUE; // Reset depth value

	return (result);
}

static void
sm__batch_release(struct batch *batch)
{
	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	glCall(glBindVertexArray(batch->vertex_buffer.vao));

	i32 loc;
	loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_color"), true);
	glCall(glDisableVertexAttribArray(loc));

	loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_uv"), true);
	glCall(glDisableVertexAttribArray(loc));

	loc = shader_resource_get_attribute_loc(RC.state.default_shader, str8_from("a_position"), true);
	glCall(glDisableVertexAttribArray(loc));

	glCall(glBindVertexArray(0));
	glCall(glDeleteBuffers(ARRAY_SIZE(batch->vertex_buffer.vbos), batch->vertex_buffer.vbos));

	glCall(glDeleteBuffers(1, &batch->vertex_buffer.ebo));
	glCall(glDeleteVertexArrays(1, &batch->vertex_buffer.vao));
}

static void
sm__batch_reset(void)
{
	RC.state.vertex_counter = 0;
	RC.batch.current_clip_control_depth = RESET_DEPTH_VALUE;
	for (u32 i = 0; i < MAX_BATCH_DRAW_CALLS; ++i)
	{
		RC.batch.draws[i].mode = GL_QUADS;
		RC.batch.draws[i].vertex_counter = 0;
	}
	RC.batch.draws_len = 1;
}

static b8
sm__batch_overflow(u32 vertices)
{
	b8 overflow = false;
	if (RC.state.vertex_counter + vertices >= RC.batch.vertex_buffer.element_count * 4)
	{
		u32 current_mode = RC.batch.draws[RC.batch.draws_len - 1].mode;
		overflow = true;
		sm__batch_draw();
		RC.batch.draws[RC.batch.draws_len - 1].mode = current_mode;
	}
	return (overflow);
}

void
sm__batch_draw(void)
{
	if (RC.state.vertex_counter == 0) { return; }

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

	// glCall(glUseProgram(RC.state.default_program->program));

	m4 mvp;
	glm_mat4_mul(RC.state.projection.data, RC.state.modelview.data, mvp.data);
	renderer_shader_set_uniform(str8_from("u_pvm"), &mvp, sizeof(m4), 1);

	renderer_state_commit();
	for (u32 i = 0, vertex_offset = 0; i < RC.batch.draws_len; ++i)
	{
		struct draw_call *d_call = RC.batch.draws + i;
		if (d_call->mode == GL_LINES || d_call->mode == GL_TRIANGLES)
		{
			glCall(glDrawArrays(d_call->mode, (i32)vertex_offset, (i32)d_call->vertex_counter));
		}
		else
		{
			glCall(glDrawElements(GL_TRIANGLES, (i32)(d_call->vertex_counter / 4 * 6), GL_UNSIGNED_INT,
			    (void *)(vertex_offset / 4 * 6 * sizeof(u32))));
		}
		vertex_offset += (d_call->vertex_counter + d_call->vertex_alignment);
	}

	sm__batch_reset();
}

void
renderer_batch3D_begin(camera_component *camera)
{
	sm__batch_draw();

	renderer_state_clear();
	renderer_shader_set(str8_from("default"));

	sm__m4_mode(MATRIX_MODE_PROJECTION);
	sm__m4_push();
	sm__m4_load_identity();

#if 0
	f32 top = 0.1f * tanf(glm_rad(camera->fov * 05));
	f32 right = top * camera->aspect_ratio;

	glm_frustum(-right, right, -top, top, 0.1f, 100.f, proj.data);
	glm_lookat(camera->position.data, camera->target.data, camera->up.data, view.data);
#endif

	sm__m4_mul(camera->projection.data);

	sm__m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__m4_load_identity();

	sm__m4_mul(camera->view.data);
}

void
renderer_batch3D_end(void)
{
	sm__batch_draw(); // Update and draw internal render batch

	sm__m4_mode(MATRIX_MODE_PROJECTION);
	sm__m4_pop(); // Restore previous matrix (projection) from stack

	sm__m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__m4_load_identity();
}

void
renderer_batch_begin(void)
{
	sm__batch_draw();

	sm__m4_load_identity();

	renderer_state_clear();
	renderer_shader_set(str8_from("default"));
}

void
renderer_batch_sampler_set(str8 sampler)
{
	renderer_texture_set(sampler, str8_from("u_tex0"));
}

void
renderer_batch_blend_set(struct blend_state blend)
{
	renderer_blend_set(blend);
}

void
renderer_batch_end(void)
{
	sm__batch_draw();

	sm__m4_load_identity();
}

static void
sm__renderer_begin(u32 mode)
{
	const u32 draw_counter = RC.batch.draws_len;
	struct draw_call *d_call = &RC.batch.draws[draw_counter - 1];

	if (d_call->mode == mode) { return; }

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
		if (!sm__batch_overflow(d_call->vertex_alignment))
		{
			RC.state.vertex_counter += d_call->vertex_alignment;
			RC.batch.draws_len++;
			// draw_counter = (u32)SM_ARRAY_LEN(RC.batch.draws) + 1;
		}
	}
	if (RC.batch.draws_len >= MAX_BATCH_DRAW_CALLS) { sm__batch_draw(); }

	d_call = &RC.batch.draws[RC.batch.draws_len - 1];
	d_call->mode = mode;
	d_call->vertex_counter = 0;
}

static void
sm__renderer_end(void)
{
	RC.batch.current_clip_control_depth += (1.0f / 20000.0f);
	if (RC.state.vertex_counter >= (RC.batch.vertex_buffer.element_count * 4 - 4))
	{
		for (i32 i = (i32)RC.state.stack_counter; i >= 0; --i) { sm__m4_pop(); }
		sm__batch_draw();
	}
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
	glm_vec2_copy(RC.state.v_uv.data, v_buf->uvs[RC.state.vertex_counter].data);
	v_buf->colors[RC.state.vertex_counter] = RC.state.v_color;

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
	sm__position_v3(v3_new(position.x, position.y, RC.batch.current_clip_control_depth));
}

static void
sm__position_2f(f32 x, f32 y)
{
	sm__position_v3(v3_new(x, y, RC.batch.current_clip_control_depth));
}

static void
sm__uv_v2(v2 uv)
{
	glm_vec2_copy(uv.data, RC.state.v_uv.data);
}

static void
sm__uv_2f(f32 x, f32 y)
{
	RC.state.v_uv.x = x;
	RC.state.v_uv.y = y;
}

static void
sm__color_v4(v4 c)
{
	RC.state.v_color = color_from_v4(c);
}

static void
sm__color_hex(u32 c)
{
	RC.state.v_color = color_from_hex(c);
}

static void
sm__color_4ub(u8 r, u8 g, u8 b, u8 a)
{
	RC.state.v_color.r = r;
	RC.state.v_color.g = g;
	RC.state.v_color.b = b;
	RC.state.v_color.a = a;
}

static void
sm__color(color c)
{
	RC.state.v_color = c;
}

static void
sm__m4_load_identity(void)
{
	*RC.state.current_matrix = m4_identity();
}

static void
sm__m4_mul(mat4 mat)
{
	glm_mat4_mul(RC.state.current_matrix->data, mat, RC.state.current_matrix->data);
}

static void
sm__renderer_trs_mul(trs transform)
{
	m4 mat = trs_to_m4(transform);
	glm_mat4_mul(RC.state.current_matrix->data, mat.data, RC.state.current_matrix->data);
}

static void
sm__renderer_translate(v3 translate)
{
	glm_translate(RC.state.current_matrix->data, translate.data);
}

static void
sm__renderer_rotate(v4 rotate)
{
	glm_quat_rotate(RC.state.current_matrix->data, rotate.data, RC.state.current_matrix->data);
}

static void
sm__renderer_scale(v3 scale)
{
	glm_scale(RC.state.current_matrix->data, scale.data);
}

static void
sm__m4_mode(enum matrix_mode mode)
{
	if (mode == MATRIX_MODE_PROJECTION) RC.state.current_matrix = &RC.state.projection;
	else if (mode == MATRIX_MODE_MODEL_VIEW) RC.state.current_matrix = &RC.state.modelview;
	RC.state.matrix_mode = mode;
}

static void
sm__m4_pop(void)
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
sm__m4_push(void)
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

// void
// draw_begin(void)
// {
// 	sm__renderer_m4_load_identity();
// }
//
// void
// draw_end(void)
// {
// 	sm__batch_draw();
// }

void
upload_mesh(mesh_component *mesh)
{
	mesh_resource *m = mesh->mesh_ref;
	glCall(glGenVertexArrays(1, &m->vao));
	glCall(glBindVertexArray(m->vao));

	struct attributes
	{
		u32 idx;
		str8 n;
		void *ptr;
		u32 size;
		u32 comp;
	} ctable[4] = {
	    {
		.idx = 0,
		.n = str8_from("a_position"),
		.ptr = m->positions,
		.size = array_len(m->positions) * sizeof(v3),
		.comp = 3,
	     },
	    {
		.idx = 1,
		.n = str8_from("a_uv"),
		.ptr = m->uvs,
		.size = array_len(m->uvs) * sizeof(v2),
		.comp = 2,
	     },
	    {
		.idx = 2,
		.n = str8_from("a_color"),
		.ptr = m->colors,
		.size = array_len(m->colors) * sizeof(v4),
		.comp = 4,
	     },
	    {
		.idx = 3,
		.n = str8_from("a_normal"),
		.ptr = m->normals,
		.size = array_len(m->normals) * sizeof(v3),
		.comp = 3,
	     },
	};

	for (u32 i = 0; i < ARRAY_SIZE(ctable); ++i)
	{
		// sm__assert(ctable[i].ptr != 0);
		if (!ctable[i].ptr) continue;
		glCall(glGenBuffers(1, &m->vbos[ctable[i].idx]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->vbos[ctable[i].idx]));
		glCall(glBufferData(GL_ARRAY_BUFFER, ctable[i].size, ctable[i].ptr, GL_STATIC_DRAW));

		i32 a_loc = shader_resource_get_attribute_loc(RC.state.selected_shader, ctable[i].n, true);
		glCall(glVertexAttribPointer((u32)a_loc, (i32)ctable[i].comp, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_loc));
	}

	sm__assert(m->indices);
	glCall(glGenBuffers(1, &m->ebo));
	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ebo));
	glCall(glBufferData(GL_ELEMENT_ARRAY_BUFFER, array_len(m->indices) * sizeof(u32), m->indices, GL_STATIC_DRAW));

	if (m->flags & MESH_FLAG_SKINNED)
	{
		glCall(glGenBuffers(1, &m->skin_data.vbos[0]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->skin_data.vbos[0]));
		glCall(glBufferData(GL_ARRAY_BUFFER, array_len(m->skin_data.weights) * sizeof(v4), m->skin_data.weights,
		    GL_STATIC_DRAW));

		i32 a_weights_loc =
		    shader_resource_get_attribute_loc(RC.state.selected_shader, str8_from("a_weights"), true);
		glCall(glVertexAttribPointer((u32)a_weights_loc, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_weights_loc));

		glCall(glGenBuffers(1, &m->skin_data.vbos[1]));
		glCall(glBindBuffer(GL_ARRAY_BUFFER, m->skin_data.vbos[1]));
		glCall(glBufferData(GL_ARRAY_BUFFER, array_len(m->skin_data.influences) * sizeof(iv4),
		    m->skin_data.influences, GL_STATIC_DRAW));

		i32 a_joints_loc =
		    shader_resource_get_attribute_loc(RC.state.selected_shader, str8_from("a_joints"), true);
		glCall(glVertexAttribPointer((u32)a_joints_loc, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
		glCall(glEnableVertexAttribArray((u32)a_joints_loc));
	}

	glCall(glBindVertexArray(0));

	glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	glCall(glBindBuffer(GL_ARRAY_BUFFER, 0));

	if (m->vao > 0)
	{
		log_info(str8_from("[{s}] mesh uploaded successfully to VRAM"), mesh->resource_ref->name);
		return;
	}

	sm__unreachable();
}

// void
// draw_mesh(camera_component *camera, mesh_component *mesh, material_component *material, m4 model)
// {
// 	sm__assert(mesh->resource_ref->type == RESOURCE_MESH);
//
// 	struct mesh_resource *mes = mesh->resource_ref->data;
// 	struct material_resource *mat = (material->resource_ref) ? material->resource_ref->material_data : 0;
//
// 	if (!mes) { return; }
//
// 	if (mes->vao == 0) { upload_mesh(mesh); }
//
// 	v4 diffuse_color = v4_one();
// 	b8 double_sided = mes->flags & MESH_FLAG_DOUBLE_SIDED;
//
// 	b8 use_fog = renderer_is_set_fog() && !mes->skin_data.is_skinned;
//
// 	struct shader_resource *shader;
// 	shader = mes->skin_data.is_skinned ? RC.state.default_program_skinned3D : RC.state.default_program3D;
// 	if (use_fog) { shader = RC.state.default_program3D_fog; }
//
// 	u32 texture = RC.state.default_texture_ref.image_data->texture_handle;
//
// 	if (mat)
// 	{
// 		if (mat->color.hex != 0) { diffuse_color = color_to_v4(mat->color); }
// 		// double_sided = mat->double_sided;
// 		// if (mat->shader_handle) { shader = mat->shader_handle; }
// 		if (mat->image.size != 0)
// 		{
// 			struct resource *img_resource = resource_get_by_name(mat->image);
// 			sm__assert(img_resource->type == RESOURCE_IMAGE);
//
// 			struct image_resource *img = img_resource->data;
// 			if (!img->texture_handle) { renderer_upload_texture(img); }
// 			texture = img->texture_handle;
// 			sm__assert(texture != 0);
// 		}
// 	}
//
// 	glCall(glBindVertexArray(mes->vao));
// 	glCall(glUseProgram(shader->program));
//
// 	i32 texture_loc = shader_resource_get_sampler_loc(shader, str8_from("u_tex0"), true);
// 	glCall(glUniform1i(texture_loc, 0));
// 	glCall(glActiveTexture(GL_TEXTURE0 + 0));
// 	glCall(glBindTexture(GL_TEXTURE_2D, texture));
//
// 	// glCall(u_model_loc = glGetUniformLocation(shader, "u_model"));
// 	i32 u_model_loc = shader_resource_get_uniform_loc(shader, str8_from("u_model"), false);
// 	if (u_model_loc != -1) { glCall(glUniformMatrix4fv(u_model_loc, 1, false, model.float16)); }
//
// 	// i32 u_inv_model_loc = -1;
// 	// glCall(u_inv_model_loc = glGetUniformLocation(shader, "u_inverse_model"));
// 	i32 u_inv_model_loc = shader_resource_get_uniform_loc(shader, str8_from("u_inverse_model"), false);
// 	if (u_inv_model_loc != -1)
// 	{
// 		m4 inv_model;
// 		glm_mat4_inv(model.data, inv_model.data);
// 		glCall(glUniformMatrix4fv(u_inv_model_loc, 1, false, inv_model.float16));
// 	}
//
// 	// i32 u_fog_mode_loc = -1;
// 	// glCall(u_fog_mode_loc = glGetUniformLocation(shader, "u_fog_mode"));
// 	i32 u_fog_mode_loc = shader_resource_get_uniform_loc(shader, str8_from("u_fog_mode"), false);
// 	if (u_fog_mode_loc != -1) { glCall(glUniform1i(u_fog_mode_loc, RC.env.fog_mode)); }
//
// 	// i32 u_fog_start_end_density_loc = -1;
// 	// glCall(u_fog_start_end_density_loc = glGetUniformLocation(shader, "u_fog_sed"));
// 	i32 u_fog_start_end_density_loc = shader_resource_get_uniform_loc(shader, str8_from("u_fog_sed"),
// false); 	if (u_fog_start_end_density_loc != -1)
// 	{
// 		v3 fog_start_end_density = v3_new(RC.env.fog_start, RC.env.fog_end, RC.env.fog_density);
// 		glCall(glUniform3fv(u_fog_start_end_density_loc, 1, fog_start_end_density.data));
// 	}
//
// 	// i32 u_fog_color_loc = -1;
// 	// glCall(u_fog_color_loc = glGetUniformLocation(shader, "u_fog_color"));
// 	i32 u_fog_color_loc = shader_resource_get_uniform_loc(shader, str8_from("u_fog_color"), false);
// 	if (u_fog_color_loc != -1)
// 	{
// 		v4 fog_color = color_to_v4(RC.env.fog_color);
// 		glCall(glUniform4fv(u_fog_color_loc, 1, fog_color.data));
// 	}
//
// 	m4 view = camera_get_view(camera);
// 	m4 proj = camera_get_projection(camera);
//
// 	m4 view_projection_matrix;
// 	glm_mat4_mul(proj.data, view.data, view_projection_matrix.data);
//
// 	// i32 u_pv = -1;
// 	// glCall(u_pv = glGetUniformLocation(shader, "u_pv"));
// 	i32 u_pv = shader_resource_get_uniform_loc(shader, str8_from("u_pv"), false);
// 	if (u_pv != -1) { glCall(glUniformMatrix4fv(u_pv, 1, false, view_projection_matrix.float16)); }
//
// 	// i32 u_pvm = -1;
// 	// glCall(u_pvm = glGetUniformLocation(shader, "u_pvm"));
// 	i32 u_pvm = shader_resource_get_uniform_loc(shader, str8_from("u_pvm"), false);
// 	if (u_pvm != -1)
// 	{
// 		m4 mvp;
// 		glm_mat4_mul(view_projection_matrix.data, model.data, mvp.data);
// 		glCall(glUniformMatrix4fv(u_pvm, 1, false, mvp.float16));
// 	}
//
// 	// i32 u_diffuse_color = -1;
// 	// glCall(u_diffuse_color = glGetUniformLocation(shader, "u_diffuse_color"));
// 	i32 u_diffuse_color = shader_resource_get_uniform_loc(shader, str8_from("u_diffuse_color"), false);
// 	if (u_diffuse_color != -1) { glCall(glUniform4fv(u_diffuse_color, 1, diffuse_color.data)); }
//
// 	if (mes->skin_data.is_skinned)
// 	{
// 		sm__assert(shader == RC.state.default_program_skinned3D);
//
// 		i32 u_animated = shader_resource_get_uniform_loc(shader, str8_from("u_animated"), true);
// 		glCall(glUniformMatrix4fv(u_animated, (i32)array_len(mes->skin_data.pose_palette), GL_FALSE,
// 		    mes->skin_data.pose_palette->float16));
// 	}
//
// 	if (double_sided) glCall(glDisable(GL_CULL_FACE));
//
// 	if (mes->indices) glCall(glDrawElements(GL_TRIANGLES, (i32)array_len(mes->indices), GL_UNSIGNED_INT,
// 0)); 	else glCall(glDrawArrays(GL_TRIANGLES, 0, (i32)array_len(mes->positions)));
//
// 	if (double_sided) glCall(glEnable(GL_CULL_FACE));
//
// 	glCall(glBindTexture(GL_TEXTURE_2D, 0));
//
// 	glCall(glUseProgram(0));
// 	glCall(glBindVertexArray(0));
// }

void
draw_mesh2(mesh_component *mesh)
{
	sm__assert(mesh->resource_ref->type == RESOURCE_MESH);

	struct mesh_resource *mes = mesh->resource_ref->data;

	if (!mes) { return; }
	if (mes->vao == 0) { upload_mesh(mesh); }

	glCall(glBindVertexArray(mes->vao));

	if (mes->indices) { glCall(glDrawElements(GL_TRIANGLES, (i32)array_len(mes->indices), GL_UNSIGNED_INT, 0)); }
	else { glCall(glDrawArrays(GL_TRIANGLES, 0, (i32)array_len(mes->positions))); }

	glCall(glBindVertexArray(0));
}

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

#include "renderer/toshibasat8x8.h"

static u32
sm__get_sprite_index(struct atlas_sprite *sprite_desc, u32 sprite_count, i32 char_value)
{
	u32 result = 0;

	for (u32 i = 0; i < sprite_count; i++)
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

	struct resource *resource = resource_get_by_name(str8_from("toshibasat8x8"));
	sm__assert(resource && resource->type == RESOURCE_IMAGE && resource->image_data);
	struct image_resource *image = resource->image_data;

	if (image->texture_handle == 0) { renderer_upload_texture(image); }

	u32 win_width = RC.framebuffer.width;

	sm__renderer_begin(GL_QUADS);
	{
		sm__color(c);

		f32 advance_x = 0;
		f32 advance_y = 0;
		for (const i8 *p = text.idata; *p; p++)
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
// 		for (const i8 *p = text.idata; *p; p++)
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
// 		for (const i8 *p = text.idata; *p; p++)
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
	for (u32 ring = 0; ring < rings; ring++)
	{
		for (u32 slice = 0; slice < slices; slice++)
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
		for (u32 uj = 0; uj < slices; uj++)
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

void
renderer_on_resize(u32 width, u32 height)
{
	glCall(glViewport(0, 0, (i32)width, (i32)height));

	m4 ortho;
	sm__m4_mode(MATRIX_MODE_PROJECTION);
	sm__m4_load_identity();
	glm_ortho(0, (f32)width, (f32)height, 0, -1.0f, 1.0f, ortho.data);
	sm__m4_mul(ortho.data);

	sm__m4_mode(MATRIX_MODE_MODEL_VIEW);
	sm__m4_load_identity();
}

void
renderer_set_clear_color(color c)
{
	RC.clear_color = c;
}

void
renderer_clear_color(void)
{
	v4 c = color_to_v4(RC.clear_color);
	glCall(glClearColor(c.r, c.g, c.b, c.a));
}

void
renderer_clear_color_buffer(void)
{
	glClear(GL_COLOR_BUFFER_BIT);
}

void
renderer_clear_depth_buffer(void)
{
	glClear(GL_DEPTH_BUFFER_BIT);
}
