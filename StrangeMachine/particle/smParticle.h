#ifndef SM_PARTICLE_H
#define SM_PARTICLE_H

#include "core/smCore.h"

struct particle;

struct particle
{
	struct particle *next, *prev;

	v3 position;
	// v4 rotation;
	v3 velocity;
	color color_begin, color_end;

	f32 energy, energy_remaing;

	enum
	{
		PARTICLE_TYPE_NONE = 0,

		// enforce 32-bit size enum
		SM__PARTICLE_TYPE_ENFORCE_ENUM_SIZE = 0x7fffffff
	} type;
};

void particle_update(struct particle *particle, f32 dt);
struct particle particle_make(void);

#endif // SM_PARTICLE_H
