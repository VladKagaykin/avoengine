#include <vector>
#pragma once
extern int tick;
extern const int max_tick;
extern int absolute_tick;
void timer();
void render_loop(int);
void init_tick_system();
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices);
void enable_fog(float density,float r,float g,float b,float start=5.0f,float end=30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start,float end);
void set_fog_color(float r,float g,float b);
void set_fog_density(float density);
void delay_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop=false);
void disappearing_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop=false);
void play_white_noise_3d(float x, float y, float z, float volume);
