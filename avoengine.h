#ifndef AVOENGINE_H
#define AVOENGINE_H
#include <GL/glew.h>
#include <string>
#include <vector>
#include <cmath>
#include <unordered_map>
// #include "src/miniaudio.h"
#include <glm/glm.hpp>
class Portal;
class pseudo_3d_entity;
#include <mutex>

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

extern GLuint currentShaderProg; 
using namespace std; 
extern unordered_map<string, GLuint> textureCache;

extern mutex textureCacheMutex;

class Light;

GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();

class WarpPlane {
public:
    float originX, originY, originZ;
    float yaw, pitch, roll;
    float sizeU, sizeV;
    GLuint displacementTex;
    bool enabled;

    WarpPlane();
    void setDisplacementTexture(const char* filename);
    void setDisplacementFromData(int w, int h, const float* data);
    void enable();
    void disable();
};

extern WarpPlane* activeWarpPlane;
void set_active_warp_plane(WarpPlane* wp);

extern bool is_scene_changed;
using Function = void(*)();
extern Function current_scene;

void fixed_scene(Function scene);
void clean_scene();

enum DrawCommandType : int {
    CMD_SQUARE,
    CMD_TEXT,
    CMD_3DOBJECT,
    CMD_PSEUDO3D,
    CMD_LINE_2D,
    CMD_LINE_3D,
    CMD_PANORAMA,
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

void flushDrawQueue();

GLuint loadTextureFromFile(const char* filename);
void clearTextureCache();

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
    int MAX_TEXTURES= 2;
    int MAX_PORTALS= 8;
    int MAX_PORTAL_VERTS= 16;
    float SHADOW_BIAS= 0.001;
    float CAM_WARP_STRENGTH= 0.2;
    float SHADOW_WARP_STRENGTH= 0.2;
    float CAM_STEP_SIZE= 2;
    float SHADOW_STEP_SIZE= 2;
    int MAX_SHADOW_BOUNCES= 2;
    float RAY_MULTIPLY=0.5;
    int TEXT_SAMPLE=4;
};

extern settings Engine_settings;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);

extern GLuint defaultLightingShader;
#endif
