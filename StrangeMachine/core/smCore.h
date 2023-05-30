#ifndef SM_CORE_H
#define SM_CORE_H

#include "core/smArena.h"
#include "core/smBase.h"
#include "core/smBaseMemory.h"
#include "core/smMM.h"
#include "core/smString.h"
#include "math/smMath.h"

#define sm__debug_args	 , str8_from(sm__file_name), sm__file_line
#define sm__debug_params , str8 file, u32 line

struct ctx
{
	f32 dt;
	f32 fixed_dt;
	u32 win_width, win_height;
	struct arena *arena;

	void *user_data;
};

typedef void (*layer_f)(struct ctx *ctx);

// Layer
struct layer
{
	str8 name;
	void *user_data;

	layer_f on_attach;
	layer_f on_update;
	layer_f on_draw;
	layer_f on_detach;
};

struct layer layer_make(
    str8 name, void *user_data, layer_f on_attach, layer_f on_update, layer_f on_draw, layer_f on_detach);
void layer_release(struct layer *layer);

// Stack Layers
struct stack_layer
{
	u32 layer_len;
	struct layer layers[8];
	u32 overlayer_len;
	struct layer overlayers[8];
};

struct stack_layer stack_layer_make(void);
void stack_layer_release(struct stack_layer *stack_layer);

void stack_layer_push(struct stack_layer *stack_layer, struct layer layer);
void stack_layer_pop(struct stack_layer *stack_layer);
void stack_layer_push_overlayer(struct stack_layer *stack_layer, struct layer overlayer);
void stack_layer_pop_overlayer(struct stack_layer *stack_layer);

u32 stack_layer_get_len(struct stack_layer *stack_layer);
struct layer *stack_layer_get_layer(struct stack_layer *stack_layer, u32 index);

// Base memory
struct dyn_buf
{
	u8 *data;

	u32 len;
	u32 cap;
};

struct buf
{
	u8 *data;

	u32 size;
};

b8 base_memory_init(u32 size);
void base_memory_teardown(void);
struct buf base_memory_reserve(u32 size);
struct buf base_memory_begin(void);
void base_memory_end(u32 size);
void base_memory_reset(void);

// Arena

struct arena
{
	struct buf base_memory;

	void *tlsf;
	void *tlsf_pool;

	void *mem;
};

void sm__arena_make(struct arena *alloc, struct buf base_memory, str8 file, u32 line);
void sm__arena_release(struct arena *alloc sm__debug_params);

void *sm__arena_malloc(struct arena *alloc, u32 size sm__debug_params);
void *sm__arena_realloc(struct arena *alloc, void *ptr, u32 size sm__debug_params);
void *sm__arena_aligned(struct arena *alloc, u32 align, u32 size sm__debug_params);
void sm__arena_free(struct arena *alloc, void *ptr sm__debug_params);
void sm__arena_validate(struct arena *arena sm__debug_params);
u32 sm__arena_get_overhead_size(void);

#define arena_make(_arena, _base_mem)	     sm__arena_make((_arena), (_base_mem)sm__debug_args)
#define arena_release(_arena)		     sm__arena_release((_arena)sm__debug_args)
#define arena_reserve(_arena, _size)	     sm__arena_malloc((_arena), (_size)sm__debug_args)
#define arena_aligned(_arena, _align, _size) sm__arena_aligned((_arena), (_align), (_size)sm__debug_args)
#define arena_resize(_arena, _ptr, _size)    sm__arena_realloc((_arena), (_ptr), (_size)sm__debug_args)
#define arena_free(_arena, _ptr)	     sm__arena_free((_arena), (_ptr)sm__debug_args)
#define arena_validate(_arena)		     sm__arena_validate((_arena)sm__debug_args)
#define arena_get_overhead_size()	     sm__arena_get_overhead_size();

// Array
struct sm__array_header
{
	u32 len;
	u32 cap;
	size_t __padding;
};

#define array(type)	      type *
#define sm__array_header_size (sizeof(struct sm__array_header))
#define sm__a2r(_ptr)	      ((struct sm__array_header *)(_ptr)-1)
#define sm__r2a(_ptr)	      ((struct sm__array_header *)(_ptr) + 1)

void *sm__array_make2(struct arena *arena, u32 cap, u32 item_size sm__debug_params);
void sm__array_release2(struct arena *arena, void *ptr sm__debug_params);
void *sm__array_set_len2(struct arena *arena, void *ptr, u32 len, u32 size sm__debug_params);
void *sm__array_set_cap2(struct arena *arena, void *ptr, u32 cap, u32 size sm__debug_params);
void *sm__array_push2(struct arena *arena, void *ptr, void *value, size_t size sm__debug_params);
void *sm__array_pop2(void *ptr);
void *sm__array_copy2(struct arena *arena, void *dest_ptr, const void *src_ptr, size_t item_size sm__debug_params);

