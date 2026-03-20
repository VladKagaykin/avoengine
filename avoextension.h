#pragma once
extern int tick;
extern const int max_tick;
void timer();
void init_tick_system();
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const vector<float>& vertices);
void play_white_noise_3d(float x, float y, float z, float volume);
