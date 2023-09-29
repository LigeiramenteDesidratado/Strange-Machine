#include "audio/smAudio.h"
#include "core/smBaseMemory.h"
#include "ecs/smStage.h"
#include "renderer/smRenderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "vendor/glad/glad.h"

#include "core/smBase.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "core/smString.h"

static void glfw_window_resize_callback(GLFWwindow *window, i32 width, i32 height);
static void glfw_key_callback(GLFWwindow *win, i32 key, i32 scancode, i32 action, i32 mods);
static void glfw_mouse_button_callback(GLFWwindow *window, i32 button, i32 action, i32 mods);
static void glfw_char_callback(GLFWwindow *win, u32 scancode);
static void glfw_scroll_callback(GLFWwindow *window, f64 xoffset, f64 yoffset);
static void glfw_cursor_pos_callback(GLFWwindow *window, f64 xpos, f64 ypos);
static void glfw_window_pos_callback(GLFWwindow *window, i32 xpos, i32 ypos);

static inline void core_input_clear(void);

// Window
struct window
{
	str8 title;
	GLFWwindow *glfw_handle;
	u32 width, height;
	u32 framebuffer_width, framebuffer_height;
	i32 x, y;
};

// Input
struct input
{
	struct
	{
		u32 pressed : 1;
		u32 locked : 31;

	} keys[MAX_KEYBOARD_KEYS];

	struct
	{
		u32 button;

		f32 x, y;
		f32 x_offset, y_offset;

		f32 scroll;
	} cursor;
};

// Time
struct time
{
	f32 current;  // Current time measure
	f32 previous; // Previous time measure
	f32 update;   // Time measure for frame update
	f32 draw;     // Time measure for frame draw
	f32 frame;    // Time measure for one frame
	f32 target;   // Desired time for one frame, if 0 not applied
	f32 target_fps;
	f32 fixed_dt;
	f32 fixed_fps;
	u32 frame_counter; // Frame counter
};

struct core
{
	enum
	{
		CORE_MEMORY = BIT(0),
		CORE_STR8 = BIT(1),
		CORE_LOG = BIT(2),
		CORE_WINDOW = BIT(3),
		CORE_RESOURCE = BIT(4),
		CORE_SOUND = BIT(5),
		CORE_RENDERER = BIT(6),
		CORE_STAGE = BIT(7),

		SM__CORE_ENFORCE_ENUM_SIZE = 0x7fffffff
	} modules;

	struct window window;
	struct input input;
	struct time time;

	struct stack_layer layers;

	struct arena user_arena;
	void *user_data;
};

static struct core CC; // Context Core

static b8
window_init(str8 title, u32 width, u32 height)
{
	if (!glfwInit())
	{
		str8_println(str8_from("error initializing glfw"));
		return (false);
	}

	glfwWindowHint(GLFW_POSITION_X, (int)(1360 / 2) - (int)(width / 2));
	glfwWindowHint(GLFW_POSITION_Y, (int)(768 / 2) - (int)(height / 2));
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

	// glfwGetWindowPos(handle, int *xpos, int *ypos)

	GLFWwindow *handle = 0;
	handle = glfwCreateWindow((i32)width, (i32)height, title.idata, 0, 0);
	if (!handle)
	{
		glfwTerminate();
		str8_println(str8_from("error creating glfw window"));
		return (false);
	}

	glfwMakeContextCurrent(handle);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	glfwSetKeyCallback(handle, glfw_key_callback);
	// glfwSetCharCallback(handle, glfw_char_callback);
	glfwSetMouseButtonCallback(handle, glfw_mouse_button_callback);
	glfwSetScrollCallback(handle, glfw_scroll_callback);
	glfwSetWindowSizeCallback(handle, glfw_window_resize_callback);
	glfwSetCursorPosCallback(handle, glfw_cursor_pos_callback);
	glfwSetWindowPosCallback(handle, glfw_window_pos_callback);

	CC.window.glfw_handle = handle;
	CC.window.title = title;
	CC.window.width = width;
	CC.window.height = height;

	return (true);
}

static void
window_teardown(struct window *win)
{
	glfwDestroyWindow(win->glfw_handle);
	glfwTerminate();
}

static void
core__at_exit(void)
{
	str8_buffer_flush();
	mm__print();
}

