#ifndef AVOENGINE_H
#define AVOENGINE_H
#include <GL/glew.h>
#include <GL/glut.h>
#include <string>
#include <vector>
#include <cmath>
#include "miniaudio.h"

extern int window_w;
extern int window_h;
extern int screen_w;
extern int screen_h;

extern std::string cpu_name;
extern std::string gpu_name;
extern std::string ram_v;

GLuint loadTextureFromFile(const char* filename);
void clearTextureCache();
void preloadTextures(const std::vector<std::string>& filenames);

extern ma_engine audio_engine;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

void triangle(float scale,float cx,float cy,double r,double g,double b,float rotate,const float* vertices,const char* texture_file=nullptr);

void square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* texture_file=nullptr);

void circle(float scale,float cx,float cy,double r,double g,double b,float radius,float in_radius,float rotate,int slices,int loops,const char* texture_file=nullptr);

void draw_text(const char* text,float x,float y,void* font,float r,float g,float b,float a=1.0f);

class pseudo_3d_entity{
public:
    pseudo_3d_entity(float x,float y,float z,float g_angle,float v_angle,std::vector<const char*> textures,int v_angles,float* vertices);
    void draw(float cam_h,float cam_x,float cam_y,float cam_z)const;
    void setRadius(float r){radius=r;}
    float getRadius()const{return radius;}
    void setGAngle(float a){g_angle=a;}
    void setVAngle(float a){v_angle=a;}
    float getGAngle()const{return g_angle;}
    float getVAngle()const{return v_angle;}
    float getX()const{return x;}
    float getY()const{return y;}
    float getZ()const{return z;}
private:
    int getTextureIndex(float cam_h,float cam_v)const;
    bool isVisible(float cam_x,float cam_y,float cam_z)const;
    float x,y,z,g_angle,v_angle;
    float radius=1.0f;
    std::vector<const char*>textures;
    int v_angles;
    float* vertices;
    mutable int cachedTexIdx=-1;
    mutable float cachedCamH=1e9f;
    mutable float cachedCamV=1e9f;
};
void drawEntities(std::vector<pseudo_3d_entity>& entities,float cam_h,float cam_x,float cam_y,float cam_z);
void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void changeSize3D(int w,int h);
void changeSize2D(int w,int h);
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw);
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw);
void draw3DObject(float cx,float cy,float cz,double r,double g,double b,const char* texture_file,const std::vector<float>& vertices,const std::vector<int>& indices,const std::vector<float>& texcoords={});
void begin_2d(int w,int h);
void end_2d();
void init_audio();
void play_sound(const char* filename,float volume=1.0f);
void play_sound_loop(const char* filename,float volume=1.0f);
void play_sound_3d(const char* filename,float x,float y,float z,float volume=1.0f);
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume=1.0f);
void stop_all_looping_sounds();
void draw_performance_hud(int win_w,int win_h);
#endif 