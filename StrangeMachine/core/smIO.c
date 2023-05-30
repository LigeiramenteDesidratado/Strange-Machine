#include "core/smBase.h"

#include "core/smLog.h"
#include "core/smString.h"
#include "physfs.h"

static void
sm__io_log_last_error(str8 message)
{
	PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
	const char *err_str = PHYSFS_getErrorByCode(err);

	log_error(str8_from("{s} PHYSFS_Error: {s}"), message, (str8){.cidata = err_str, .size = strlen(err_str)});
}

b8
io_init(i8 *argv[], str8 assets_folder)
{
	if (!PHYSFS_init(argv[0]))
	{
		sm__io_log_last_error(str8_from("error while initializing."));
		return (false);
	}

	if (!PHYSFS_mount(assets_folder.idata, "/", 1))
	{
		sm__io_log_last_error(str8_from("error while mounting"));
		return (false);
	}

	if (!PHYSFS_setWriteDir(assets_folder.idata))
	{
		sm__io_log_last_error(str8_from("error while mounting"));
		return (false);
	}

	return (true);
}

void
io_teardown(void)
{
	PHYSFS_deinit();
}
