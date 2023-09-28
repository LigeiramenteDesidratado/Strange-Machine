#include "audio/smAudio.h"
#include "core/smCore.h"
#include "core/smLog.h"
#include "core/smResource.h"

#include "vendor/miniaudio/miniaudio.h"

#define GEN_NAME		       str8_audio
#define GEN_KEY_TYPE		       str8
#define GEN_VALUE_TYPE		       ma_sound *
#define GEN_HASH_KEY_FN(_key)	       str8_hash(_key)
#define GEN_CMP_KEY_FN(_key_a, _key_b) str8_eq(_key_a, _key_b)
#include "core/smHashMap.inl"

struct audio_manager
{
	struct arena arena;

	struct
	{
		ma_resource_manager_config config;
		ma_resource_manager manager;
	} resource;

	struct
	{
		ma_engine_config config;
		ma_engine engine;

	} engine;

	array(ma_sound) audios;
	struct str8_audio_map map;
};

static struct audio_manager AC; // Audio Context

#define MA_MALLOC(sz)	     arena_reserve(&AC.arena, sz)
#define MA_REALLOC(p, newsz) arena_resize(&AC.arena, p, newsz)
#define MA_FREE(p)	     arena_free(&AC.arena, p)

#define DRWAV_MALLOC(sz)	arena_reserve(&AC.arena, sz)
#define DRWAV_REALLOC(p, newsz) arena_resize(&AC.arena, p, newsz)
#define DRWAV_FREE(p)		arena_free(&AC.arena, p)

#define DRFLAC_MALLOC(sz)	 arena_reserve(&AC.arena, sz)
#define DRFLAC_REALLOC(p, newsz) arena_resize(&AC.arena, p, newsz)
#define DRFLAC_FREE(p)		 arena_free(&AC.arena, p)

#define DRMP3_MALLOC(sz)	arena_reserve(&AC.arena, sz)
#define DRMP3_REALLOC(p, newsz) arena_resize(&AC.arena, p, newsz)
#define DRMP3_FREE(p)		arena_free(&AC.arena, p)

#define MINIAUDIO_IMPLEMENTATION
#include "vendor/miniaudio/miniaudio.h"

static void
sm__physfs_log_last_error(str8 message)
{
	const i8 *err_str = fs_file_last_error();

	log_error(str8_from("{s} PHYSFS_Error: {s}"), message, (str8){.cidata = err_str, .size = (u32)strlen(err_str)});
}

static void *
sm__ma_malloc(size_t size, sm__maybe_unused void *user_data)
{
	void *result;

	result = arena_reserve(&AC.arena, size);
	return (result);
}

static void *
sm__ma_realloc(void *ptr, size_t size, sm__maybe_unused void *user_data)
{
	void *result;

	result = arena_resize(&AC.arena, ptr, size);
	return (result);
}

static void
sm__ma_free(void *ptr, sm__maybe_unused void *user_data)
{
	arena_free(&AC.arena, ptr);
}

static void
sm__ma_allocation_callbacks_init(ma_allocation_callbacks *allocation_callback)
{
	allocation_callback->pUserData = 0;
	allocation_callback->onMalloc = sm__ma_malloc;
	allocation_callback->onRealloc = sm__ma_realloc;
	allocation_callback->onFree = sm__ma_free;
}

