#ifndef SM_THREAD_H
#define SM_THREAD_H

// Modified version of https://github.com/septag/sx
//
// Copyright 2018 Sepehr Taghdisian (septag@github). All rights reserved.
// License: https://github.com/septag/sx#license-bsd-2-clause
//
// parts of this code is copied from bx library: https://github.com/bkaradzic/bx
// Copyright 2011-2019 Branimir Karadzic. All rights reserved.
// License: https://github.com/bkaradzic/bx#license-bsd-2-clause
//
// threads.h - v1.0 - Common portable multi-threading primitives
//
//      sync_thread       Portable thread
//      sync_tls          Portable thread-local-storage which you can store a user_data per Tls
//      sync_mutex        Portable OS mutex, use for long-time data locks, for short-time locks use
//                      sync_lock_t in atomics.h
//      sync_sem          Portable OS semaphore. 'post' increases the count. 'wait' waits on semaphore
//                      if count is zero,
//                      else decreases the count and continue
//      sync_signal       Portable OS signals/events. simplified version of the semaphore,
//                      where you 'wait' for signal to be triggered, then in another thread you
//                      'raise' it and 'wait' will continue
//      sync_queue_spsc   Single producer/Single consumer self contained queue
//

#include "smBase.h"
#include "smString.h"

typedef struct sync_alloc sync_alloc;

// Thread
struct thread;

// Thread callback function
typedef i32(thread_cb)(void *user_data1, void *user_data2);

struct thread *thread_create(
    struct arena *arena, thread_cb *callback, void *user_data1, i32 stack_sz, str8 name, void *user_data2);
i32 thread_destroy(struct thread *thrd, struct arena *arena);
b8 thread_running(struct thread *thrd);
void thread_setname(struct thread *thrd, str8 name);
void thread_yield(void);
u32 thread_tid(void);

#if defined(__GNUC__) || defined(__clang__)
#	define sync_align_decl(_align, _decl) _decl __attribute__((aligned(_align)))
#else
#	define sync_align_decl(_align, _decl) __declspec(align(_align)) _decl
#endif

// Mutex
sync_align_decl(64, struct) mutex
{
	u8 data[64];
};

void sync_mutex_init(struct mutex *mutex);
void sync_mutex_release(struct mutex *mutex);
void sync_mutex_enter(struct mutex *mutex);
void sync_mutex_exit(struct mutex *mutex);
b8 sync_mutex_try(struct mutex *mutex);

#define sync_mutex_lock(_mtx)	 sync_mutex_enter(_mtx)
#define sync_mutex_unlock(_mtx)	 sync_mutex_exit(_mtx)
#define sync_mutex_trylock(_mtx) sync_mutex_try(_mtx)

// Semaphore
sync_align_decl(16, struct) semaphore
{
	u8 data[128];
};

void sync_semaphore_init(struct semaphore *sem);
void sync_semaphore_release(struct semaphore *sem);
void sync_semaphore_post(struct semaphore *sem, i32 count);
b8 sync_semaphore_wait(struct semaphore *sem, i32 msecs);

// Signal
sync_align_decl(16, struct) signal
{
	u8 data[128];
};

void sync_signal_init(struct signal *sig);
void sync_signal_release(struct signal *sig);
void sync_signal_raise(struct signal *sig);
b8 sync_signal_wait(struct signal *sig, i32 msecs);

#endif // SM_THREAD_H
