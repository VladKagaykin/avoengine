#ifndef AVOENGINE_H
#define AVOENGINE_H
#include <GL/glew.h>
#include <GL/glut.h>
#include <string>
#include <vector>
#include <cmath>
#include <unordered_map>
#include "src/miniaudio.h"
#include <glm/glm.hpp>
class Portal;
class pseudo_3d_entity;
#include <mutex>

struct GLFWwindow;

extern int window_w;
extern int window_h;
extern int screen_w;
extern int screen_h;

extern std::string cpu_name;
extern std::string gpu_name;
extern std::string ram_v;

extern GLuint currentShaderProg; 

class Light;
extern std::vector<Light*> activeLights;

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

extern ma_engine audio_engine;

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
    float RAY_MULTIPLY=1;
    int TEXT_SAMPLE=4;
};

extern settings Engine_settings;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2, float r, float g, float b, float a, float thickness);
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex=nullptr, float alpha = 1.0f);

void draw_text(const char* text, float x, float y, const char* fontPath, int fontSize, float r, float g, float b, float a=1);

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll = 0.0f);
void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float a, float thickness,
                  int segments, float alpha = 1.0f);
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f,
                  float alpha = 1.0f);

class Light {
public:
    Light();
    void setPosition(float x, float y, float z);
    void setDirectionFromPitchYaw(float pitch_deg, float yaw_deg);
    void setColor(float r, float g, float b);
    void setIntensity(float intensity);
    void setRadius(float radius_deg);
    void setAttenuation(float constant, float linear, float quadratic);
    void enable();
    void disable();
    bool isEnabled() const { return enabled; }

    bool enabled = false;
    float pos[3]   = {0,0,0};
    float dir[3]   = {0,0,-1};
    float color[3] = {1,1,1};
    float intensity = 1.0f;
    float cutoff    = 180.0f;
    float constAtt  = 1.0f;
    float linearAtt = 0.0f;
    float quadAtt   = 0.0f;
};

void applyAllLights();

extern GLuint defaultLightingShader;

void set_ambient_light(float r, float g, float b);
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);
void init_audio();
void play_sound(const char* filename,float volume=1.0f);
void play_sound_loop(const char* filename,float volume=1.0f);
void play_sound_3d(const char* filename,float x,float y,float z,float volume=1.0f);
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume=1.0f);
void stop_all_looping_sounds();
void draw_performance_hud(int win_w,int win_h, const char* font_path);

struct sphere_panorama {
    bool enabled = false;
    GLuint texture = 0;
    std::string path;
};
extern sphere_panorama sphere_sky;

void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);

struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
extern fog_params fog;

extern float global_ambient[3];
#endif
