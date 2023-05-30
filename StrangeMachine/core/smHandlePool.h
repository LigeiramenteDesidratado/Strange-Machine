#ifndef SM_CORE_HANDLE_POOL_H
#define SM_CORE_HANDLE_POOL_H

#include "core/smBase.h"
#include "core/smCore.h"

typedef u32 handle_t;

struct handle_pool
{
	u32 len;
	u32 cap;
	struct arena* arena;
	array(handle_t) dense; // [0..count] saves actual handles
	array(u32) sparse;     // [0..capacity] saves indexes-to-dense for removal lookup
};

#define CONFIG_HANDLE_GEN_BITS 14
#define INVALID_HANDLE	       0u

static const u32 sm__handle_index_mask = (1 << (32 - CONFIG_HANDLE_GEN_BITS)) - 1;
static const u32 sm__handle_gen_mask = ((1 << CONFIG_HANDLE_GEN_BITS) - 1);
static const u32 sm__handle_gen_shift = (32 - CONFIG_HANDLE_GEN_BITS);

#define handle_index(HANDLE)   (u32)((HANDLE)&sm__handle_index_mask)
#define sm__handle_gen(HANDLE) (u32)(((HANDLE) >> sm__handle_gen_shift) & sm__handle_gen_mask)
#define sm__handle_make(GEN, INDEX) \
	(u32)((((u32)(GEN)&sm__handle_gen_mask) << sm__handle_gen_shift) | ((u32)(INDEX)&sm__handle_index_mask))

void handle_pool_make(struct arena *arena, struct handle_pool *handle_pool, u32 capacity);
void handle_pool_release(struct arena *arena, struct handle_pool *handle_pool);
void handle_pool_reset(struct handle_pool *pool);
void handle_pool_copy(struct handle_pool *dest, struct handle_pool *src);

handle_t handle_new(struct arena *arena, struct handle_pool *handle_pool);
void handle_remove(struct handle_pool *pool, handle_t handle);
b8 handle_valid(const struct handle_pool *pool, handle_t handle);
handle_t handle_at(const struct handle_pool *pool, u32 index); // converts pool index to a valid handle
b8 handle_full(const struct handle_pool *pool);

#endif // SM_HANDLE_POOL_H
