#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smMM.h"
#include "core/smString.h"
#include "core/smThread.h"
#include "math/smMath.h"

static str8 str__conststr = str8_from("ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
static i8 str__strbuf[64 + 1];

static u8 str_buffer_data[1];

static struct dyn_buf str_buffer =
    (struct dyn_buf){.data = str_buffer_data, .cap = ARRAY_SIZE(str_buffer_data), .len = 0};

static struct mutex str8_mutex;

b8
str8_init(void)
{
	sync_mutex_init(&str8_mutex);

	return (true);
}

void
str8_teardown(void)
{
	sync_mutex_release(&str8_mutex);
}

void
str8_buffer_flush(void)
{
	sync_mutex_lock(&str8_mutex);
	if (str_buffer.len > 0)
	{
		write(1, str_buffer.data, str_buffer.len);
		str_buffer.len = 0;
	}
	sync_mutex_unlock(&str8_mutex);
}

static void
sm__buffer_push(str8 str)
{
	if (str_buffer.len + str.size > str_buffer.cap) { str8_buffer_flush(); }
	if (str.size > str_buffer.cap) { write(1, str.idata, str.size); }
	else
	{
		sync_mutex_lock(&str8_mutex);
		memcpy(str_buffer.data + str_buffer.len, str.data, str.size);
		str_buffer.len += str.size;
		sync_mutex_unlock(&str8_mutex);
	}
}

str8
i64tostr(i64 value, i32 base)
{
	str8 result = {0};

	// check that the base if valid
	if (base < 2 || base > 36) { return (result); }

	i8 *ptr = str__strbuf, *ptr1 = str__strbuf, tmp_char;
	i64 tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = str__conststr.idata[35 + (tmp_value - value * base)];
		result.size++;
	} while (value);

	// Apply negative sign
	if (tmp_value < 0)
	{
		*ptr++ = '-';
		result.size++;
	}

	*ptr-- = '\0';
	while (ptr1 < ptr)
	{
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}
	result.data = (u8 *)str__strbuf;

	return (result);
}

str8
i32tostr(i32 value, i32 base)
{
	str8 result = {0};

	// check that the base if valid
	if (base < 2 || base > 36) { return (result); }

	char *ptr = str__strbuf, *ptr1 = str__strbuf, tmp_char;
	i32 tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = str__conststr.idata[35 + (tmp_value - value * base)];
		result.size++;
	} while (value);

	// Apply negative sign
	if (tmp_value < 0)
	{
		*ptr++ = '-';
		result.size++;
	}
	*ptr-- = '\0';
	while (ptr1 < ptr)
	{
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}

	result.data = (u8 *)str__strbuf;

	return (result);
}

str8
u32tostr(u32 value, i32 base)
{
	str8 result = {0};

	// check that the base if valid
	if (base < 2 || base > 36) { return (result); }

	char *ptr = str__strbuf, *ptr1 = str__strbuf, tmp_char;
	uint32_t tmp_value;

	do {
		tmp_value = value;
		value /= (u32)base;
		*ptr++ = str__conststr.idata[35 + (tmp_value - value * (u32)base)];
		result.size++;
	} while (value);

	*ptr-- = '\0';
	while (ptr1 < ptr)
	{
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}

	result.data = (u8 *)str__strbuf;

	return (result);
}

str8
u64tostr(u64 value, i32 base)
{
	str8 result = {0};
	// check that the base if valid
	if (base < 2 || base > 36) { return (result); }

	i8 *ptr = str__strbuf, *ptr1 = str__strbuf, tmp_char;
	u64 tmp_value;

	do {
		tmp_value = value;
		value /= (u32)base;
		*ptr++ = str__conststr.idata[35 + (tmp_value - value * (u32)base)];
		result.size++;
	} while (value);

	*ptr-- = '\0';
	while (ptr1 < ptr)
	{
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}
	result.data = (u8 *)str__strbuf;

	return (result);
}

b8
f64isnan(f64 value)
{
	union
	{
		u64 x;
		f64 d;
	} pun = {.d = value};

	if (((pun.x >> 52) & 0x07FFULL) == 0x07FFULL) { return ((pun.x << 12) & 0xFFFFFFFFFFFFFFFFULL); }

	return (false);
}

b8
f64isinf(f64 value)
{
	union
	{
		u64 x;
		f64 d;
	} pun = {.d = value};

	if (((pun.x >> 52) & 0x07FFULL) == 0x07FFULL) { return ((pun.x << 12) & 0xFFFFFFFFFFFFFFFFULL) == 0; }

	return (false);
}

