#include "core/smBaseMemory.h"
#include "core/smCore.h"
#include "core/smMM.h"

static struct dyn_buf BM;

#include <sys/mman.h>

b8
base_memory_init(u32 size)
{
	assert(BM.data == 0 && BM.len == 0 && BM.cap == 0);

#ifdef USE_SM_MALLOC
	BM.data = mm_malloc(size);
#else
	const i32 prot = PROT_READ | PROT_WRITE;
	const i32 flags = MAP_PRIVATE | MAP_ANONYMOUS;

	BM.data = mmap(NULL, size, prot, flags, -1, 0);
#endif
	if (BM.data == 0) { return (false); }
	BM.len = 0;
	BM.cap = size;

	return (true);
}

void
base_memory_teardown(void)
{
#ifdef USE_SM_MALLOC
	mm_free(BM.data);
#else
	munmap(BM.data, BM.len);
#endif
	BM.len = 0;
	BM.cap = 0;
}

struct buf
base_memory_reserve(u32 size)
{
	struct buf result = {0};

	if (BM.len + size <= BM.cap)
	{
		result.data = BM.data + BM.len;
		result.size = size;

		BM.len += size;
	}
	else
	{
		printf("base memory overflow: %d\n", size + BM.len);
		exit(1);
	}

	return (result);
}

struct buf
base_memory_begin(void)
{
	struct buf result;

	result.data = BM.data + BM.len;
	result.size = BM.cap - BM.len;

	return (result);
}

void
base_memory_end(u32 size)
{
	if (BM.len + size <= BM.cap) { BM.len += size; }
	else
	{
		printf("base memory overflow: %d\n", size + BM.len);
		exit(1);
	}
}

void
base_memory_reset(void)
{
	BM.len = 0;
}
