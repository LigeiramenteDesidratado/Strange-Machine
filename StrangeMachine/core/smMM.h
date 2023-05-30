#ifndef SM_CORE_MM_H
#define SM_CORE_MM_H

#include "core/smBase.h"

void *mm_malloc(size_t size);
void *mm_calloc(size_t nmemb, size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);

/* alinged memory allocation */
void *mm_aligned_alloc(size_t alignment, size_t size);
void *mm_aligned_realloc(void *ptr, size_t alignment, size_t size);
void mm_aligned_free(void *ptr);

void mm__print(void);

#endif // SM_CORE_MM_H
