#ifndef SM_CORE_REF_COUNT_H
#define SM_CORE_REF_COUNT_H

#include "core/smBase.h"

#define USE_ATOMIC 1

struct ref_counter
{
#if USE_ATOMIC
	_Atomic i32 ref_count;
#else
	i32 ref_count;
#endif
};

sm__force_inline void
rc_store(struct ref_counter *rc, i32 value)
{
#if USE_ATOMIC
	atomic_store(&rc->ref_count, value);
#else
	rc->ref_count = value;
#endif
}

sm__force_inline i32
rc_load(struct ref_counter *rc)
{
#if USE_ATOMIC
	return (atomic_load(&rc->ref_count));
#else
	return (rc->ref_count);
#endif
}

sm__force_inline i32
rc_increment(struct ref_counter *rc)
{
#if USE_ATOMIC
	return (atomic_fetch_add(&rc->ref_count, 1));
#else
	return (++rc->ref_count);
#endif
}

sm__force_inline i32
rc_decrement(struct ref_counter *rc)
{
#if USE_ATOMIC
	return (atomic_fetch_sub(&rc->ref_count, 1));
#else
	return (--rc->ref_count);
#endif
}

sm__force_inline i32
rc_exchange(struct ref_counter *rc, i32 value)
{
#if USE_ATOMIC
	return atomic_exchange(&rc->ref_count, value);
#else
	i32 result = rc->ref_count;
	rc->ref_count = value;

	return (result);
#endif
}

#endif // SM_CORE_REF_COUNT_H