static void
sm__core_teardown_modules(void)
{
	if (CC.modules & CORE_STAGE) { stage_teardown(); }
	if (CC.modules & CORE_RENDERER) { renderer_teardown(); }
	if (CC.modules & CORE_RESOURCE) { resource_manager_teardown(); }
	if (CC.modules & CORE_SOUND) { audio_manager_teardown(); }
	if (CC.modules & CORE_WINDOW) { window_teardown(&CC.window); }
	if (CC.modules & CORE_LOG) { log_teardown(); }
	if (CC.modules & CORE_STR8) { str8_teardown(); }
	if (CC.modules & CORE_MEMORY) { base_memory_teardown(); }
}

b8
core_init(struct core_init *core_init)
{
	assert(core_init);

	u32 base_size = MAX(core_init->total_memory, MB(4));
	if (!base_memory_init(base_size))
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing core"));
		return (false);
	}

	CC.modules |= CORE_MEMORY;

	if (!str8_init())
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing str8"));
		return (false);
	}
	CC.modules |= CORE_STR8;

	atexit(core__at_exit);

	if (!log_init())
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing log"));
		return (false);
	}
	CC.modules |= CORE_LOG;

	if (!window_init(core_init->title, core_init->w, core_init->h))
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing window"));
		return (false);
	}
	CC.modules |= CORE_WINDOW;

	if (!resource_manager_init(core_init->argv, core_init->assets_folder))
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing resource"));
		return (false);
	}
	CC.modules |= CORE_RESOURCE;

	if (!audio_manager_init())
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing sound"));
		return (false);
	}
	CC.modules |= CORE_SOUND;

	if (!renderer_init(core_init->framebuffer_w, core_init->framebuffer_h))
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing renderer"));
		return (false);
	}
	CC.window.framebuffer_width = core_init->framebuffer_w;
	CC.window.framebuffer_height = core_init->framebuffer_h;
	CC.modules |= CORE_RENDERER;
	renderer_on_resize(core_init->w, core_init->h);

	struct buf m_scene = base_memory_reserve(MB(8));
	if (!stage_init(m_scene))
	{
		sm__core_teardown_modules();
		str8_println(str8_from("error initializing stage"));
		return (false);
	}
	CC.modules |= CORE_STAGE;

	for (u8 i = 0; i < core_init->num_layers; ++i)
	{
		struct layer layer = layer_make(core_init->layer_init[i].name, core_init->layer_init[i].user_data,
		    core_init->layer_init[i].on_attach, core_init->layer_init[i].on_update,
		    core_init->layer_init[i].on_draw, core_init->layer_init[i].on_detach);

		stack_layer_push(&CC.layers, layer);
	}

	// renderer_init(core_init->w, core_init->h);

	if (core_init->target_fps < 1) { CC.time.target = 0.0; }
	else { CC.time.target = 1.0f / (f32)core_init->target_fps; }

	core_set_fixed_fps(core_init->fixed_fps);

	u64 seed = core_init->prng_seed ? core_init->prng_seed : 42;
	prng_seed(seed);

	CC.user_data = core_init->user_data;

	arena_make(&CC.user_arena, base_memory_reserve(MB(3)));
	arena_validate(&CC.user_arena);

	struct ctx ctx = {
	    .dt = CC.time.frame,
	    .fixed_dt = CC.time.fixed_dt,
	    .win_width = CC.window.width,
	    .win_height = CC.window.height,
	    .framebuffer_width = CC.window.framebuffer_width,
	    .framebuffer_height = CC.window.framebuffer_height,
	    .arena = &CC.user_arena,
	    .user_data = CC.user_data,
	};

	u32 stack_layer_len = stack_layer_get_len(&CC.layers);
	for (u32 i = 0; i < stack_layer_len; ++i)
	{
		struct layer *layer = stack_layer_get_layer(&CC.layers, i);
		if (layer->on_attach) { layer->on_attach(&ctx); }
	}

	f32 cursor_pos_x = (f32)core_init->w / 2.0f;
	f32 cursor_pos_y = (f32)core_init->h / 2.0f;

	CC.input.cursor.x = cursor_pos_x;
	CC.input.cursor.y = cursor_pos_y;
	glfwSetCursorPos(CC.window.glfw_handle, (f64)cursor_pos_x, (f64)cursor_pos_y);

	str8_buffer_flush();

	return (true);
}

