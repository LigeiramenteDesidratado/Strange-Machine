#include "core/smArena.h"
#include "core/smBase.h"
#include "core/smLog.h"
#include "core/smString.h"

#include "vendor/physfs/src/physfs.h"

struct fs
{
	struct arena *arena;
};

static struct fs FC;

static void
sm__physfs_log_last_error(str8 message)
{
	PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
	const char *err_str = PHYSFS_getErrorByCode(err);

	log_error(str8_from("{s} PHYSFS_Error: {s}"), message, (str8){.cidata = err_str, .size = strlen(err_str)});
}

static void *
sm__physfs_malloc(u64 size)
{
	void *result;

	result = arena_reserve(FC.arena, size);

	return (result);
}

static void *
sm__physfs_realloc(void *ptr, u64 size)
{
	void *result;

	result = arena_resize(FC.arena, ptr, size);

	return (result);
}

static void
sm__physfs_free(void *ptr)
{
	arena_free(FC.arena, ptr);
}

fs_init(struct arena *arena, str8 mount_path, i8 *argv[])
{
	PHYSFS_Allocator allocator = {
	    .Malloc = (void *(*)(unsigned long long))sm__physfs_malloc,
	    .Realloc = (void *(*)(void *, unsigned long long))sm__physfs_realloc,
	    .Free = sm__physfs_free,
	};
	PHYSFS_setAllocator(&allocator);

	if (!PHYSFS_init(argv[0]))
	{
		sm__physfs_log_last_error(str8_from("error while initializing."));
		return (false);
	}

	if (!PHYSFS_mount(mount_path.idata, "/", 1))
	{
		sm__physfs_log_last_error(str8_from("error while mounting"));
		return (false);
	}

	if (!PHYSFS_setWriteDir(mount_path.idata))
	{
		sm__physfs_log_last_error(str8_from("error while setting write dir"));
		return (false);
	}
}
