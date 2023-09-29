// Modified version of https://github.com/septag/sx
//
// Copyright 2018 Sepehr Taghdisian (septag@github). All rights reserved.
// License: https://github.com/septag/sx#license-bsd-2-clause
//
// parts of this code is copied from bx library: https://github.com/bkaradzic/bx
// Copyright 2011-2019 Branimir Karadzic. All rights reserved.
// License: https://github.com/bkaradzic/bx#license-bsd-2-clause
//
#include "smThread.h"
#include "smCore.h"

#if defined(_DURANGO) || defined(_XBOX_ONE)
#	undef SM_PLATFORM_XBOXONE
#	define SM_PLATFORM_XBOXONE 1
#elif defined(__ANDROID__) || defined(ANDROID)
// Android compiler defines __linux__
#	include <sys/cdefs.h> // Defines __BIONIC__ and includes android/api-level.h
#	undef SM_PLATFORM_ANDROID
#	define SM_PLATFORM_ANDROID __ANDROID_API__
#elif defined(_WIN32) || defined(_WIN64)
// http://msdn.microsoft.com/en-us/library/6sehtctf.aspx
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif // NOMINMAX
//  If _USING_V110_SDK71_ is defined it means we are using the v110_xp or v120_xp toolset.
#	if defined(_MSC_VER) && (_MSC_VER >= 1700) && (!_USING_V110_SDK71_)
#		include <winapifamily.h>
#	endif // defined(_MSC_VER) && (_MSC_VER >= 1700) && (!_USING_V110_SDK71_)
#	if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
#		undef SM_PLATFORM_WINDOWS
#		if !defined(WINVER) && !defined(_WIN32_WINNT)
#			if SM_ARCH_64BIT
//				When building 64-bit target Win7 and above.
#				define WINVER	     0x0601
#				define _WIN32_WINNT 0x0601
#			else
//				Windows Server 2003 with SP1, Windows XP with SP2 and above
#				define WINVER	     0x0502
#				define _WIN32_WINNT 0x0502
#			endif // SM_ARCH_64BIT
#		endif	       // !defined(WINVER) && !defined(_WIN32_WINNT)
#		define SM_PLATFORM_WINDOWS _WIN32_WINNT
#	else
#		undef SM_PLATFORM_WINRT
#		define SM_PLATFORM_WINRT 1
#	endif
#elif defined(__VCCOREVER__) || defined(__RPI__)
// RaspberryPi compiler defines __linux__
#	undef SM_PLATFORM_RPI
#	define SM_PLATFORM_RPI 1
#elif defined(__linux__)
#	undef SM_PLATFORM_LINUX
#	define SM_PLATFORM_LINUX 1
#elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) || defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
#	undef SM_PLATFORM_IOS
#	define SM_PLATFORM_IOS 1
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#	undef SM_PLATFORM_OSX
#	define SM_PLATFORM_OSX __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#elif defined(__EMSCRIPTEN__)
#	undef SM_PLATFORM_EMSCRIPTEN
#	define SM_PLATFORM_EMSCRIPTEN 1
#elif defined(__ORBIS__)
#	undef SM_PLATFORM_PS4
#	define SM_PLATFORM_PS4 1
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
#	undef SM_PLATFORM_BSD
#	define SM_PLATFORM_BSD 1
#elif defined(__GNU__)
#	undef SM_PLATFORM_HURD
#	define SM_PLATFORM_HURD 1
#elif defined(__NX__)
#	undef SM_PLATFORM_NX
#	define SM_PLATFORM_NX 1
#endif //

#define SM_PLATFORM_POSIX                                                                                   \
	(0 || SM_PLATFORM_ANDROID || SM_PLATFORM_BSD || SM_PLATFORM_EMSCRIPTEN || SM_PLATFORM_HURD ||       \
	    SM_PLATFORM_IOS || SM_PLATFORM_LINUX || SM_PLATFORM_NX || SM_PLATFORM_OSX || SM_PLATFORM_PS4 || \
	    SM_PLATFORM_RPI)