b8
f64isnegative(f64 value)
{
	union
	{
		u64 x;
		f64 d;
	} pun = {.d = value};

	return (pun.x & 0x8000000000000000ULL);
}

str8
f64tostr(f64 value, u32 precision)
{
	str8 result = {0};

	if (f64isinf(value))
	{
		result = f64isnegative(value) ? str8_negative_infinite_const : str8_positive_infinite_const;

		return (result);
	}

	if (f64isnan(value))
	{
		result = f64isnegative(value) ? str8_negative_nan_const : str8_positive_nan_const;

		return (result);
	}

	static i8 conversion[1076], intpart[311];
	u32 len = 0;
	f64 fp_int = 0.0, fp_frac = 0.0, fp_abs = fabs(value);

	if (value == 0)
	{
		conversion[len++] = '.';
		while (precision--) conversion[len++] = '0';
		goto result;
	}

	fp_frac = modf(fp_abs, &fp_int); // Separate integer/fractional parts

	while (fp_int > 0) // Convert integer part, if any
	{
		intpart[len++] = '0' + (i8)fmod(fp_int, 10);
		fp_int = floor(fp_int / 10);
	}
	if (value < 0) { intpart[len++] = '-'; }

	for (u32 i = 0; i < len; i++) { conversion[i] = intpart[len - i - 1]; } // Reverse the integer part, if any

	conversion[len++] = '.'; // Decimal point

	while (fp_frac > 0 && precision--) // Convert fractional part, if any
	{
		fp_frac *= 10;
		fp_frac = modf(fp_frac, &fp_int);
		conversion[len++] = '0' + (i8)fp_int;
	}

result:
	conversion[len] = '\0'; // String terminator
	result.idata = conversion;
	result.size = len;

	return (result);
}

static void
sm__str8_buf_grow_if(struct arena *arena, struct str8_buf *str_buf, u32 size)
{
	if (str_buf->len + size > str_buf->cap)
	{
		u32 new_cap = (str_buf->cap * 2) > str_buf->len + size ? (str_buf->cap * 2) : str_buf->len + size;
		str_buf->data = arena_resize(arena, str_buf->data, new_cap);
		str_buf->cap = new_cap;
	}
}

struct str8_buf
str_buf_begin(struct arena *arena)
{
	struct str8_buf result;

	result.data = arena_reserve(arena, 256);
	result.cap = 256;
	result.len = 0;

	return (result);
}

str8
str_buf_end(struct arena *arena, struct str8_buf str_buf)
{
	str8 result;

	result.data = arena_resize(arena, str_buf.data, str_buf.len + 1);
	if (str_buf.data != result.data) { memcpy(result.data, str_buf.data, str_buf.len); }

	result.data[str_buf.len] = 0;
	result.size = str_buf.len;

	return (result);
}

void
str_buf_append(struct arena *arena, struct str8_buf *str_buf, str8 append)
{
	sm__str8_buf_grow_if(arena, str_buf, append.size);

	u8 *data = str_buf->data + str_buf->len;
	memcpy(data, append.data, append.size);

	str_buf->len += append.size;
}

void
str_buf_append_char(struct arena *arena, struct str8_buf *str_buf, i8 c)
{
	sm__str8_buf_grow_if(arena, str_buf, sizeof(i8));

	u8 *data = str_buf->data + str_buf->len;
	*data = (u8)c;

	str_buf->len += sizeof(i8);
}

void
str_buf_insert(struct arena *arena, struct str8_buf *str_buf, str8 string, u32 index)
{
	assert(index <= str_buf->len);

	sm__str8_buf_grow_if(arena, str_buf, string.size);

	u8 *data = str_buf->data + index;
	u8 *next_data = data + string.size;

	memmove(next_data, data, str_buf->len - index);
	memcpy(data, string.data, string.size);

	str_buf->len += string.size;
}

void
str_buf_replace_insert(struct arena *arena, struct str8_buf *str_buf, str8 string, u32 index, u32 replace)
{
	assert(index <= str_buf->len);
	assert(index + replace <= str_buf->len);

	sm__str8_buf_grow_if(arena, str_buf, string.size);

	u8 *data = str_buf->data + index;
	u8 *next_data = data + string.size;

	memmove(next_data, data + replace, str_buf->len - index);
	memcpy(data, string.data, string.size);

	str_buf->len += (string.size - replace);
}

str8
str8_from_cstr(struct arena *arena, const char *cstr)
{
	str8 result;

	u32 cstr_len = (u32)strlen(cstr);
	i8 *name = arena_reserve(arena, cstr_len + 1);
	memcpy(name, cstr, cstr_len);
	name[cstr_len] = 0;
	result = (str8){.idata = name, .size = cstr_len};

	return (result);
}

