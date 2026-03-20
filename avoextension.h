#include <vector>
#pragma once
extern int tick;
extern const int max_tick;
extern int absolute_tick;
void timer();
void render_loop(int);
void init_tick_system();
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices);
void delay_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop=false);
void disappearing_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop=false);
void play_white_noise_3d(float x, float y, float z, float volume);