void
core_teardown(void)
{
	struct ctx ctx = {
	    .dt = CC.time.frame,
	    .fixed_dt = CC.time.fixed_dt,
	    .win_width = CC.window.width,
	    .win_height = CC.window.height,
	    .framebuffer_width = CC.window.framebuffer_width,
	    .framebuffer_height = CC.window.framebuffer_height,
	    .user_data = CC.user_data,
	};

	u32 stack_layer_len = stack_layer_get_len(&CC.layers);
	for (u32 i = 0; i < stack_layer_len; ++i)
	{
		struct layer *layer = stack_layer_get_layer(&CC.layers, i);
		layer->on_detach(&ctx);
	}

	renderer_teardown();

	audio_manager_teardown();

	resource_manager_teardown();

	stack_layer_release(&CC.layers);

	window_teardown(&CC.window);

	log_teardown();

	str8_teardown();

	base_memory_teardown();
}

void
core_wait(f32 seconds)
{
#define SUPPORT_PARTIALBUSY_WAIT_LOOP
#if defined(SUPPORT_BUSY_WAIT_LOOP) || defined(SUPPORT_PARTIALBUSY_WAIT_LOOP)
	f32 destinationTime = glfwGetTime() + seconds;
#endif

#if defined(SUPPORT_BUSY_WAIT_LOOP)
	while (GetTime() < destinationTime) {}
#else
#	if defined(SUPPORT_PARTIALBUSY_WAIT_LOOP)
	f32 sleepSeconds = seconds - seconds * 0.05; // NOTE: We reserve a percentage of the time for busy waiting
#	else
	f64 sleepSeconds = seconds;
#	endif

// System halt functions
#	if defined(_WIN32)
	Sleep((unsigned long)(sleepSeconds * 1000.0));
#	endif
#	if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
	struct timespec req = {0};
	time_t sec = sleepSeconds;
	long nsec = (sleepSeconds - sec) * 1000000000L;
	req.tv_sec = sec;
	req.tv_nsec = nsec;

	// NOTE: Use nanosleep() on Unix platforms... usleep() it's deprecated.
	while (nanosleep(&req, &req) == -1) continue;
#	endif
#	if defined(__APPLE__)
	usleep(sleepSeconds * 1000000.0);
#	endif

#	if defined(SUPPORT_PARTIALBUSY_WAIT_LOOP)
	while (glfwGetTime() < destinationTime) {}
#	endif
#endif
#undef SUPPORT_PARTIALBUSY_WAIT_LOOP
}

u32
core_get_fps(void)
{
	u32 fps = 0;

#define FPS_CAPTURE_FRAMES_COUNT 30   // 30 captures
#define FPS_AVERAGE_TIME_SECONDS 0.5f // 500 millisecondes
#define FPS_STEP		 (FPS_AVERAGE_TIME_SECONDS / FPS_CAPTURE_FRAMES_COUNT)

	static u32 index = 0;
	static f32 history[FPS_CAPTURE_FRAMES_COUNT] = {0};
	static f32 average = 0, last = 0;
	f32 fpsFrame = CC.time.frame;

	if (fpsFrame == 0) { return (0); }

	if (((f32)glfwGetTime() - last) > FPS_STEP)
	{
		last = (float)glfwGetTime();
		index = (index + 1) % FPS_CAPTURE_FRAMES_COUNT;
		average -= history[index];
		history[index] = fpsFrame / FPS_CAPTURE_FRAMES_COUNT;
		average += history[index];
	}

	fps = (u32)roundf(1.0f / average);

#undef FPS_CAPTURE_FRAMES_COUNT
#undef FPS_AVERAGE_TIME_SECONDS
#undef FPS_STEP

	return (fps);
}

void
core_set_fixed_fps(u32 fps)
{
	CC.time.fixed_fps = MAX(24.0f, (f32)fps);
	CC.time.fixed_dt = 1.0f / CC.time.fixed_fps;
}

void *
core_set_user_data(void *user_data)
{
	void *old_user_data = CC.user_data;
	CC.user_data = user_data;

	return (old_user_data);
}

b8
core_button_pressed(u32 button)
{
	return (CC.input.cursor.button & button);
}

b8
core_key_pressed(u32 key)
{
	assert(key < MAX_KEYBOARD_KEYS);
	return (!CC.input.keys[key].locked && CC.input.keys[key].pressed);
}