void
str8_release(struct arena *arena, str8 *str)
{
	arena_free(arena, str->idata);
	str->data = 0;
	str->size = 0;
}

b8
str8_eq(str8 str1, str8 str2)
{
	if (str1.size == str2.size) { return (memcmp(str1.data, str2.data, str1.size) == 0); }

	return (false);
}

str8
str8_dup(struct arena *arena, str8 str)
{
	str8 result;

	result.data = arena_reserve(arena, str.size + 1);
	memcpy(result.data, str.data, str.size);

	result.data[str.size] = 0;

	result.size = str.size;

	return (result);
}

void
str8_print(str8 s)
{
	sm__buffer_push(s);
}

void
str8_println(str8 s)
{
	sm__buffer_push(s);
	sm__buffer_push(str8_newline_const);
}

u32
str8_hash(str8 str)
{
	u32 hash = 5381;

	u32 size = str.size;
	const u8 *p = str.data;

	while (size--) { hash = ((hash << 5) + hash) + *p++; /* hash * 33 + c */ }

	return (hash);
}

u32
strc_hash(i8 *str)
{
	u32 hash = 5381;
	u8 c;
	const u8 *p = (u8 *)str;

	while ((c = *p++) != '\0') { hash = ((hash << 5) + hash) + c; /* hash * 33 + c */ }

	return (hash);
}

static union
{
	u16 t;
	i8 c[2];
} format_table[] = {
    {.c = "s"}, // string   {s}
    {.c = "f"}, // float    {f}
    {.c = "d"}, // double   {d}
    {.c = "b"}, // boolean  {b}

    {.c = "i8"}, // signed 8  {i8(b|o|d|x)} -|
    {.c = "i1"}, // signed 16 {i1(b|o|d|x)} -|
    {.c = "i3"}, // signed 32 {i3(b|o|d|x)} -|
    {.c = "i6"}, // signed 64 {i6(b|o|d|x)} -|-- where (b|o|d|x) means binary, octal, decimal and hexadecimal

    {.c = "u8"}, // unsigned 8  {u8(b|o|d|x)} -|
    {.c = "u1"}, // unsigned 16 {u1(b|o|d|x)} -|
    {.c = "u3"}, // unsigned 32 {u3(b|o|d|x)} -|
    {.c = "u6"}, // unsigned 63 {u6(b|o|d|x)} -|-- where (b|o|d|x) means binary, octal, decimal and hexadecimal

    {.c = "v2"}, // vector 2 {v2}
    {.c = "v3"}, // vector 3 {v3}
    {.c = "v4"}, // vector 4 {v4}

    {.c = "cv"}, // color vector 4 {cv}
    {.c = "cx"}, // color hex      {cx}

};

#define FORMAT_TABLE_SIZE (sizeof(format_table) / sizeof(format_table[0]))

