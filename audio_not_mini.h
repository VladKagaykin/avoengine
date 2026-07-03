#ifndef AUDIO_NOT_MINI
#define AUDIO_NOT_MINI

#include <vector>
#include "src/miniaudio.h"

// std::vector<ma_sound*> loopingSounds;

extern ma_engine audio_engine;

void init_audio();
void play_sound(const char* filename,float volume=1.0f);
void play_sound_loop(const char* filename,float volume=1.0f);
void play_sound_3d(const char* filename,float x,float y,float z,float volume=1.0f);
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume=1.0f);
void stop_all_looping_sounds();

#endif