#define array_make(_arena, _ptr, _cap)                                                                            \
	do {                                                                                                      \
		if ((_ptr) || !(_cap)) { continue; }                                                              \
		struct sm__array_header *raw = sm__array_make2((_arena), (_cap), sizeof(*(_ptr)) sm__debug_args); \
		(_ptr) = (typeof((_ptr)))sm__r2a(raw);                                                            \
	} while (0)

#define array_release(_arena, _ptr)                                       \
	do {                                                              \
		if (_ptr)                                                 \
		{                                                         \
			struct sm__array_header *raw = sm__a2r(_ptr);     \
			sm__array_release2((_arena), raw sm__debug_args); \
			_ptr = 0;                                         \
		}                                                         \
	} while (0)

#define array_set_len(_arena, _ptr, _len)                                                                          \
	do {                                                                                                       \
		if (!(_ptr) && (_len) == 0) { continue; }                                                          \
		struct sm__array_header *raw =                                                                     \
		    !(_ptr) ? sm__array_make2((_arena), (_len), sizeof(*(_ptr)) sm__debug_args) : sm__a2r((_ptr)); \
		raw = sm__array_set_len2((_arena), raw, (_len), sizeof(*(_ptr)) sm__debug_args);                   \
		(_ptr) = (typeof((_ptr)))sm__r2a(raw);                                                             \
	} while (0)

#define array_set_cap(_arena, _ptr, _cap)                                                                          \
	do {                                                                                                       \
		if (!(_ptr) && (_cap) == 0) { continue; }                                                          \
		struct sm__array_header *raw =                                                                     \
		    !(_ptr) ? sm__array_make2((_arena), (_cap), sizeof(*(_ptr)) sm__debug_args) : sm__a2r((_ptr)); \
		raw = sm__array_set_cap2((_arena), raw, (_cap), sizeof(*(_ptr)) sm__debug_args);                   \
		(_ptr) = (typeof((_ptr)))sm__r2a(raw);                                                             \
	} while (0)

#define array_len(_ptr) ((_ptr) == 0 ? 0 : (((struct sm__array_header *)(_ptr)) - 1)->len)
#define array_cap(_ptr) ((_ptr) == 0 ? 0 : (((struct sm__array_header *)(_ptr)) - 1)->cap)

#define array_push(_arena, _ptr, _value)                                                                      \
	do {                                                                                                  \
		struct sm__array_header *raw =                                                                \
		    !(_ptr) ? sm__array_make2((_arena), 1, sizeof(*(_ptr)) sm__debug_args) : sm__a2r((_ptr)); \
		sm__static_type_assert(*(_ptr), (_value));                                                    \
		raw = sm__array_push2((_arena), raw, &(_value), sizeof((_value)) sm__debug_args);             \
		(_ptr) = (typeof((_ptr)))sm__r2a(raw);                                                        \
	} while (0)

/* WARNING: do not use array_pop inside a loop that depends array_len.
 * like:
 *  for (u32 i = 0; i < array_len(array); ++i) {
 *    // ...
 *    array_pop(array);
 *  }
 */
#define array_pop(_ptr)                                                 \
	do {                                                            \
		if (_ptr)                                               \
		{                                                       \
			struct sm__array_header *raw = sm__a2r((_ptr)); \
			raw = sm__array_pop2(raw);                      \
		}                                                       \
	} while (0)

#define array_del(_ptr, i, n)                                                                                \
	do {                                                                                                 \
		assert((i) >= 0 && "negative index");                                                        \
		assert((n) >= -1);                                                                           \
		struct sm__array_header *raw = sm__a2r(_ptr);                                                \
		assert((i) < raw->len && "index out of range");                                              \
		if ((n) == 0) { continue; }                                                                  \
		if (((n) == -1))                                                                             \
		{                                                                                            \
			raw->len = i;                                                                        \
			continue;                                                                            \
		}                                                                                            \
		u32 ___nnn = ((i) + (n) >= raw->len) ? raw->len - (i) : (n);                                 \
		memmove(&(_ptr)[(i)], &(_ptr)[(i) + ___nnn], sizeof(*(_ptr)) * (raw->len - ((i) + ___nnn))); \
		raw->len = raw->len - ___nnn;                                                                \
	} while (0)

#define array_last_item(_ptr)                                       \
	((_ptr) == 0 ? 0                                            \
	    : ((((struct sm__array_header *)(_ptr)) - 1)->len == 0) \
		? 0                                                 \
		: (_ptr) + ((((struct sm__array_header *)(_ptr)) - 1)->len - 1))

