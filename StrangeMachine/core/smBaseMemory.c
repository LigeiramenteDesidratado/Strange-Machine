#include "core/smBaseMemory.h"
#include "core/smCore.h"
#include "core/smMM.h"

static struct dyn_buf BM;

b8
base_memory_init(u32 size)
{
	assert(BM.data == 0 && BM.len == 0 && BM.cap == 0);

	BM.data = mm_malloc(size);
	if (BM.data == 0) { return (false); }
	BM.len = 0;
	BM.cap = size;

	atexit(mm__print);

	return (true);
}

void
base_memory_teardown(void)
{
	mm_free(BM.data);
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