static str8
sm__str_format(struct arena *arena, str8 format, va_list args)
{
	str8 result = {0};

	struct str8_buf result_buf = str_buf_begin(arena);

	for (u32 i = 0; i < format.size; ++i)
	{
		if (format.data[i] == '{')
		{
			if (i + 2 < format.size && format.data[i + 2] == '}')
			{
				// u32 hash = str_hash((str){.data = &format.data[i + 1], .size = 1});
				i8 format_id = format.idata[i + 1];

				if (format_id == format_table[0].c[0]) // handle string
				{
					i += 2;
					str8 value = va_arg(args, str8);
					if (value.size == 0) { value = str8_from("NULL"); }
					str_buf_append(arena, &result_buf, value);
				}
				else if (format_id == format_table[1].c[0] ||
					 format_id == format_table[2].c[0]) // handle float and double
				{
					i += 2;
					f64 value = va_arg(args, f64);
					str8 s = f64tostr(value, 6);
					str_buf_append(arena, &result_buf, s);
				}
				else if (format_id == format_table[3].c[0]) // handle boolean
				{
					i += 2;
					b8 value = (b8)va_arg(args, i32);
					str8 s = (value) ? str8_from("true") : str8_from("false");
					str_buf_append(arena, &result_buf, s);
				}

				continue;
			}

			else if (i + 3 < format.size && format.data[i + 3] == '}')
			{
				u16 format_id = *(u16 *)&format.data[i + 1];

				if (format_id == format_table[12].t) // handle v2
				{
					i += 3;
					v2 value = va_arg(args, v2);
					str8 x = f64tostr((f64)value.x, 6);
					str_buf_append(arena, &result_buf, x);
					str_buf_append(arena, &result_buf, str8_from(", "));

					str8 y = f64tostr((f64)value.y, 6);
					str_buf_append(arena, &result_buf, y);
				}
				else if (format_id == format_table[13].t) // handle v3
				{
					i += 3;
					v3 value = va_arg(args, v3);

					str8 x = f64tostr((f64)value.x, 6);
					str_buf_append(arena, &result_buf, x);
					str_buf_append(arena, &result_buf, str8_from(", "));

					str8 y = f64tostr((f64)value.y, 6);
					str_buf_append(arena, &result_buf, y);
					str_buf_append(arena, &result_buf, str8_from(", "));

					str8 z = f64tostr((f64)value.z, 6);
					str_buf_append(arena, &result_buf, z);
				}
				else if (format_id == format_table[14].t ||
					 format_id == format_table[15].t) // handle color v4
				{
					i += 3;
					v4 value;
					if (format_id == format_table[15].t)
					{
						color c = va_arg(args, color);
						value = color_to_v4(c);
					}
					else value = va_arg(args, v4);

					str8 comma_space = str8_from(", ");

					str8 x = f64tostr((f64)value.x, 6);
					str_buf_append(arena, &result_buf, x);
					str_buf_append(arena, &result_buf, comma_space);

					str8 y = f64tostr((f64)value.y, 6);
					str_buf_append(arena, &result_buf, y);
					str_buf_append(arena, &result_buf, comma_space);

					str8 z = f64tostr((f64)value.z, 6);
					str_buf_append(arena, &result_buf, z);
					str_buf_append(arena, &result_buf, comma_space);

					str8 w = f64tostr((f64)value.w, 6);
					str_buf_append(arena, &result_buf, w);
				}

				else if (format_id == format_table[16].t) // handle color hex
				{
					i += 3;
					color value = va_arg(args, color);
					str8 s = u32tostr(value.hex, 16);
					str_buf_append(arena, &result_buf, s);
				}
				continue;
			}
			else if (i + 4 < format.size && format.data[i + 4] == '}')
			{
				/* u32 hash = str_hash((str){.data = &format.data[i + 1], .size = 2}); */
				u16 format_id = *(u16 *)&format.data[i + 1];

				for (u32 f = 4; f < 4 + 8; ++f)
				{
					if (format_id == format_table[f].t)
					{
						i += 4;
						i32 base = 10;
						switch (format.data[i - 1])
						{
						case 'b': base = 2; break;
						case 'o': base = 8; break;
						case 'x': base = 16; break;
						}

						// f32 val = va_arg(args, f32);

						if (f < 6)
						{
							i32 value = va_arg(args, i32);
							str8 s = i32tostr(value, base);
							str_buf_append(arena, &result_buf, s);
						}
						else if (f == 6)
						{
							i64 value = va_arg(args, i64);
							str8 s = i64tostr(value, base);
							str_buf_append(arena, &result_buf, s);
						}
						else if (f < 10)
						{
							u32 value = va_arg(args, u32);
							str8 s = u32tostr(value, base);
							str_buf_append(arena, &result_buf, s);
						}
						else
						{
							u64 value = va_arg(args, u64);
							str8 s = u64tostr(value, base);
							str_buf_append(arena, &result_buf, s);
						}

						break;
					}
				}
				continue;
			}
		}

		str_buf_append_char(arena, &result_buf, format.idata[i]);
	}

	result = str_buf_end(arena, result_buf);

	return (result);
}

str8
str8_format(struct arena *arena, str8 format, ...)
{
	str8 result = {0};

	va_list args;
	va_start(args, format);

	result = sm__str_format(arena, format, args);

	va_end(args);

	return (result);
}

void
str8_printfln(struct arena *arena, str8 format, ...)
{
	str8 output;

	va_list args;
	va_start(args, format);

	output = sm__str_format(arena, format, args);

	va_end(args);

	sm__buffer_push(output);
	sm__buffer_push(str8_newline_const);

	arena_free(arena, output.data);
}

void
str8_printf(struct arena *arena, str8 format, ...)
{
	str8 output;

	va_list args;
	va_start(args, format);

	output = sm__str_format(arena, format, args);

	va_end(args);

	sm__buffer_push(output);

	arena_free(arena, output.data);
}

void
str8_vprintf(struct arena *arena, str8 format, va_list args)
{
	str8 output;

	output = sm__str_format(arena, format, args);

	sm__buffer_push(output);

	arena_free(arena, output.data);
}