// Doubly linked list
// thanks to Mr. 4th Programming
struct node
{
	struct node *next;
	struct node *prev;
};

#define dll_init_sentinel_NP_(s, next, prev)  s->next = s, s->prev = s
#define dll_insert_NP_(p, n1, n2, next, prev) n2->next = p->next, n1->prev = p, p->next->prev = n2, p->next = n1
#define dll_remove_NP_(n1, n2, next, prev)    n2->next->prev = n1->prev, n1->prev->next = n2->next, n2->next = n1->prev = 0

#define dll_init_sentinel_(s)		     dll_init_sentinel_NP_(s, next, prev)
#define dll_insert_(p, n)		     dll_insert_NP_(p, n, n, next, prev)
#define dll_insert_multiple_(p, n1, n2)	     dll_insert_NP_(p, n1, n2, next, prev)
#define dll_insert_back_(p, n)		     dll_insert_NP_(p, n, n, prev, next)
#define dll_insert_multiple_back_(p, n1, n2) dll_insert_NP_(p, n2, n1, prev, next)
#define dll_remove_(n)			     dll_remove_NP_(n, n, next, prev)
#define dll_remove_multiple_(n1, n2)	     dll_remove_NP_(n1, n2, next, prev)

#define dll_init_sentinel(s)		    (dll_init_sentinel_((s)))
#define dll_insert(p, n)		    (dll_insert_((p), (n)))
#define dll_insert_multiple(p, n1, n2)	    (dll_insert_multiple_((p), (n1), (n2)))
#define dll_insert_back(p, n)		    (dll_insert_back_((p), (n)))
#define dll_insert_multiple_back(p, n1, n2) (dll_insert_multiple_back_((p), (n1), (n2)))
#define dll_remove(n)			    (dll_remove_((n)))
#define dll_remove_multiple(n1, n2)	    (dll_remove_multiple_((n1), (n2)))

// Resource
b8 resource_manager_init(struct buf base_memory, char *argv[], str8 assets_folder);
void resource_manager_teardown(void);
struct arena *resource_get_arena(void);

// PRNG
void prng_seed(u64 seed);

f64 f64_range11(void);
f64 f64_range01(void);
f32 f32_range11(void);
f32 f32_range01(void);

// min inclusive, max exclusive
f32 f32_min_max(f32 min, f32 max);
f64 f64_min_max(f64 min, f64 max);
u64 u64_min_max(u64 min, u64 max);
i64 i64_min_max(i64 min, i64 max);
u32 u32_min_max(u32 min, u32 max);
i32 i32_min_max(i32 min, i32 max);

#define prng_min_max(MIN, MAX)          \
	_Generic((MIN), u32             \
		 : u32_min_max, u64     \
		 : u64_min_max, i32     \
		 : i32_min_max, i64     \
		 : i64_min_max, f32     \
		 : f32_min_max, f64     \
		 : f64_min_max, default \
		 : i32_min_max)(MIN, MAX)

// Initialize core
struct core_init
{
	i32 argc;
	i8 **argv;

	// window
	str8 title;
	u32 w, h;
	u32 total_memory;

	u32 target_fps; // Desired FPS 60, 30, 24
	u32 fixed_fps;	// Useful for physics
	u64 prng_seed;	// Pseudorandom number generator seed

	str8 assets_folder; // Add an archive or directory to the search path.

	void *user_data;

	u8 num_layers;

	struct
	{
		str8 name;
		void *user_data;

		layer_f on_attach;
		layer_f on_update;
		layer_f on_draw;
		layer_f on_detach;
	} layer_init[8];
};

b8 core_init(struct core_init *core_init);

void core_main_loop(void);
void core_teardown(void);

b8 core_key_pressed(u32 key);
b8 core_key_pressed_lock(u32 key);

v2 core_get_cursor_pos(void);
v2 core_get_cursor_last_pos(void);
v2 core_get_cursor_rel_pos(void);

f32 core_get_scroll(void);

void core_hide_cursor(void);
void core_show_cursor(void);

void core_wait(f32 seconds);

u32 core_get_fps(void);
void core_set_fixed_fps(u32 fps);
void *core_set_user_data(void *user_data);

