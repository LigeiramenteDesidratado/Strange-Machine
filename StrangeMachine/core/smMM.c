#include "core/smBase.h"

#include "core/smMM.h"

static struct
{
	u64 total_allocs;
	u64 allocs;
	u64 frees;
	size_t total_bytes;
	size_t bytes;

} mem_info = {0};

#define MM_HEADER_SIZE sizeof(size_t)

void *mm_malloc(size_t size);
void *mm_calloc(size_t nmemb, size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);

/* Alinged memory allocation */
void *mm_aligned_alloc(size_t alignment, size_t size);
void *mm_aligned_realloc(void *ptr, size_t alignment, size_t size);
void mm_aligned_free(void *ptr);

void *
mm_malloc(size_t size)
{
	/*
	 * ISO/IEC 9899:TC3 section 6.5.3.4 The sizeof operator
	 * item 3: When applied to an operand that has type char, unsigned char, or signed char,
	 * (or a qualified version thereof) the result is 1...
	 * */
	u8 *ptr = 0;
	ptr = malloc(size + MM_HEADER_SIZE);
	if (ptr == 0) { return (0); }

	*(size_t *)ptr = size; /* save the size at the begining of the array */

	mem_info.total_bytes += size;
	mem_info.bytes += size;
	mem_info.total_allocs++;
	mem_info.allocs++;

	/*
	 * -------------------
	 * |size|0|1|2|3|...|
	 * -----^------------
	 *      |
	 *      address returned
	 * */
	return (ptr + MM_HEADER_SIZE);
}

void *
mm_calloc(size_t nmemb, size_t size)
{
	size_t _size = nmemb * size;

	u8 *ptr = 0;
	ptr = mm_malloc(_size);
	if (ptr == 0) { return (0); }
	memset(ptr, 0x0, _size);

	return ptr;
}

void *
mm_realloc(void *ptr, size_t size)
{
	if (!ptr) { return (mm_malloc(size)); }

	u8 *newp = 0;
	u8 *_ptr = (((u8 *)ptr) - MM_HEADER_SIZE);

	if ((newp = realloc(_ptr, size + MM_HEADER_SIZE)) == 0) { return (0); }

	_ptr = newp;
	size_t old_size = *(size_t *)_ptr;
	mem_info.bytes -= old_size;
	*(size_t *)_ptr = size;

	mem_info.total_bytes += size - old_size;
	mem_info.bytes += size;
	mem_info.total_allocs++;

	return (_ptr + MM_HEADER_SIZE);
}

void
mm_free(void *ptr)
{
	if (ptr)
	{
		u8 *_ptr = ((u8 *)ptr) - MM_HEADER_SIZE;
		size_t size = *((size_t *)_ptr);

		mem_info.bytes -= size;
		mem_info.allocs--;
		mem_info.frees++;

		free(_ptr);
	}
}

/* Alinged memory allocation */
void *
mm_aligned_alloc(size_t alignment, size_t size)
{
	void *ptr = 0;

	// check if the alignment request is a power of two
	assert((alignment & (alignment - 1)) == 0);

	// ensure align and size are non-zero values before we try to allocate any memory
	if (!alignment || !size) { return (0); }

	size_t _size = size + alignment - 1;

	u8 *raw = (void *)malloc(_size + MM_HEADER_SIZE);
	if (!raw) { return (0); }

	// store the size at the beginning of the array
	*(size_t *)raw = size;

	u8 *header_address = (raw + MM_HEADER_SIZE); // not the actual address of the header

	u8 *aligned_address = (u8 *)(((uintptr_t)header_address + (alignment - 1)) & ~(uintptr_t)(alignment - 1));

	u8 offset = (u8)(aligned_address - header_address);

	// store the offset 1 byte before the aligned address
	*((u8 *)aligned_address - 1) = offset;

	ptr = aligned_address;

	mem_info.total_bytes += size;
	mem_info.bytes += size;
	mem_info.total_allocs++;
	mem_info.allocs++;

	return (ptr);
}

void *
mm_aligned_realloc(void *ptr, size_t alignment, size_t size)
{
	if (!ptr) { return (mm_aligned_alloc(alignment, size)); }

	u8 *new_ptr = mm_aligned_alloc(alignment, size);

	// get the offset by reading the byte before the aligned address
	u8 offset = *((u8 *)ptr - 1);

	// get the original address by subtracting the offset plus the header size from the aligned address
	u8 *_ptr = ((u8 *)ptr) - (offset + MM_HEADER_SIZE);

	size_t old_size = *((size_t *)_ptr);

	size_t copy_size = old_size < size ? old_size : size; // get min

	memcpy(new_ptr, ptr, copy_size);

	mm_aligned_free(ptr);

	return (new_ptr);
}

void
mm_aligned_free(void *ptr)
{
	if (ptr)
	{
		/* get the offset by reading the byte before the aligned address */
		u8 offset = *((u8 *)ptr - 1);

		/* get the original address by subtracting the offset plus the header size from the aligned address */
		u8 *_ptr = ((u8 *)ptr) - (offset + MM_HEADER_SIZE);

		size_t size = *((size_t *)_ptr);

		mem_info.bytes -= size;
		mem_info.allocs--;
		mem_info.frees++;

		free(_ptr);
	}
}

static i8 *mm__human_readable_size(void);

void
mm__print(void)
{
	printf("dangling: %lu\n", mem_info.allocs);
	printf("dangling bytes: %lu %s\n", mem_info.bytes, (mem_info.bytes == 0) ? "" : "(Waring: memory leaks!)");
	printf("total (re)alloctions: %lu\n", mem_info.total_allocs);
	printf("total bytes: %s\n", mm__human_readable_size());
	printf("free calls: %lu\n", mem_info.frees);
}

static i8 *
mm__human_readable_size(void)
{
	static i8 buf[32] = {0};
	double size = (double)mem_info.total_bytes;
	if (size < KB(1)) sprintf(buf, "%.2f B", size);
	else if (size < GB(1)) sprintf(buf, "%.2f MB", (f64)B2MBf((f32)size));
	else if (size < MB(1)) sprintf(buf, "%.2f KB", (f64)B2KBf((f32)size));
	else sprintf(buf, "%.2f GB", (f64)B2GBf((f32)size));

	return buf;
}
