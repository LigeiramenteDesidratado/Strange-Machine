#ifndef SM_EFFECTS_SHAKE
#define SM_EFFECTS_SHAKE

#include "core/smCore.h"

struct shake
{
	f32 duration;
	f32 start_time;
	f32 t;

	f32 freq;
	f32 amp;

	b8 is_shaking;
};

struct shake shake_make(f32 duration, f32 freq, f32 amplitude);
void shake_do(struct shake *shake, v3 *v);
void shake_start(struct shake *shake);

#endif /* SM_EFFECTS_SHAKE */
