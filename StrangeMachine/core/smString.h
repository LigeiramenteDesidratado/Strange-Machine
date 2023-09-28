#ifndef SM_CORE_STRING_H
#define SM_CORE_STRING_H

#include "core/smBase.h"

struct arena;

typedef struct
{
	union
	{
		u8 *data;
		const u8 *cdata;
		i8 *idata;
		const i8 *cidata;
	};

	u32 size;
} str8;

#define str8_from(S) ((str8){.idata = S, .size = sizeof(S) - 1})
str8 str8_from_cstr(struct arena *arena, const char *cstr);
#define str8_from_cstr_stack(S) ((str8){.idata = S, .size = strlen(S)})
void str8_release(struct arena *arena, str8 *str);

#define strfy_(a) #a
#define strfy(a)  strfy_(a)

#define str8fy(a) str8_from(strfy(a))

#define str8_empty_const	   str8_from("")
#define str8_backspace_const	   str8_from("\b")
#define str8_newline_const	   str8_from("\n")
#define str8_carriage_return_const str8_from("\r")
#define str8_horizontal_tab_const  str8_from("\t")
#define str8_vertical_tab_const	   str8_from("\b")
#define str8_backslash_const	   str8_from("\\")
#define str8_single_quote_const	   str8_from("\'")
#define str8_double_quote_const	   str8_from("\"")

#define str8_positive_infinite_const str8_from("+INF")
#define str8_negative_infinite_const str8_from("-INF")
#define str8_positive_nan_const	     str8_from("+NAN")
#define str8_negative_nan_const	     str8_from("-NAN")

struct str8_buf
{
	u8 *data;
	u32 len;
	u32 cap;
};

b8 f64isnan(f64 value);
b8 f64isinf(f64 value);
b8 f64isnegative(f64 value);

str8 f64tostr(f64 value, u32 precision);
#define f32tostr(_value, _base) (f32) f64tostr((f64)(_value), _base);

str8 i32tostr(i32 value, i32 base);
str8 i64tostr(i64 value, i32 base);
#define i16tostr(_value, _base) (i16) i32tostr((i32)(_value), _base)
#define i8tostr(_value, _base)	(i8) i32tostr((i32)(_value), _base)

str8 u32tostr(u32 value, i32 base);
str8 u64tostr(u64 value, i32 base);
#define u16tostr(_value, _base) (u16) u32tostr((u32)(_value), _base)
#define u8tostr(_value, _base)	(u8) u32tostr((u32)(_value), _base)

struct str8_buf str_buf_begin(struct arena *arena);
str8 str_buf_end(struct arena *arena, struct str8_buf str_buf);

void str_buf_append(struct arena *arena, struct str8_buf *str_buf, str8 append);
void str_buf_append_char(struct arena *arena, struct str8_buf *str_buf, i8 c);
#define str_buf_newline(S) (str_buf_append(S, str_newline_const));
void str_buf_insert(struct arena *arena, struct str8_buf *str_buf, str8 string, u32 index);
void str_buf_replace_insert(struct arena *arena, struct str8_buf *str_buf, str8 string, u32 index, u32 replace);

u32 str8_hash(str8 str);
u32 strc_hash(i8 *str);
str8 str8_dup(struct arena *arena, str8 str);
b8 str8_eq(str8 str1, str8 str2);

void str8_print(str8 s);
void str8_println(str8 s);
void str8_printf(struct arena *arena, str8 format, ...);
void str8_printfln(struct arena *arena, str8 format, ...);
void str8_vprintf(struct arena *arena, str8 format, va_list args);

str8 str8_format(struct arena *arena, str8 format, ...);

void str8_buffer_flush(void);

#endif // SM_CORE_STRING_H
