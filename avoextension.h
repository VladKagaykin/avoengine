#pragma once
#ifndef AVOEXTENSION_H
#define AVOEXTENSION_H
#include <vector>
#include <string>
#include <GL/glew.h>
#include <vector>
#include <GL/gl.h>
#include "avoengine.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>
#include <unordered_map>
struct GLFWwindow;

extern int tick;
extern const int max_tick;
extern int absolute_tick;

void init_tick_system();
void update_ticks();
void set_icon(const char* path);
extern bool keys[256];
extern bool skeys[512];
void init_keyboard(GLFWwindow* window);
extern std::map<std::string, bool> mouse;
extern int mouse_x,mouse_y;
extern bool mouse_captured;
void init_mouse(GLFWwindow* window);
void set_mouse_capture(GLFWwindow* window, bool capture);
void update_mouse();
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices);

void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);
void delay_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop = false);
void disappearing_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop = false);

void play_white_noise_3d(float x, float y, float z, float volume);

#endif