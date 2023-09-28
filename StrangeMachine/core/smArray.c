#include "core/smArray.h"
#include "core/smCore.h"
#include "core/smMM.h"

void *
sm__array_make2(struct arena *arena, u32 cap, u32 item_size, str8 file, u32 line)
{
	struct sm__array_header *result;

	result = sm__arena_aligned(arena, 16, sm__array_header_size + (cap * item_size), file, line);

	result->len = 0;
	result->cap = cap;

	return (result);
}

void
sm__array_release2(struct arena *arena, void *ptr, str8 file, u32 line)
{
	sm__arena_free(arena, ptr, file, line);
}

void *
sm__array_set_len2(struct arena *arena, void *ptr, u32 len, u32 size, str8 file, u32 line)
{
	struct sm__array_header *result = (struct sm__array_header *)ptr;

	if (len > result->cap)
	{
		result = sm__arena_realloc(arena, ptr, sm__array_header_size + (len * size), file, line);
		result->cap = len;
	}

	result->len = len;

	return (result);
}

void *
sm__array_set_cap2(struct arena *arena, void *ptr, u32 cap, u32 size, str8 file, u32 line)
{
	struct sm__array_header *result = (struct sm__array_header *)ptr;

	if (cap > result->cap)
	{
		result = sm__arena_realloc(arena, ptr, sm__array_header_size + (cap * size), file, line);
	}

	result->cap = cap;
	result->len = MIN(result->len, result->cap);

	return (result);
}

void *
sm__array_push2(struct arena *arena, void *ptr, void *value, size_t size, str8 file, u32 line)
{
	struct sm__array_header *result = (struct sm__array_header *)ptr;

	if (++result->len > result->cap)
	{
		result = sm__array_set_cap2(arena, result, result->cap * 2, (u32)size, file, line);
	}

	memcpy((u8 *)result + sm__array_header_size + (size * (result->len - 1)), value, size);

	return (result);
}

void *
sm__array_pop2(void *ptr)
{
	struct sm__array_header *result = (struct sm__array_header *)ptr;

	result->len = result->len == 0 ? 0 : result->len - 1;

	return (result);
}

void *
sm__array_copy2(struct arena *arena, void *dest_ptr, const void *src_ptr, size_t item_size, str8 file, u32 line)
{
	struct sm__array_header *result = (struct sm__array_header *)dest_ptr;
	const struct sm__array_header *src = (struct sm__array_header *)src_ptr;

	result = sm__array_set_cap2(arena, result, src->cap, (u32)item_size, file, line);
	result = sm__array_set_len2(arena, result, src->len, (u32)item_size, file, line);

	memcpy((u8 *)result + sm__array_header_size, (u8 *)src + sm__array_header_size, item_size * src->len);

	return (result);
}

#if 0

#	define SM__ARRAY_LEN(RAW) RAW[0]
#	define SM__ARRAY_CAP(RAW) RAW[1]

size_t *
sm__array_ctor(size_t cap, size_t size)
{
	size_t *array = mm_aligned_alloc(16, SM__ARRAY_HEADER_OFFSET + (size * cap));
	assert(array);

	SM__ARRAY_LEN(array) = 0;
	SM__ARRAY_CAP(array) = cap;

	return (array);
}

size_t *
sm__array_dtor(size_t *array)
{
	mm_aligned_free(array);

	return (0);
}

size_t *
sm__array_set_len(size_t *array, size_t len, size_t size)
{
	if (len > SM__ARRAY_CAP(array))
	{
		void *tmp = NULL;
		tmp = mm_aligned_realloc(array, 16, SM__ARRAY_HEADER_OFFSET + (len * size));
		assert(tmp);

		array = (size_t *)tmp;
		SM__ARRAY_CAP(array) = len;
	}

	SM__ARRAY_LEN(array) = len;

	return (array);
}

size_t *
sm__array_set_cap(size_t *array, size_t cap, size_t size)
{
	if (cap > SM__ARRAY_CAP(array))
	{
		void *tmp = 0;
		tmp = mm_aligned_realloc(array, 16, SM__ARRAY_HEADER_OFFSET + (cap * size));
		assert(tmp);

		array = (size_t *)tmp;
	}

	SM__ARRAY_CAP(array) = cap;
	SM__ARRAY_LEN(array) =
	    (SM__ARRAY_LEN(array) > SM__ARRAY_CAP(array)) ? SM__ARRAY_CAP(array) : SM__ARRAY_LEN(array);

	return (array);
}

size_t *
sm__array_push(size_t *array, void *value, size_t size)
{
	if (++SM__ARRAY_LEN(array) > SM__ARRAY_CAP(array))
	{
		array = sm__array_set_cap(array, SM__ARRAY_CAP(array) * 2, size);
	}

	memcpy((u8 *)array + SM__ARRAY_HEADER_OFFSET + (size * (SM__ARRAY_LEN(array) - 1)), value, size);

	return (array);
}

size_t *
sm__array_pop(size_t *array)
{
	SM__ARRAY_LEN(array) = (SM__ARRAY_LEN(array) == 0) ? 0 : (SM__ARRAY_LEN(array) - 1);

	return (array);
}

size_t *
sm__array_copy(size_t *dest, size_t *src, size_t size)
{
	dest = sm__array_set_cap(dest, SM__ARRAY_CAP(src), size);
	dest = sm__array_set_len(dest, SM__ARRAY_LEN(src), size);

	memcpy((u8 *)dest + SM__ARRAY_HEADER_OFFSET, (u8 *)src + SM__ARRAY_HEADER_OFFSET, size * SM__ARRAY_LEN(src));
	return (dest);
}
#endif
