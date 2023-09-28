#include "core/smLog.h"
#include "smBase.h"

#include "core/smArena.h"
#include "core/smCore.h"

#include "vendor/tlsf/tlsf.h"

static void
sm__arena_walker(void *ptr, size_t size, i32 used, sm__maybe_unused void *user)
{
	static u32 total_unused = 0;
	static u32 total_used = 0;
	if (used) { total_used += (u32)size; }
	else { total_unused += (u32)size; }
	printf("ptr: %p, size: %lu, used: %d\n", ptr, size, used);
	printf("unused: %d\n", total_unused);
	printf("used: %d\n", total_used);
}

void
sm__arena_make(struct arena *alloc, struct buf base_memory, str8 file, u32 line)
{
	assert(base_memory.size >= tlsf_size());
	const u32 _size = base_memory.size - (u32)tlsf_size();
	i8 *buf = (i8 *)base_memory.data;

	void *tlsf_mem = buf;
	tlsf_t tlsf = tlsf_create(tlsf_mem);
	if (tlsf == 0)
	{
		log__log(LOG_ERRO, file, line, str8_from("error while creating TLSF."));
		exit(1);
	}

	void *allocator_mem = buf + tlsf_size();
	pool_t tlsf_pool = tlsf_add_pool(tlsf, allocator_mem, _size);
	if (tlsf_pool == 0)
	{
		log__log(LOG_ERRO, file, line, str8_from("error while creating TLSF pool."));
		exit(1);
	}

	alloc->base_memory = base_memory;
	alloc->tlsf = tlsf;
	alloc->tlsf_pool = tlsf_pool;
	alloc->mem = allocator_mem;
}

void
sm__arena_release(struct arena *alloc, sm__maybe_unused str8 file, sm__maybe_unused u32 line)
{
	// tlsf_remove_pool(alloc->tlsf, alloc->tlsf_pool);

	// free(alloc->_tlsf_mem);
	// free(alloc->mem);

	*alloc = (struct arena){0};
}

void *
sm__arena_malloc(struct arena *alloc, u32 size, str8 file, u32 line)
{
	void *result = 0;
	result = tlsf_malloc(alloc->tlsf, size);
	if (result == 0)
	{
		tlsf_walk_pool(alloc->tlsf_pool, sm__arena_walker, 0);
		log__log(LOG_ERRO, file, line,
		    str8_from("OOM: error while allocating memory. Consider increasing the arena size"));
		exit(1);
	}

	return (result);
}

void *
sm__arena_realloc(struct arena *alloc, void *ptr, u32 size, str8 file, u32 line)
{
	void *result = 0;

	result = tlsf_realloc(alloc->tlsf, ptr, size);
	if (result == 0)
	{
		tlsf_walk_pool(alloc->tlsf_pool, sm__arena_walker, 0);
		log__log(LOG_ERRO, file, line,
		    str8_from("OOM: error while reallocating memory. Consider increasing the arena size"));
		exit(1);
	}

	return (result);
}

void *
sm__arena_aligned(struct arena *alloc, u32 align, u32 size, str8 file, u32 line)
{
	void *result = 0;
	result = tlsf_memalign(alloc->tlsf, align, size);
	if (result == 0)
	{
		log__log(LOG_ERRO, file, line,
		    str8_from("OOM: error while allocating aligned memory. Consider increasing the arena size"));
		exit(1);
	}

	return (result);
}

void
sm__arena_free(struct arena *alloc, void *ptr, sm__maybe_unused str8 file, sm__maybe_unused u32 line)
{
	tlsf_free(alloc->tlsf, ptr);
}

void
sm__arena_validate(struct arena *arena, sm__maybe_unused str8 file, sm__maybe_unused u32 line)
{
	assert(tlsf_check(arena->tlsf) == 0);
	assert(tlsf_check_pool(arena->tlsf_pool) == 0);
}

u32
sm__arena_get_overhead_size(void)
{
	u32 result;

	result = (u32)tlsf_size();

	return (result);
}