#define SM_PLATFORM_NONE                                                                                    \
	!(0 || SM_PLATFORM_ANDROID || SM_PLATFORM_BSD || SM_PLATFORM_EMSCRIPTEN || SM_PLATFORM_HURD ||      \
	    SM_PLATFORM_IOS || SM_PLATFORM_LINUX || SM_PLATFORM_NX || SM_PLATFORM_OSX || SM_PLATFORM_PS4 || \
	    SM_PLATFORM_RPI || SM_PLATFORM_WINDOWS || SM_PLATFORM_WINRT || SM_PLATFORM_XBOXONE)

#if SM_PLATFORM_APPLE
#	include <dispatch/dispatch.h>
#	include <mach/mach.h>
#	include <pthread.h>
#elif SM_PLATFORM_POSIX
#	define __USE_GNU
#	include <errno.h>
#	include <pthread.h>
#	include <semaphore.h>
#	include <sys/prctl.h>
#	include <time.h>
#	include <unistd.h>
#	if defined(__FreeBSD__)
#		include <pthread_np.h>
#	endif
#	if SM_PLATFORM_LINUX || SM_PLATFORM_RPI
#		include <sys/syscall.h> // syscall
#	endif
#elif SM_PLATFORM_WINDOWS
// clang-format off
#    define VC_EXTRALEAN
#    define WIN32_LEAN_AND_MEAN
SM_PRAGMA_DIAGNOSTIC_PUSH()
SM_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(5105)
#    include <windows.h>
SM_PRAGMA_DIAGNOSTIC_POP()
#    include <limits.h>
#    include <synchapi.h>
// clang-format on
#endif // SM_PLATFORM_

// #include "sx/atomic.h"

struct sm__mutex
{
#if SM_PLATFORM_WINDOWS
	CRITICAL_SECTION handle;
#elif SM_PLATFORM_POSIX
	pthread_mutex_t handle;
#endif
};

struct sm__semaphore
{
#if SM_PLATFORM_APPLE
	dispatch_semaphore_t handle;
#elif SM_PLATFORM_POSIX
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	i32 count;
#elif SM_PLATFORM_WINDOWS
	HANDLE handle;
#endif
};

struct sm__signal
{
#if SM_PLATFORM_WINDOWS
#	if _WIN32_WINNT >= 0x0600
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE cond;
	i32 value;
#	else
	HANDLE e;
#	endif
#elif SM_PLATFORM_POSIX
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	i32 value;
#endif
};

struct thread
{
	struct semaphore sem;
	thread_cb *callback;

#if SM_PLATFORM_WINDOWS
	HANDLE handle;
	DWORD thread_id;
#elif SM_PLATFORM_POSIX
	pthread_t handle;
#	if SM_PLATFORM_APPLE
	str8 name;
#	endif
#endif

	void *user_data1;
	void *user_data2;
	i32 stack_sz;
	b8 running;
};

sm__static_assert(sizeof(struct sm__mutex) <= sizeof(struct mutex), "struct mutex size mismatch");
sm__static_assert(sizeof(struct sm__semaphore) <= sizeof(struct semaphore), "struct mutex size mismatch");
sm__static_assert(sizeof(struct sm__signal) <= sizeof(struct signal), "struct mutex size mismatch");

static size_t
os_minstacksz(void)
{
	return 32768; // 32kb
}

// Apple has different implementation for semaphores
#if SM_PLATFORM_APPLE

void
sync_semaphore_init(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	_sem->handle = dispatch_semaphore_create(0);
	sm__assertf(_sem->handle != NULL, "dispatch_semaphore_create failed");
}

void
sync_semaphore_release(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	if (_sem->handle)
	{
		dispatch_release(_sem->handle);
		_sem->handle = NULL;
	}
}

