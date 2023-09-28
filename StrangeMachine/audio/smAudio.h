#ifndef SM_AUDION_H
#define SM_AUDION_H

#include "core/smCore.h"
#include "math/smMath.h"

b8 audio_manager_init(void);
void audio_manager_teardown(void);

void audio_add_sound(str8 name, str8 file);
void audio_add_music(str8 name, str8 file);
void audio_manager_teardown(void);
void audio_play(str8 name);
void audio_set_looping(str8 name, b8 loop);
void audio_set_position(str8 name, v3 position);
void audio_set_listener_position(v3 position);
void audio_set_listener_direction(v3 direction);
void audio_set_listener_world_up(v3 up);
void audio_set_listener_velocity(v3 velocity);
void audio_set_master_volume(f32 volume);
void audio_start(void);
void audio_stop(void);

#endif // SM_AUDION_H
