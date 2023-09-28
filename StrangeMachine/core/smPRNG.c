#include "core/smCore.h"

static u32 state128[4] = {0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b};
static u64 state256[4] = {0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c};

void
prng_seed(u64 seed)
{
	state128[0] = (u32)seed ^ state128[0];
	state128[0] = (u32)seed ^ state128[1];
	state128[0] = (u32)seed ^ state128[2];
	state128[0] = (u32)seed ^ state128[3];

	state256[0] = seed ^ state256[0];
	state256[0] = seed ^ state256[1];
	state256[0] = seed ^ state256[2];
	state256[0] = seed ^ state256[3];
}

static inline u32
rotl128(const u32 x, int k)
{
	return (x << k) | (x >> (32 - k));
}

static inline u64
rotl256(const u64 x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static u64
next256pp(void)
{
	const u64 result = rotl256(state256[0] + state256[3], 23) + state256[0];

	const u64 t = state256[1] << 17;

	state256[2] ^= state256[0];
	state256[3] ^= state256[1];
	state256[1] ^= state256[2];
	state256[0] ^= state256[3];

	state256[2] ^= t;

	state256[3] = rotl256(state256[3], 45);

	return (result);
}

static u64
next256p(void)
{
	const u64 result = state256[0] + state256[3];

	const u64 t = state256[1] << 17;

	state256[2] ^= state256[0];
	state256[3] ^= state256[1];
	state256[1] ^= state256[2];
	state256[0] ^= state256[3];

	state256[2] ^= t;

	state256[3] = rotl256(state256[3], 45);

	return (result);
}

static u32
next128pp(void)
{
	const u32 result = rotl128(state128[0] + state128[3], 7) + state128[0];

	const u32 t = state128[1] << 9;

	state128[2] ^= state128[0];
	state128[3] ^= state128[1];
	state128[1] ^= state128[2];
	state128[0] ^= state128[3];

	state128[2] ^= t;

	state128[3] = rotl128(state128[3], 11);

	return (result);
}

static u32
next128p(void)
{
	const u32 result = state128[0] + state128[3];

	const u32 t = state128[1] << 9;

	state128[2] ^= state128[0];
	state128[3] ^= state128[1];
	state128[1] ^= state128[2];
	state128[0] ^= state128[3];

	state128[2] ^= t;

	state128[3] = rotl128(state128[3], 11);

	return (result);
}

f64
f64_range11(void)
{
	u64 x = next256p();

	const union
	{
		u64 i;
		f64 d;
	} u = {.i = 0x4000000000000000UL | x >> 12};

	return (u.d - 3.0);
}

f64
f64_range01(void)
{
	u64 x = next256p();

	const union
	{
		u64 i;
		f64 d;
	} pun = {.i = 0x3FFUL << 52 | x >> 12};

	return (pun.d - 1.0);
}

f32
f32_range11(void)
{
	u32 x = next128p();

	const union
	{
		u32 i;
		f32 f;
	} pun = {.i = 0x40000000U | x >> 9};

	return (pun.f - 3.0f);
}

f32
f32_range01(void)
{
	u32 x = next128p();

	const union
	{
		u32 i;
		f32 f;
	} pun = {.i = 0x3F800000U | x >> 9};

	return (pun.f - 1.0f);
}

f32
f32_min_max(f32 min, f32 max)
{
	u32 x = next128p();

	const union
	{
		u32 i;
		f32 f;
	} u = {.i = 0x3F800000U | x >> 9};

	return (min + (u.f - 1.0f) * (max - min));
}

f64
f64_min_max(f64 min, f64 max)
{
	u64 x = next256p();

	const union
	{
		u64 i;
		f64 f;
	} u = {.i = 0x3FFUL << 52 | x >> 12};

	return (min + (u.f - 1.0) * (max - min));
}

u64
u64_min_max(u64 min, u64 max)
{
	u64 x = next256pp();

	return (u64)((f64)min + ((f64)x / (f64)UINT64_MAX) * (f64)(max - min));
}

i64
i64_min_max(i64 min, i64 max)
{
	u64 x = next256pp();

	return (i64)((f64)min + ((f64)x / (f64)UINT64_MAX) * (f64)(max - min));
}

u32
u32_min_max(u32 min, u32 max)
{
	u32 x = next128pp();

	return (u32)((f32)min + ((f32)x / (f32)UINT32_MAX) * (f32)(max - min));
}

i32
i32_min_max(i32 min, i32 max)
{
	u32 x = next128pp();

	return (i32)((f32)min + ((f32)x / (f32)UINT32_MAX) * (f32)(max - min));
}

u32
u32_prng(void)
{
	u32 result = next128pp();

	return (result);
}

i32
i32_prng(void)
{
	i32 result = next128pp();

	return (result);
}

u64
u64_prng(void)
{
	u64 result = next256pp();

	return (result);
}

i64
i64_prng(void)
{
	i64 result = next256pp();

	return (result);
}