static ma_result
sm__physfs_vfs_open(sm__maybe_unused ma_vfs *vfs, const i8 *file_path, ma_uint32 open_mode, ma_vfs_file *file)
{
	if (file == 0) { return (MA_INVALID_ARGS); }

	*file = 0;

	if (file_path == 0 || open_mode == 0) { return (MA_INVALID_ARGS); }

	struct fs_file *f = arena_reserve(&AC.arena, sizeof(struct fs_file));

	if ((open_mode & MA_OPEN_MODE_READ) != 0)
	{
		assert((open_mode & MA_OPEN_MODE_WRITE) == 0);
		*f = fs_file_open_read_cstr(file_path);
	}
	else { *f = fs_file_open_write_cstr(file_path); }

	if (!f->ok) { return (MA_ERROR); }

	*file = f;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_close(sm__maybe_unused ma_vfs *pVFS, ma_vfs_file file)
{
	assert(file != 0);

	struct fs_file *f = (struct fs_file *)file;

	fs_file_close(f);
	arena_free(&AC.arena, f);

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_read(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, void *dest, size_t size, size_t *bytes_read)
{
	size_t result;

	assert(file != 0);
	assert(dest != 0);

	struct fs_file *f = (struct fs_file *)file;
	result = fs_file_read(f, dest, size);
	if (bytes_read != 0) { *bytes_read = result; }

	if (result != size)
	{
		if (result == 0 && fs_file_eof(f)) { return (MA_AT_END); }
		else
		{
			sm__physfs_log_last_error(str8_from("error while reading bytes"));
			return (MA_ERROR);
		}
	}

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_write(
    sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, const void *src, size_t size, size_t *bytes_written)
{
	size_t result;

	assert(file != 0);
	assert(src != 0);

	struct fs_file *f = (struct fs_file *)file;
	result = fs_file_write(f, src, size);

	if (bytes_written != 0) { *bytes_written = result; }

	// TODO: handle error
	assert(result == size);

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_seek(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
	assert(file != NULL);

	struct fs_file *f = (struct fs_file *)file;

	i64 size = f->status.filesize;
	assert(size != -1);

	i64 position = 0;

	switch (origin)
	{
	case ma_seek_origin_start: position = offset; break;
	case ma_seek_origin_end: position = size + offset; break;
	case ma_seek_origin_current:
		{
			i64 current = fs_file_tell(f);
			if (current == -1)
			{
				sm__physfs_log_last_error(str8_from("error while telling."));
				return (MA_ERROR);
			}

			position = current + offset;
		}
		break;
	}

	assert(position >= 0);
	assert(position <= size); // consider EOF

	i32 err = fs_file_seek(f, (u64)position);
	if (err == 0)
	{
		sm__physfs_log_last_error(str8_from("error while seeking."));
		return (MA_ERROR);
	}

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_tell(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_int64 *cursor)
{
	assert(file != 0);
	assert(cursor != 0);

	struct fs_file *f = file;

	i64 result = fs_file_tell(f);
	if (result == -1)
	{
		*cursor = 0;
		sm__physfs_log_last_error(str8_from("error while telling."));
		return (MA_ERROR);
	}

	*cursor = result;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_info(sm__maybe_unused ma_vfs *vfs, ma_vfs_file file, ma_file_info *file_info)
{
	assert(file != 0);
	assert(file_info != 0);

	struct fs_file *f = file;

	assert(f->status.filesize != -1);
	file_info->sizeInBytes = f->status.filesize;

	return (MA_SUCCESS);
}

static ma_result
sm__physfs_vfs_openw(sm__maybe_unused ma_vfs *vfs, sm__maybe_unused const wchar_t *file_path,
    sm__maybe_unused ma_uint32 openMode, sm__maybe_unused ma_vfs_file *file)
{
	assert(0 && "openw");
}

static ma_result
sm__ma_filesystem_callbacks_init(ma_default_vfs *vfs)
{
	if (vfs == 0) { return (MA_INVALID_ARGS); }

	vfs->cb.onOpen = sm__physfs_vfs_open;
	vfs->cb.onOpenW = sm__physfs_vfs_openw;
	vfs->cb.onClose = sm__physfs_vfs_close;
	vfs->cb.onRead = sm__physfs_vfs_read;
	vfs->cb.onWrite = sm__physfs_vfs_write;
	vfs->cb.onSeek = sm__physfs_vfs_seek;
	vfs->cb.onTell = sm__physfs_vfs_tell;
	vfs->cb.onInfo = sm__physfs_vfs_info;
	sm__ma_allocation_callbacks_init(&vfs->allocationCallbacks);

	return (MA_SUCCESS);
}

static void
sm__audio_log(sm__maybe_unused void *user_data, u32 level, const i8 *message)
{
	str8 m = str8_from_cstr_stack((i8 *)message);
	if (m.size == 0) { return; }
	m.size--;

	switch (level)
	{
	case MA_LOG_LEVEL_DEBUG: log_debug(m); break;
	case MA_LOG_LEVEL_INFO: log_info(m); break;
	case MA_LOG_LEVEL_WARNING: log_warn(m); break;
	case MA_LOG_LEVEL_ERROR: log_error(m); break;
	}
}

b8
audio_manager_init(void)
{
	struct buf m_resource = base_memory_reserve(MB(3));

	arena_make(&AC.arena, m_resource);
	arena_validate(&AC.arena);

	// Allocation init
	ma_allocation_callbacks allocation_callback;
	sm__ma_allocation_callbacks_init(&allocation_callback);

	// Virtual filesystem init
	ma_default_vfs *vfs = arena_reserve(&AC.arena, sizeof(ma_default_vfs));
	sm__ma_filesystem_callbacks_init(vfs);

	// Log init
	ma_log *log = arena_reserve(&AC.arena, sizeof(ma_log));
	ma_log_init(&allocation_callback, log);
	ma_log_callback log_callback = ma_log_callback_init(sm__audio_log, 0);
	ma_log_register_callback(log, log_callback);

	AC.resource.config = ma_resource_manager_config_init();
	AC.resource.config.pLog = log;
	AC.resource.config.pVFS = vfs;
	AC.resource.config.allocationCallbacks = allocation_callback;

	ma_result result = ma_resource_manager_init(&AC.resource.config, &AC.resource.manager);
	if (result != MA_SUCCESS)
	{
		log_error(str8_from("failed to initialize the resource manager."));
		return (false);
	}

	AC.engine.config = ma_engine_config_init();
	AC.engine.config.pResourceManager = &AC.resource.manager;
	AC.engine.engine.pLog = log;
	AC.engine.config.allocationCallbacks = allocation_callback;

	result = ma_engine_init(&AC.engine.config, &AC.engine.engine);
	if (result != MA_SUCCESS)
	{
		ma_device_uninit(AC.engine.config.pDevice);
		log_error(str8_from("failed to initialize the engine."));
		return (false);
	}

	ma_engine_listener_set_world_up(&AC.engine.engine, 0, 0.0f, 1.0f, 0.0f);

	array_set_cap(&AC.arena, AC.audios, 5);
	memset(AC.audios, 0x0, sizeof(ma_sound) * 5);

	AC.map = str8_audio_map_make(&AC.arena);

	return (true);
}

void
audio_add_sound(str8 name, str8 file)
{
	array_push(&AC.arena, AC.audios, (ma_sound){0});
	ma_sound *psound = array_last_item(AC.audios);

	ma_result result = ma_sound_init_from_file(
	    &AC.engine.engine, file.idata, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, 0, 0, psound);
	if (result != MA_SUCCESS)
	{
		log_error(str8_from("[{s}] failed to load sound file: {s}."), name, file);
		return;
	}

	struct str8_audio_result put_result = str8_audio_map_put(&AC.arena, &AC.map, name, psound);
	if (put_result.ok)
	{
		log_error(str8_from("[{s}] duplicated sound/music!"), name);
		exit(1);
	}

	ma_sound_set_pinned_listener_index(put_result.value, 0);
}

void
audio_add_music(str8 name, str8 file)
{
	array_push(&AC.arena, AC.audios, (ma_sound){0});
	ma_sound *pmusic = array_last_item(AC.audios);
	ma_result result = ma_sound_init_from_file(&AC.engine.engine, file.idata, MA_SOUND_FLAG_STREAM, 0, 0, pmusic);
	if (result != MA_SUCCESS)
	{
		log_error(str8_from("[{s}] failed to load music file: {s}."), name, file);
		return;
	}

	struct str8_audio_result put_result = str8_audio_map_put(&AC.arena, &AC.map, name, pmusic);
	if (put_result.ok)
	{
		log_error(str8_from("[{s}] duplicated sound/music!"), name);
		exit(1);
	}
	ma_sound_set_pinned_listener_index(put_result.value, 0);
}

void
audio_manager_teardown(void)
{
	ma_engine_uninit(&AC.engine.engine);
}

void
audio_play(str8 name)
{
	struct str8_audio_result get_result = str8_audio_map_get(&AC.map, name);
	if (!get_result.ok)
	{
		log_warn(str8_from("[{s}] audio not found!"), name);
		return;
	}
	ma_sound_start(get_result.value);
}

void
audio_set_looping(str8 name, b8 loop)
{
	struct str8_audio_result get_result = str8_audio_map_get(&AC.map, name);
	if (!get_result.ok)
	{
		log_warn(str8_from("[{s}] audio not found. Audio loop not set"), name);
		return;
	}

	ma_sound_set_looping(get_result.value, loop);
}

void
audio_set_position(str8 name, v3 position)
{
	struct str8_audio_result get_result = str8_audio_map_get(&AC.map, name);
	if (!get_result.ok)
	{
		log_warn(str8_from("[{s}] audio not found. Audio position not set"), name);
		return;
	}

	ma_sound_set_position(get_result.value, position.x, position.y, position.z);
}

void
audio_set_listener_position(v3 position)
{
	ma_engine_listener_set_position(&AC.engine.engine, 0, position.x, position.y, position.z);
}

void
audio_set_listener_direction(v3 direction)
{
	ma_engine_listener_set_direction(&AC.engine.engine, 0, direction.x, direction.y, direction.z);
}

void
audio_set_listener_world_up(v3 up)
{
	ma_engine_listener_set_world_up(&AC.engine.engine, 0, up.x, up.y, up.z);
}

void
audio_set_listener_velocity(v3 velocity)
{
	ma_engine_listener_set_velocity(&AC.engine.engine, 0, velocity.x, velocity.y, velocity.z);
}

void
audio_set_master_volume(f32 volume)
{
	ma_result result = ma_engine_set_volume(&AC.engine.engine, volume);
	if (result != MA_SUCCESS) { log_error(str8_from("error while setting volume.")); }
}

void
audio_start(void)
{
	ma_result result = ma_engine_start(&AC.engine.engine);
	if (result != MA_SUCCESS) { log_error(str8_from("error while starting sound engine.")); }
}

void
audio_stop(void)
{
	ma_result result = ma_engine_stop(&AC.engine.engine);
	if (result != MA_SUCCESS) { log_error(str8_from("error while stopping sound engine.")); }
}