void
sync_semaphore_post(struct semaphore *sem, i32 count)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	for (i32 i = 0; i < count; i++) { dispatch_semaphore_signal(_sem->handle); }
}

b8
sync_semaphore_wait(struct semaphore *sem, i32 msecs)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	dispatch_time_t dt =
	    msecs < 0 ? DISPATCH_TIME_FOREVER : dispatch_time(DISPATCH_TIME_NOW, (i64)msecs * 1000000ll);
	return !dispatch_semaphore_wait(_sem->handle, dt);
}
#endif

// Other implementations are either Posix or Win32
#if SM_PLATFORM_POSIX

// Thread
static void *
thread_fn(void *arg)
{
	struct thread *thrd = (struct thread *)arg;

	union
	{
		void *ptr;
		i32 i;
	} cast;

#	if SM_PLATFORM_APPLE
	if (thrd->name.size > 0) { thread_setname(thrd, thrd->name); }
#	endif

	sync_semaphore_post(&thrd->sem, 1);
	cast.i = thrd->callback(thrd->user_data1, thrd->user_data2);
	return cast.ptr;
}

struct thread *
thread_create(struct arena *arena, thread_cb *callback, void *user_data1, i32 stack_sz, str8 name, void *user_data2)
{
	struct thread *thrd = arena_reserve(arena, sizeof(struct thread));
	// struct thread *thrd = (struct thread *)sx_malloc(alloc, sizeof(sx_thread));

	sync_semaphore_init(&thrd->sem);
	thrd->callback = callback;
	thrd->user_data1 = user_data1;
	thrd->user_data2 = user_data2;
	thrd->stack_sz = MAX(stack_sz, (i32)os_minstacksz());
	thrd->running = true;

	pthread_attr_t attr;
	i32 r = pthread_attr_init(&attr);
	sm__assertf(r == 0, "pthread_attr_init failed");
	r = pthread_attr_setstacksize(&attr, thrd->stack_sz);
	sm__assertf(r == 0, "pthread_attr_setstacksize failed");

#	if SM_PLATFORM_APPLE
	thrd->name = (str8){0};
	if (name.size > 0) { thrd->name = name; }
#	endif

	r = pthread_create(&thrd->handle, &attr, thread_fn, thrd);
	sm__assertf(r == 0, "pthread_create failed");

	// Ensure that thread callback is running
	sync_semaphore_wait(&thrd->sem, -1);

#	if !SM_PLATFORM_APPLE
	if (name.size > 0) { thread_setname(thrd, name); }
#	endif

	return thrd;
}

i32
thread_destroy(struct thread *thrd, struct arena *arena)
{
	sm__assertf(thrd->running, "Thread is not running!");

	union
	{
		void *ptr;
		i32 i;
	} cast;

	pthread_join(thrd->handle, &cast.ptr);

	sync_semaphore_release(&thrd->sem);

	thrd->handle = 0;
	thrd->running = false;

	arena_free(arena, thrd);

	return cast.i;
}

b8
thread_running(struct thread *thrd)
{
	return thrd->running;
}

void
thread_setname(sm__maybe_unused struct thread *thrd, sm__maybe_unused str8 name)
{
#	if SM_PLATFORM_APPLE
	sx_unused(thrd);
	pthread_setname_np(name.idata);
#	elif SM_PLATFORM_BSD
#		if defined(__NetBSD__)
	pthread_setname_np(thrd->handle, "%s", (void *)name.idata);
#		else
	pthread_set_name_np(thrd->handle, name.idata);
#		endif // defined(__NetBSD__)
#	elif (SM_CRT_GLIBC >= 21200) && !SM_PLATFORM_HURD
	pthread_setname_np(thrd->handle, name.idata);
#	elif SM_PLATFORM_LINUX
	prctl(PR_SET_NAME, name.data, 0, 0, 0);
#	endif
}

