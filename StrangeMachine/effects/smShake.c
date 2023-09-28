#include "effects/smShake.h"

f32 sm__shake_decay(const struct shake *shake);
f32 sm__shake_amplitude(const struct shake *shake);

struct shake
shake_make(f32 duration, f32 freq, f32 amp)
{
	struct shake result;

	// The duration in seconds
	result.duration = duration;

	// The frequency in Hz
	result.freq = freq;
	result.amp = amp;

	result.start_time = 0;
	result.t = 0;
	result.is_shaking = false;

	return (result);
}

void
shake_do(struct shake *shake, v3 *v)
{
	if (!shake->is_shaking) { return; }

	shake->t = core_get_time() - shake->start_time;

	if (shake->t > shake->duration) { shake->is_shaking = false; }

	f32 amp = sm__shake_amplitude(shake) * shake->amp;

	if (shake->is_shaking)
	{
		switch (prng_min_max(0, 9))
		{
		case 0:
			{
				v->x = v->x + amp;
				v->y = v->y + amp;
				v->z = v->z + amp;
			}
			break;

		case 1:
			{
				v->x = v->x - amp;
				v->y = v->y - amp;
				v->z = v->z - amp;
			}
			break;

		case 2:
			{
				v->x = v->x + amp;
				v->y = v->y - amp;
				v->z = v->z - amp;
			}
			break;
		case 3:
			{
				v->x = v->x - amp;
				v->y = v->y + amp;
				v->z = v->z - amp;
			}
			break;
		case 4:
			{
				v->x = v->x - amp;
				v->y = v->y - amp;
				v->z = v->z + amp;
			}
			break;
		case 5:
			{
				v->x = v->x - amp;
				v->y = v->y + amp;
				v->z = v->z + amp;
			}
			break;
		case 6:
			{
				v->x = v->x + amp;
				v->y = v->y - amp;
				v->z = v->z + amp;
			}
			break;
		default:
			{
				v->x = v->x + amp;
				v->y = v->y + amp;
				v->z = v->z - amp;
			}
			break;
		}
	}
}

void
shake_start(struct shake *shake)
{
	shake->start_time = core_get_time();
	shake->t = 0;
	shake->is_shaking = true;
}

f32
sm__shake_amplitude(const struct shake *shake)
{
	// Get the previous and next sample
	f32 s = shake->t / 1000.0f * shake->freq;

	f32 j = floor(s);
	i32 s0 = (i32)j;
	// i32 s1 = s0 + 1;

	// Get the current decay
	f32 k = sm__shake_decay(shake);

	f32 noise = f32_range11();
	f32 noise1 = f32_range11();

	f32 amp = (noise + (s - s0) * (noise1 - noise)) * k;

	return (amp);
}

f32
sm__shake_decay(const struct shake *shake)
{
	// Linear decay
	if (shake->t >= shake->duration) { return 0.0; }

	return (shake->duration - shake->t) / shake->duration;
}
