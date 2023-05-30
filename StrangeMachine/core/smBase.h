#ifndef SM_CORE_BASE_H
#define SM_CORE_BASE_H

#include <assert.h>
#include <ctype.h> /* tolower */
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <math.h> /* mod sqrt sin cos... */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h> /* bool true false */
#include <stddef.h>  /* NULL */
#include <stdint.h>
#include <stdio.h>    /* printf */
#include <stdlib.h>   /* malloc calloc realloc free */
#include <string.h>   /* memcpy and string manipulation */
#include <sys/stat.h> /* stat */
#include <time.h>
#include <unistd.h>

#if SM_DEBUG

// #   define sx_assert(_e)
// #   define sx_assertf(_e, ...)

#	define sm__unreachable()                             \
		do {                                          \
			printf("unreachable code reached\n"); \
			abort();                              \
		} while (0)

#	define sm__unimplemented(EXIT_IF_TRUE)                 \
		do {                                            \
			printf("unimplemented code reached\n"); \
			if (EXIT_IF_TRUE) { abort(); }          \
		} while (0)

#	if defined(_MSC_VER)
#		define sm__dbgbreak() __debugbreak()
#	elif defined(__clang__)
#		if (__has_builtin(__builtin_debugtrap))
#			define sm__dbgbreak() __builtin_debugtrap()
#		else
#			define sm__dbgbreak() __builtin_trap() // this cannot be used in constexpr functions
#		endif
#	elif defined(__GNUC__)
#		define sm__dbgbreak() __builtin_trap()
#	endif

#	define sm__file_line __LINE__
#	if defined(__GNUC__) || defined(__clang__)
#		define sm__file_name (__builtin_strrchr(__FILE__, '/') + 1)
#	elif defined(_MSC_VER)
#		define sm__file_name (strrchr(__FILE__, '\\') + 1)
#	else
#		define sm__file_name __FILE__
#	endif
//
// #define sm__assert(_e) do { if (!(_e)) { sx__debug_message(__FILE__, __LINE__, #_e); sx_hwbreak(); }} while(0)
// #define sm__assertf(_e, ...) do { if (!(_e)) { sx__debug_message(__FILE__, __LINE__, __VA_ARGS__); sx_hwbreak(); }}
// while(0)

#else // SM_DEBUG

#	if defined(__GNUC__) || defined(__clang__)
#		define sm__unreachable() __builtin_unreachable();
#	elif defined(_MSC_VER)
#		define sm__unreachable() __assume(0);
#	else
while (!0)
	;
#	endif

#	define sm__dbgbreak()

#	define sm__file_line 0
#	define sm__file_name "FILE"

#	define sm__unimplemented(EXIT_IF_TRUE)

#endif // SM_DEBUG

#if defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#	define sm__static_assert _Static_assert
#else
#	define sm__static_assert
#endif

#define sm__static_type_assert(X, Y) _Generic((Y), __typeof__(X) : _Generic((X), __typeof__(Y) : (void)NULL))

#if defined(__clang__) || defined(__GNUC__)
#	define sm__maybe_unused __attribute__((unused))
#else
#	define sm__maybe_unused
#endif

#if defined(__clang__) || defined(__GNUC__)
#	define sm__force_inline static inline __attribute__((__always_inline__))
#elif defined(_MSC_VER)
#	define sm__force_inline __forceinline
#endif

// Unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Signed types
typedef char i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean type
typedef _Bool b8;

/* Ensure all types are of the correct size. */
sm__static_assert(sizeof(u8) == 1, "expected u8 to be 1 byte");
sm__static_assert(sizeof(u16) == 2, "expected u16 to be 2 bytes");
sm__static_assert(sizeof(u32) == 4, "expected u32 to be 4 bytes");
sm__static_assert(sizeof(u64) == 8, "expected u64 to be 8 bytes");
sm__static_assert(sizeof(i8) == 1, "expected i8 to be 1 byte");
sm__static_assert(sizeof(i16) == 2, "expected i16 to be 2 bytes");
sm__static_assert(sizeof(i32) == 4, "expected i32 to be 4 bytes");
sm__static_assert(sizeof(i64) == 8, "expected i64 to be 8 bytes");
sm__static_assert(sizeof(f32) == 4, "expected f32 to be 4 bytes");
sm__static_assert(sizeof(f64) == 8, "expected f64 to be 8 bytes");

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

#define B(x)  (x)
#define KB(x) ((x) << 10)
#define MB(x) ((x) << 20)
#define GB(x) ((x) << 30)

#define B2KB(X) ((X) >> 10)
#define B2MB(X) ((X) >> 20)
#define B2GB(X) ((X) >> 30)

#define B2KBf(X) (X / (f32)(1 << 10))
#define B2MBf(X) (X / (f32)(1 << 20))
#define B2GBf(X) (X / (f32)(1 << 30))

#define BIT(x)	 (1 << (x))
#define BIT64(x) (1ULL << (x))

#define ARRAY_SIZE(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

#endif // SM_CORE_BASE_H
