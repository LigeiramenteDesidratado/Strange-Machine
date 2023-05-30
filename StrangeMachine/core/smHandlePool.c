#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smHandlePool.h"
#include "core/smMM.h"

#define ALIGN_MASK(VALUE, MASK) (((VALUE) + (MASK)) & ((~0u) & (~(MASK))))

void
handle_pool_make(struct arena *arena, struct handle_pool *handle_pool, u32 capacity)
{
	handle_pool->len = 0;
	handle_pool->cap = capacity;

	handle_pool->dense = arena_reserve(arena, capacity * sizeof(handle_t));
	handle_pool->sparse = arena_reserve(arena, capacity * sizeof(u32));

	handle_pool_reset(handle_pool);
}

void
handle_pool_release(struct arena *arena, struct handle_pool *handle_pool)
{
	arena_free(arena, handle_pool->dense);
	arena_free(arena, handle_pool->sparse);

	memset(handle_pool, 0x0, sizeof(struct handle_pool));
}

void
handle_pool_reset(struct handle_pool *pool)
{
	pool->len = 0;
	for (u32 i = 0, c = pool->cap; i < c; i++) { pool->dense[i] = sm__handle_make(0, i); }
}

void
handle_pool_grow(struct arena *arena, struct handle_pool *handle_pool, u32 new_capacity)
{
	handle_pool->dense = arena_resize(arena, handle_pool->dense, new_capacity * sizeof(handle_t));
	handle_pool->sparse = arena_resize(arena, handle_pool->sparse, new_capacity * sizeof(u32));

	u32 size = new_capacity - handle_pool->cap;
	for (u32 i = 0; i < size; ++i)
	{
		handle_pool->dense[handle_pool->cap + i] = sm__handle_make(0, handle_pool->cap + i);
	}

	handle_pool->cap = new_capacity;
}

void
handle_pool_copy(struct handle_pool *dest, struct handle_pool *src)
{
	if (dest == src) { return; }
	assert(dest->cap == src->cap);

	dest->len = src->len;

	memcpy(dest->dense, src->dense, sizeof(handle_t) * src->len);
	memcpy(dest->sparse, src->sparse, sizeof(u32) * src->len);
}

handle_t
handle_new(struct arena *arena, struct handle_pool *handle_pool)
{
	handle_t result = INVALID_HANDLE;

	if (handle_pool->len >= handle_pool->cap)
	{
		handle_pool_grow(arena, handle_pool, handle_pool->cap << 1);
	}

	u32 index = handle_pool->len++;
	handle_t handle = handle_pool->dense[index];

	// increase generation
	u32 gen = sm__handle_gen(handle);
	u32 _index = handle_index(handle);
	handle_t new_handle = sm__handle_make(++gen, _index);

	handle_pool->dense[index] = new_handle;
	handle_pool->sparse[_index] = index;
	result = new_handle;

	return (result);
}

void
handle_remove(struct handle_pool *pool, handle_t handle)
{
	assert(pool->len > 0);
	assert(handle_valid(pool, handle));

	u32 index = pool->sparse[handle_index(handle)];
	handle_t last_handle = pool->dense[--pool->len];

	pool->dense[pool->len] = handle;
	pool->sparse[handle_index(last_handle)] = index;
	pool->dense[index] = last_handle;
}

b8
handle_valid(const struct handle_pool *pool, handle_t handle)
{
	assert(handle);

	b8 result;

	u32 index = pool->sparse[handle_index(handle)];
	result = index < pool->len && pool->dense[index] == handle;

	return (result);
}

handle_t
handle_at(const struct handle_pool *pool, u32 index)
{
	assert(index < pool->len);

	handle_t result;

	result = pool->dense[index];
	return (result);
}

b8
handle_full(const struct handle_pool *pool)
{
	b8 result;

	result = pool->len == pool->cap;

	return (result);
}
