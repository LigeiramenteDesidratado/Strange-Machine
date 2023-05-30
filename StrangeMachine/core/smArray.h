#ifndef SM_CORE_ARRAY_H
#define SM_CORE_ARRAY_H

#include "core/smBase.h"

#define array(type) type *


#define SM_STATIC_TYPE_ASSERT(X, Y) _Generic((Y), __typeof__(X) : _Generic((X), __typeof__(Y) : (void)NULL))

#define SM__ARRAY_LEN_HEADER_SIZE (sizeof(size_t))
#define SM__ARRAY_CAP_HEADER_SIZE (sizeof(size_t))
#define SM__ARRAY_HEADER_OFFSET	  (SM__ARRAY_LEN_HEADER_SIZE + SM__ARRAY_CAP_HEADER_SIZE)

#define SM__RAW(ARRAY) ((size_t *)((char *)ARRAY - SM__ARRAY_HEADER_OFFSET))

#if 0
size_t *sm__array_ctor(size_t cap, size_t size);
size_t *sm__array_dtor(size_t *array);
size_t *sm__array_set_len(size_t *array, size_t len, size_t size);
size_t *sm__array_set_cap(size_t *array, size_t cap, size_t size);
size_t *sm__array_push(size_t *array, void *value, size_t size);
size_t *sm__array_pop(size_t *array);
size_t *sm__array_copy(size_t *dest, size_t *src, size_t size);

/* array utilities */
#define array_make(ARR, CAPACITY)                                         \
	do {                                                              \
		if (ARR) continue;                                        \
		size_t *raw = sm__array_ctor((CAPACITY), sizeof((*ARR))); \
		ARR = (typeof(ARR))&raw[2];                               \
	} while (0)

#define array_release(ARR)                                 \
	do {                                               \
		if (ARR)                                   \
		{                                          \
			size_t *raw = ((size_t *)(ARR)-2); \
			sm__array_dtor(raw);               \
			ARR = 0;                           \
		}                                          \
	} while (0)

#define array_set_len(ARR, LEN)                                                                \
	do {                                                                                   \
		if (!ARR && LEN == 0) { continue; }                                            \
		size_t *raw = (!ARR) ? sm__array_ctor((LEN), sizeof((*ARR))) : SM__RAW((ARR)); \
		raw = sm__array_set_len(raw, (LEN), sizeof((*ARR)));                           \
		(ARR) = (typeof(ARR))&raw[2];                                                  \
	} while (0)

#define array_set_cap(ARR, CAP)                                                                \
	do {                                                                                   \
		size_t *raw = (!ARR) ? sm__array_ctor((CAP), sizeof((*ARR))) : SM__RAW((ARR)); \
		raw = sm__array_set_cap(raw, (CAP), sizeof((*ARR)));                           \
		(ARR) = (typeof(ARR))&raw[2];                                                  \
	} while (0)

#define array_len(arr) ((arr) == 0 ? 0 : *((size_t *)arr - 2))
#define array_cap(arr) ((arr) == 0 ? 0 : *((size_t *)arr - 1))

#define array_push(ARR, VALUE)                                                             \
	do {                                                                               \
		size_t *raw = (!ARR) ? sm__array_ctor(1, sizeof((*ARR))) : SM__RAW((ARR)); \
		SM_STATIC_TYPE_ASSERT((*ARR), (VALUE));                                    \
		raw = sm__array_push(raw, &(VALUE), sizeof((VALUE)));                      \
		(ARR) = (typeof(ARR))&raw[2];                                              \
	} while (0)

/* WARNING: do not use array_pop inside a loop that depends array_len.
 * like:
 *  for (size_t i = 0; i < array_len(array); ++i) {
 *    // ...
 *    array_pop(array);
 *  }
 */
#define array_pop(ARR)                                           \
	do {                                                     \
		if (ARR)                                         \
		{                                                \
			size_t *raw = SM__RAW(ARR);              \
			raw[0] = (raw[0] == 0) ? 0 : raw[0] - 1; \
		}                                                \
	} while (0)

#define array_del(arr, i, n)                                                                                  \
	do {                                                                                                  \
		assert((i) >= 0 && "negative index");                                                         \
		assert((n) >= -1);                                                                            \
		size_t *raw = SM__RAW(arr);                                                                   \
		assert((i) < raw[0] && "index out of range");                                                 \
		if ((n) == 0) { continue; }                                                                   \
		if (((n) == -1))                                                                              \
		{                                                                                             \
			raw[0] = i;                                                                           \
			continue;                                                                             \
		}                                                                                             \
		u32 ___nnn = ((i) + (n) >= raw[0]) ? raw[0] - (i) : (n);                                      \
		memmove(&(arr)[(i)], &(arr)[(i) + (___nnn)], sizeof((*arr)) * ((raw)[0] - ((i) + (___nnn)))); \
		raw[0] = raw[0] - ___nnn;                                                                     \
	} while (0)

#define array_copy(dest, src)                                                                               \
	do {                                                                                                \
		assert((src) != 0);                                                                         \
		SM_STATIC_TYPE_ASSERT((*src), (*dest));                                                     \
		size_t *raw_src = SM__RAW(src);                                                             \
		size_t *raw_dest = (!dest) ? sm__array_ctor(raw_src[0], sizeof((*dest))) : SM__RAW((dest)); \
		raw_dest = sm__array_copy(raw_dest, raw_src, sizeof((*src)));                               \
		(dest) = (typeof(dest))&raw_dest[2];                                                        \
	} while (0)

/* ALIGNED ARRAY */

/* TODO: implement aligned array properly */
#define aligned_array_size(arr) ((arr) == NULL ? 0 : *((size_t *)arr - 2))

#define SM_ALIGNED_ARRAY_NEW(arr, alignment, size)                                                   \
	do {                                                                                         \
		SM_ASSERT(size > 0 && "negative size or non zero");                                  \
		SM_ASSERT(arr == NULL && "already allocated");                                       \
		size_t *raw = SM_ALIGNED_ALLOC(alignment, SM_HEADER_OFFSET + size * sizeof((*arr))); \
		if (raw == NULL) err(EXIT_FAILURE, NULL);                                            \
		raw[0] = size;                                                                       \
		raw[1] = size;                                                                       \
		(arr) = (void *)&raw[2];                                                             \
	} while (0)

#define aligned_array_dtor(arr)                            \
	do {                                               \
		if ((arr))                                 \
		{                                          \
			size_t *raw = ((size_t *)(arr)-2); \
			SM_ALIGNED_FREE((raw));            \
			(arr) = NULL;                      \
		}                                          \
	} while (0)

#endif

#endif /* SM_CORE_DATA_ARRAY_H */