void
thread_yield(void)
{
	sched_yield();
}

// Mutex
void
sync_mutex_init(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;

	pthread_mutexattr_t attr;
	i32 r = pthread_mutexattr_init(&attr);
	sm__assert(r == 0);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	r = pthread_mutex_init(&_m->handle, &attr);
	sm__assertf(r == 0, "pthread_mutex_init failed");
}

void
sync_mutex_release(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	pthread_mutex_destroy(&_m->handle);
}

void
sync_mutex_enter(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	pthread_mutex_lock(&_m->handle);
}

void
sync_mutex_exit(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	pthread_mutex_unlock(&_m->handle);
}

b8
sync_mutex_try(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	return pthread_mutex_trylock(&_m->handle) == 0;
}

// Signal
static inline u64
sm__to_ns(const struct timespec *_ts)
{
	return _ts->tv_sec * UINT64_C(1000000000) + _ts->tv_nsec;
}

static inline void
sm__to_time_spec_ns(struct timespec *_ts, u64 _nsecs)
{
	_ts->tv_sec = _nsecs / UINT64_C(1000000000);
	_ts->tv_nsec = _nsecs % UINT64_C(1000000000);
}

static inline void
sm__tm_add(struct timespec *_ts, i32 _msecs)
{
	u64 ns = sm__to_ns(_ts);
	sm__to_time_spec_ns(_ts, ns + (u64)(_msecs)*1000000);
}

void
sync_signal_init(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
	_sig->value = 0;
	i32 r = pthread_mutex_init(&_sig->mutex, NULL);
	sm__assertf(r == 0, "pthread_mutex_init failed");

	r = pthread_cond_init(&_sig->cond, NULL);
	sm__assertf(r == 0, "pthread_cond_init failed");
}

void
sync_signal_release(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
	pthread_cond_destroy(&_sig->cond);
	pthread_mutex_destroy(&_sig->mutex);
}

void
sync_signal_raise(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
	i32 r = pthread_mutex_lock(&_sig->mutex);
	sm__assert(r == 0);
	_sig->value = 1;
	pthread_mutex_unlock(&_sig->mutex);
	pthread_cond_signal(&_sig->cond);
}

b8
sync_signal_wait(struct signal *sig, i32 msecs)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
	i32 r = pthread_mutex_lock(&_sig->mutex);
	sm__assert(r == 0);

	if (msecs == -1) { r = pthread_cond_wait(&_sig->cond, &_sig->mutex); }
	else
	{
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		sm__tm_add(&ts, msecs);
		r = pthread_cond_timedwait(&_sig->cond, &_sig->mutex, &ts);
	}

	b8 ok = r == 0;
	if (ok) _sig->value = 0;
	r = pthread_mutex_unlock(&_sig->mutex);
	return ok;
}

// Semaphore (posix only)
#	if !SM_PLATFORM_APPLE
void
sync_semaphore_init(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	_sem->count = 0;
	i32 r = pthread_mutex_init(&_sem->mutex, NULL);
	sm__assertf(r == 0, "pthread_mutex_init failed");

	r = pthread_cond_init(&_sem->cond, NULL);
	sm__assertf(r == 0, "pthread_cond_init failed");
}

void
sync_semaphore_release(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	pthread_cond_destroy(&_sem->cond);
	pthread_mutex_destroy(&_sem->mutex);
}

void
sync_semaphore_post(struct semaphore *sem, i32 count)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	i32 r = pthread_mutex_lock(&_sem->mutex);
	sm__assert(r == 0);

	for (i32 ii = 0; ii < count; ii++)
	{
		r = pthread_cond_signal(&_sem->cond);
		sm__assert(r == 0);
	}

	_sem->count += count;
	r = pthread_mutex_unlock(&_sem->mutex);
	sm__assert(r == 0);
}