/* Printable keys */
#define KEY_SPACE	  32
#define KEY_APOSTROPHE	  39 /* ' */
#define KEY_COMMA	  44 /* , */
#define KEY_MINUS	  45 /* - */
#define KEY_PERIOD	  46 /* . */
#define KEY_SLASH	  47 /* / */
#define KEY_0		  48
#define KEY_1		  49
#define KEY_2		  50
#define KEY_3		  51
#define KEY_4		  52
#define KEY_5		  53
#define KEY_6		  54
#define KEY_7		  55
#define KEY_8		  56
#define KEY_9		  57
#define KEY_SEMICOLON	  59 /* ; */
#define KEY_EQUAL	  61 /* = */
#define KEY_A		  65
#define KEY_B		  66
#define KEY_C		  67
#define KEY_D		  68
#define KEY_E		  69
#define KEY_F		  70
#define KEY_G		  71
#define KEY_H		  72
#define KEY_I		  73
#define KEY_J		  74
#define KEY_K		  75
#define KEY_L		  76
#define KEY_M		  77
#define KEY_N		  78
#define KEY_O		  79
#define KEY_P		  80
#define KEY_Q		  81
#define KEY_R		  82
#define KEY_S		  83
#define KEY_T		  84
#define KEY_U		  85
#define KEY_V		  86
#define KEY_W		  87
#define KEY_X		  88
#define KEY_Y		  89
#define KEY_Z		  90
#define KEY_LEFT_BRACKET  91  /* [ */
#define KEY_BACKSLASH	  92  /* \ */
#define KEY_RIGHT_BRACKET 93  /* ] */
#define KEY_GRAVE_ACCENT  96  /* ` */
#define KEY_WORLD_1	  161 /* non-US #1 */
#define KEY_WORLD_2	  162 /* non-US #2 */

/* Functeys */
#define KEY_ESCAPE	  256
#define KEY_ENTER	  257
#define KEY_TAB		  258
#define KEY_BACKSPACE	  259
#define KEY_INSERT	  260
#define KEY_DELETE	  261
#define KEY_RIGHT	  262
#define KEY_LEFT	  263
#define KEY_DOWN	  264
#define KEY_UP		  265
#define KEY_PAGE_UP	  266
#define KEY_PAGE_DOWN	  267
#define KEY_HOME	  268
#define KEY_END		  269
#define KEY_CAPS_LOCK	  280
#define KEY_SCROLL_LOCK	  281
#define KEY_NUM_LOCK	  282
#define KEY_PRINT_SCREEN  283
#define KEY_PAUSE	  284
#define KEY_F1		  290
#define KEY_F2		  291
#define KEY_F3		  292
#define KEY_F4		  293
#define KEY_F5		  294
#define KEY_F6		  295
#define KEY_F7		  296
#define KEY_F8		  297
#define KEY_F9		  298
#define KEY_F10		  299
#define KEY_F11		  300
#define KEY_F12		  301
#define KEY_F13		  302
#define KEY_F14		  303
#define KEY_F15		  304
#define KEY_F16		  305
#define KEY_F17		  306
#define KEY_F18		  307
#define KEY_F19		  308
#define KEY_F20		  309
#define KEY_F21		  310
#define KEY_F22		  311
#define KEY_F23		  312
#define KEY_F24		  313
#define KEY_F25		  314
#define KEY_KP_0	  320
#define KEY_KP_1	  321
#define KEY_KP_2	  322
#define KEY_KP_3	  323
#define KEY_KP_4	  324
#define KEY_KP_5	  325
#define KEY_KP_6	  326
#define KEY_KP_7	  327
#define KEY_KP_8	  328
#define KEY_KP_9	  329
#define KEY_KP_DECIMAL	  330
#define KEY_KP_DIVIDE	  331
#define KEY_KP_MULTIPLY	  332
#define KEY_KP_SUBTRACT	  333
#define KEY_KP_ADD	  334
#define KEY_KP_ENTER	  335
#define KEY_KP_EQUAL	  336
#define KEY_LEFT_SHIFT	  340
#define KEY_LEFT_CONTROL  341
#define KEY_LEFT_ALT	  342
#define KEY_LEFT_SUPER	  343
#define KEY_RIGHT_SHIFT	  344
#define KEY_RIGHT_CONTROL 345
#define KEY_RIGHT_ALT	  346
#define KEY_RIGHT_SUPER	  347
#define KEY_MENU	  348

#define MAX_KEYBOARD_KEYS KEY_MENU

#define MOUSE_BUTTON_1 BIT(0)
#define MOUSE_BUTTON_2 BIT(1)
#define MOUSE_BUTTON_3 BIT(2)
#define MOUSE_BUTTON_4 BIT(3)
#define MOUSE_BUTTON_5 BIT(4)
#define MOUSE_BUTTON_6 BIT(5)
#define MOUSE_BUTTON_7 BIT(6)
#define MOUSE_BUTTON_8 BIT(7)

#define MOUSE_BUTTON_LAST   MOUSE_BUTTON_8
#define MOUSE_BUTTON_LEFT   MOUSE_BUTTON_1
#define MOUSE_BUTTON_RIGHT  MOUSE_BUTTON_2
#define MOUSE_BUTTON_MIDDLE MOUSE_BUTTON_3

#endif