b8
core_key_pressed_lock(u32 key, u32 frames)
{
#define MAX_LOCK 2147483648
#define MIN_LOCK 0
	assert(key < MAX_KEYBOARD_KEYS);

	frames = MIN(frames, MAX_LOCK);
	frames = MAX(frames, MIN_LOCK);
	if (!CC.input.keys[key].locked && CC.input.keys[key].pressed)
	{
		CC.input.keys[key].locked = frames;
		return (true);
	}

	return (false);
}

v2
core_get_window_cursor_position(void)
{
	return (v2_new(CC.input.cursor.x, CC.input.cursor.y));
}

v2
core_get_screen_cursor_position(void)
{
	v2 result;

	f64 x, y;
	glfwGetCursorPos(CC.window.glfw_handle, &x, &y);

	result.x = CC.window.x + x + 2;
	result.y = CC.window.y + y + 2;

	return (result);
}

b8
core_is_cursor_in_window(void)
{
	b8 result;

	result = (b8)glfwGetWindowAttrib(CC.window.glfw_handle, GLFW_HOVERED);

	return (result);
}

v2
core_get_cursor_offset(void)
{
	v2 result;

	result = v2_new(CC.input.cursor.x_offset, CC.input.cursor.y_offset);

	return (result);
}

v2
core_get_cursor_pos2(void)
{
	f64 x, y;
	(glfwGetCursorPos(CC.window.glfw_handle, &x, &y));

	return v2_new((f32)x, (f32)y);
}

void
core_set_cursor_pos(v2 position)
{
	glfwSetCursorPos(CC.window.glfw_handle, position.x, position.y);
	glfw_cursor_pos_callback(CC.window.glfw_handle, position.x, position.y);
}

f32
core_get_scroll(void)
{
	return (CC.input.cursor.scroll);
}

u32
core_get_window_width(void)
{
	return (CC.window.width);
}

u32
core_get_window_height(void)
{
	return (CC.window.height);
}

u32
core_get_framebuffer_width(void)
{
	return (CC.window.framebuffer_width);
}

u32
core_get_framebuffer_height(void)
{
	return (CC.window.framebuffer_height);
}

i32
core_get_window_x(void)
{
	return (CC.window.x);
}

i32
core_get_window_y(void)
{
	return (CC.window.y);
}

f64
core_get_time(void)
{
	return (glfwGetTime());
}

void
core_main_loop(void)
{
	while (!glfwWindowShouldClose(CC.window.glfw_handle))
	{
		struct ctx ctx = {
		    .time = glfwGetTime(),
		    .dt = CC.time.frame,
		    .fixed_dt = CC.time.fixed_dt,
		    .win_width = CC.window.width,
		    .win_height = CC.window.height,
		    .framebuffer_width = CC.window.framebuffer_width,
		    .framebuffer_height = CC.window.framebuffer_height,
		    .arena = &CC.user_arena,
		    .user_data = CC.user_data,
		};

		u32 stack_layer_len = stack_layer_get_len(&CC.layers);
		for (u32 i = 0; i < stack_layer_len; ++i)
		{
			struct layer *layer = stack_layer_get_layer(&CC.layers, i);
			layer->on_update(&ctx);
		}

		stage_do(&ctx);

		CC.time.current = (f32)glfwGetTime(); // Number of elapsed seconds since InitTimer()
		CC.time.update = CC.time.current - CC.time.previous;
		CC.time.previous = CC.time.current;

		ctx = (struct ctx){
		    .time = CC.time.current,
		    .dt = CC.time.frame,
		    .fixed_dt = CC.time.fixed_dt,
		    .win_width = CC.window.width,
		    .win_height = CC.window.height,
		    .framebuffer_width = CC.window.framebuffer_width,
		    .framebuffer_height = CC.window.framebuffer_height,
		    .arena = &CC.user_arena,
		    .user_data = CC.user_data,
		};

		renderer_start_frame();

		for (u32 i = 0; i < stack_layer_len; ++i)
		{
			struct layer *layer = stack_layer_get_layer(&CC.layers, i);
			layer->on_draw(&ctx);
		}
		renderer_finish_frame();

		// Swap front and back buffers
		glfwSwapBuffers(CC.window.glfw_handle);

		core_input_clear();

		CC.time.current = (f32)glfwGetTime();
		CC.time.draw = CC.time.current - CC.time.previous;
		CC.time.previous = CC.time.current;

		CC.time.frame = CC.time.update + CC.time.draw;

		if (CC.time.frame < CC.time.target)
		{
			core_wait(CC.time.target - CC.time.frame);

			CC.time.current = (f32)glfwGetTime();
			f32 waitTime = CC.time.current - CC.time.previous;
			CC.time.previous = CC.time.current;

			CC.time.frame += waitTime; // Total frame time: update + draw + wait
		}

		str8_buffer_flush();
		glfwPollEvents();
	}
}