b8
sync_semaphore_wait(struct semaphore *sem, i32 msecs)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	i32 r = pthread_mutex_lock(&_sem->mutex);
	sm__assert(r == 0);

	if (msecs == -1)
	{
		while (r == 0 && _sem->count <= 0) r = pthread_cond_wait(&_sem->cond, &_sem->mutex);
	}
	else
	{
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		sm__tm_add(&ts, msecs);
		while (r == 0 && _sem->count <= 0) r = pthread_cond_timedwait(&_sem->cond, &_sem->mutex, &ts);
	}

	b8 ok = r == 0;
	if (ok) { --_sem->count; }
	r = pthread_mutex_unlock(&_sem->mutex);
	return ok;
}
#	endif
#elif SM_PLATFORM_WINDOWS
// Mutex
void
sync_mutex_init(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	InitializeCriticalSection(&_m->handle);
}

void
sync_mutex_release(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	DeleteCriticalSection(&_m->handle);
}

void
sync_mutex_enter(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	EnterCriticalSection(&_m->handle);
}

void
sync_mutex_exit(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	LeaveCriticalSection(&_m->handle);
}

b8
sync_mutex_try(struct mutex *mutex)
{
	struct sm__mutex *_m = (struct sm__mutex *)mutex->data;
	return TryEnterCriticalSection(&_m->handle) == TRUE;
}

// Semaphore
void
sync_semaphore_init(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	_sem->handle = CreateSemaphoreA(NULL, 0, LONG_MAX, NULL);
	sm__assertf(_sem->handle != NULL, "Failed to create semaphore");
}

void
sync_semaphore_release(struct semaphore *sem)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	CloseHandle(_sem->handle);
}

void
sync_semaphore_post(struct semaphore *sem, i32 count)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	ReleaseSemaphore(_sem->handle, count, NULL);
}

b8
sync_semaphore_wait(struct semaphore *sem, i32 msecs)
{
	struct sm__semaphore *_sem = (struct sm__semaphore *)sem->data;
	DWORD _msecs = (msecs < 0) ? INFINITE : msecs;
	return WaitForSingleObject(_sem->handle, _msecs) == WAIT_OBJECT_0;
}

// Signal
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
void
sync_signal_init(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
#	if _WIN32_WINNT >= 0x0600
	BOOL r = InitializeCriticalSectionAndSpinCount(&_sig->mutex, 32);
	sm__assertf(r, "InitializeCriticalSectionAndSpinCount failed");
	InitializeConditionVariable(&_sig->cond);
	_sig->value = 0;
#	else
	_sig->e = CreateEvent(NULL, FALSE, FALSE, NULL);
	sm__assertf(_sig->e, "CreateEvent failed");
#	endif
}

void
sync_signal_release(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
#	if _WIN32_WINNT >= 0x0600
	DeleteCriticalSection(&_sig->mutex);
#	else
	CloseHandle(_sig->e);
#	endif
}

void
sync_signal_raise(struct signal *sig)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
#	if _WIN32_WINNT >= 0x0600
	EnterCriticalSection(&_sig->mutex);
	_sig->value = 1;
	LeaveCriticalSection(&_sig->mutex);
	WakeConditionVariable(&_sig->cond);
#	else
	SetEvent(_sig->e);
#	endif
}

b8
sync_signal_wait(struct signal *sig, i32 msecs)
{
	struct sm__signal *_sig = (struct sm__signal *)sig->data;
#	if _WIN32_WINNT >= 0x0600
	b8 timed_out = false;
	EnterCriticalSection(&_sig->mutex);
	DWORD _msecs = (msecs < 0) ? INFINITE : msecs;
	while (_sig->value == 0)
	{
		i32 r = SleepConditionVariableCS(&_sig->cond, &_sig->mutex, _msecs);
		if (!r && GetLastError() == ERROR_TIMEOUT)
		{
			timed_out = true;
			break;
		}
	}
	if (!timed_out) _sig->value = 0;
	LeaveCriticalSection(&_sig->mutex);
	return !timed_out;
#	else
	return WaitForSingleObject(_sig->e, msecs < 0 ? INFINITE : msecs) == WAIT_OBJECT_0;
#	endif
}

