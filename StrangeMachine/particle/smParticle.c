
#include "particle/smParticle.h"

struct particle
particle_make(void)
{
	struct particle result;

	result.position = v3_zero();
	result.velocity = v3_up();

	return (result);
}

void
particle_update(struct particle *particle, f32 dt)
{
	v3 velocity;
	glm_vec3_scale(particle->velocity.data, dt, velocity.data);
	glm_vec3_add(particle->position.data, velocity.data, particle->position.data);
}
