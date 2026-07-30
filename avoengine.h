#ifndef AVOENGINE_H
#define AVOENGINE_H
#include <GL/glew.h>
#include <string>
#include <vector>
#include <cmath>
#include <map>
#include <unordered_map>
// #include "src/miniaudio.h"
#include <glm/glm.hpp>
class Portal;
class pseudo_3d_entity;
#include <mutex>

using namespace std;

struct GLFWwindow;

std::string getCPUName_Linux();
std::string getRAMTotal_Linux();
std::string getGPUName_OpenGL();

std::vector<float> getProcessCPUUsage_Win();
long getProcessRAMUsage_Win();
float getGPUUsage_Win();

std::vector<float> getProcessCPUUsage_Linux();
long getProcessRAMUsage_Linux();
float getGPUUsage_Linux();

extern int window_w;
extern int window_h;
extern int screen_w;
extern int screen_h;

extern std::string cpu_name;
extern std::string gpu_name;
extern std::string ram_v;

class Light;

class WarpPlane;

enum DrawCommandType : int {
    CMD_SQUARE,
    CMD_3DOBJECT,
    CMD_LINE_2D,
    CMD_PORTAL
};

struct DrawCommand {
    DrawCommandType type;

    float scale, cx, cy, r, g, b, rotate;
    float verts[8];
    int vertCount;
    std::string tex;

    std::string text;
    float x, y;
    void* font;
    float a;

    float obj_cx, obj_cy, obj_cz;
    float obj_r, obj_g, obj_b;
    float obj_alpha;
    std::string obj_tex;
    std::vector<float> obj_vertices;
    std::vector<int> obj_indices;
    std::vector<float> obj_texcoords;
    std::vector<float> obj_normals;
    float radius = 0.0f;
    float obj_yaw = 0.0f;
    float obj_pitch = 0.0f;
    float obj_roll = 0.0f;

    const pseudo_3d_entity* entity;
    float cam_x, cam_y, cam_z;

    GLuint shaderID = 0;

    Portal* portal = nullptr;
};

extern std::vector<DrawCommand> drawQueue;
extern std::mutex drawQueueMutex;

extern GLint loc_tex;
extern GLint loc_portalMode , loc_portalDepthOnly , loc_portalTex ;

extern glm::mat4 g_projectionMatrix;
extern glm::mat4 g_modelViewMatrix;
extern GLint loc_u_projection_default;
extern GLint loc_u_modelView_default;

void updateMatrixUniforms();

struct CameraParams{
    float fov=58.0f;
    float znear=0.1f;
    float zfar=256.0f;
    float eye_x=0,eye_y=0,eye_z=0;
    float ctr_x=0,ctr_y=0,ctr_z=1;
    float up_x=0,up_y=1,up_z=0;
    float dir_x=0,dir_y=0,dir_z=1;
    float pitch=0.0f;
    float yaw=0.0f;
    float roll=0.0f;
    bool was_inverted = false;
};

extern CameraParams camera;
// сила искажения должна быть в ~10 раз меньше шага, чем больше шаг, тем больше артефакты
struct settings{
    int MAX_LIGHTS=16;
    int MAX_BOUNCES= 4;
    float MAX_DIST=camera.zfar;
    int MAX_TEXTURES= 256;
    int MAX_PORTALS= 8;
    int MAX_PORTAL_VERTS= 16;
    float SHADOW_BIAS= 0.001;
    float CAM_WARP_STRENGTH= 0.02;
    float SHADOW_WARP_STRENGTH= 0.02;
    float CAM_STEP_SIZE= 0.2;
    float SHADOW_STEP_SIZE= 0.2;
    int MAX_SHADOW_BOUNCES= 2;
    float RAY_MULTIPLY=1;
    int TEXT_SAMPLE=4;
    int TICK_SPEED=50000;
    bool DEBUG_GRAPHICS=1;
    int DEBUG_COLOR[3]={0,255,255};
    int ATLAS_SIDE=4096;
    int TEXTURE_SIDE=256;
    int ATLAS_PADDING=5; 
};

extern settings Engine_settings;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

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

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);

extern GLuint defaultLightingShader;
#endif