void
core_hide_cursor(void)
{
	glfwSetInputMode(CC.window.glfw_handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void
core_show_cursor(void)
{
	glfwSetInputMode(CC.window.glfw_handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

b8
core_is_cursor_hidden(void)
{
	i32 mode = glfwGetInputMode(CC.window.glfw_handle, GLFW_CURSOR);
	return (mode == GLFW_CURSOR_DISABLED);
}

static inline void
core_input_clear(void)
{
	CC.input.cursor.scroll = 0;

	CC.input.cursor.x_offset = 0;
	CC.input.cursor.y_offset = 0;

	for (u32 i = 0; i < MAX_KEYBOARD_KEYS; ++i)
	{
		if (CC.input.keys[i].locked) { CC.input.keys[i].locked--; }
	}
}

static void
glfw_key_callback(sm__maybe_unused GLFWwindow *win, sm__maybe_unused i32 key, sm__maybe_unused i32 scancode,
    sm__maybe_unused i32 action, sm__maybe_unused i32 mods)
{
	// printf("key: %2c, scncode: %4d, action: %4d, mods: %4d\n", key, scancode, action, mods);
	// printf("code: %d, (%s)\n", scancode, glfwGetKeyName(key, scancode));

	if (key == GLFW_KEY_UNKNOWN) { return; }

	switch (action)
	{
	case GLFW_REPEAT: /* fallthrough */
	case GLFW_PRESS:
		{
			if (CC.input.keys[key].locked == 0) { CC.input.keys[key].pressed = 1; }
			break;
		}
	case GLFW_RELEASE: CC.input.keys[key].pressed = 0; break;
	default: sm__unreachable();
	}
}

static void
glfw_mouse_button_callback(sm__maybe_unused GLFWwindow *window, sm__maybe_unused i32 button,
    sm__maybe_unused i32 action, sm__maybe_unused i32 mods)
{
	static u32 ctable[] = {
	    [GLFW_MOUSE_BUTTON_1] = MOUSE_BUTTON_1,
	    [GLFW_MOUSE_BUTTON_2] = MOUSE_BUTTON_2,
	    [GLFW_MOUSE_BUTTON_3] = MOUSE_BUTTON_3,
	    [GLFW_MOUSE_BUTTON_4] = MOUSE_BUTTON_4,
	    [GLFW_MOUSE_BUTTON_5] = MOUSE_BUTTON_5,
	    [GLFW_MOUSE_BUTTON_6] = MOUSE_BUTTON_6,
	    [GLFW_MOUSE_BUTTON_7] = MOUSE_BUTTON_7,
	    [GLFW_MOUSE_BUTTON_8] = MOUSE_BUTTON_8,
	};

	switch (action)
	{
	case GLFW_PRESS: CC.input.cursor.button |= ctable[button]; break;
	case GLFW_RELEASE: CC.input.cursor.button &= ~ctable[button]; break;
	default: sm__unreachable();
	}
}

static void
glfw_window_resize_callback(
    sm__maybe_unused GLFWwindow *window, sm__maybe_unused i32 width, sm__maybe_unused i32 height)
{
	CC.window.width = (u32)width;
	CC.window.height = (u32)height;

	renderer_on_resize(CC.window.width, CC.window.height);
}

static void
glfw_scroll_callback(sm__maybe_unused GLFWwindow *window, sm__maybe_unused f64 xoffset, sm__maybe_unused f64 yoffset)
{
	CC.input.cursor.scroll = (f32)yoffset;
}

static void
glfw_cursor_pos_callback(sm__maybe_unused GLFWwindow *window, sm__maybe_unused f64 xpos, sm__maybe_unused f64 ypos)
{
	CC.input.cursor.x_offset = xpos - CC.input.cursor.x;
	CC.input.cursor.y_offset = ypos - CC.input.cursor.y;

	CC.input.cursor.x = (f32)xpos;
	CC.input.cursor.y = (f32)ypos;
}

static void
glfw_window_pos_callback(sm__maybe_unused GLFWwindow *window, sm__maybe_unused i32 xpos, sm__maybe_unused i32 ypos)
{
	CC.window.x = xpos;
	CC.window.y = ypos;
}