// Thread
static DWORD WINAPI
thread_fn(LPVOID arg)
{
	struct thread *thrd = (struct thread *)arg;
	thrd->thread_id = GetCurrentThreadId();
	sync_semaphore_post(&thrd->sem, 1);
	return (DWORD)thrd->callback(thrd->user_data1, thrd->user_data2);
}

struct thread *
thread_create(struct arena *arena, thread_cb *callback, void *user_data1, i32 stack_sz, str8 name, void *user_data2)
{
	// struct thread *thrd = (struct thread *)sx_malloc(alloc, sizeof(sx_thread));
	struct thread *thrd = arena_reserve(arena, sizeof(struct thread));
	if (!thrd) return NULL;

	sync_semaphore_init(&thrd->sem);
	thrd->callback = callback;
	thrd->user_data1 = user_data1;
	thrd->user_data2 = user_data2;
	thrd->stack_sz = MAX(stack_sz, (i32)os_minstacksz());
	thrd->running = true;

	thrd->handle = CreateThread(NULL, thrd->stack_sz, (LPTHREAD_START_ROUTINE)thread_fn, thrd, 0, NULL);
	sm__assert(thrd->handle != NULL, "CreateThread failed");

	// Ensure that thread callback is running
	sync_semaphore_wait(&thrd->sem, -1);

	if (name.size > 0) { thread_setname(thrd, name); }

	return thrd;
}

i32
thread_destroy(struct thread *thrd, struct arena *arena)
{
	sm__assert(thrd);
	sm__assertf(thrd->running, "Thread is not running!");

	DWORD exit_code;
	WaitForSingleObject(thrd->handle, INFINITE);
	GetExitCodeThread(thrd->handle, &exit_code);
	CloseHandle(thrd->handle);

	sync_semaphore_release(&thrd->sem);

	thrd->handle = INVALID_HANDLE_VALUE;
	thrd->running = false;

	// sx_free(alloc, thrd);
	arena_free(arena, thrd);

	return (i32)exit_code;
}

b8
thread_running(struct thread *thrd)
{
	return thrd->running;
}

void
thread_yield()
{
	SwitchToThread();
}

#	pragma pack(push, 8)

struct _ThreadName
{
	DWORD type;
	LPCSTR name;
	DWORD id;
	DWORD flags;
};

#	pragma pack(pop)

void
thread_setname(struct thread *thrd, str8 name)
{
	struct _ThreadName tn;
	tn.type = 0x1000;
	tn.name = name.idata;
	tn.id = thrd->thread_id;
	tn.flags = 0;

#	if !SM_CRT_MINGW
	__try
	{
#	endif

		RaiseException(0x406d1388, 0, sizeof(tn) / 4, (ULONG_PTR *)(&tn));

#	if !SM_CRT_MINGW
	} __except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
#	endif
}

#else
#	error "Not implemented for this platform"
#endif

u32
thread_tid(void)
{
#if SM_PLATFORM_WINDOWS
	return GetCurrentThreadId();
#elif SM_PLATFORM_LINUX || SM_PLATFORM_RPI || SM_PLATFORM_STEAMLINK
	return (pid_t)syscall(SYS_gettid);
#elif SM_PLATFORM_APPLE
	return (mach_port_t)pthread_mach_thread_np(pthread_self());
#elif SM_PLATFORM_BSD
	return *(u32 *)pthread_self();
#elif SM_PLATFORM_ANDROID
	return gettid();
#elif SM_PLATFORM_HURD
	return (pthread_t)pthread_self();
#else
	sm__assertf(0, "Tid not implemented");
#endif // SM_PLATFORM_
}
