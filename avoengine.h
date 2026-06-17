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

struct settings{
    int MAX_LIGHTS=16;
    int MAX_BOUNCES= 4;
    float MAX_DIST=camera.zfar;
    int MAX_TEXTURES= 2;
    int MAX_PORTALS= 8;
    int MAX_PORTAL_VERTS= 16;
    float SHADOW_BIAS= 0.001;
    float CAM_WARP_STRENGTH= 0.05;
    float SHADOW_WARP_STRENGTH= 0.05;
    float CAM_STEP_SIZE= 0.99;
    float SHADOW_STEP_SIZE= 0.99;
    int MAX_SHADOW_BOUNCES= 2;
    float RAY_MULTIPLY=0.9;
};

extern settings Engine_settings;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2, float r, float g, float b, float a, float thickness);
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex=nullptr, float alpha = 1.0f);

void draw_text(const char* text,float x,float y,void* font,float r,float g,float b,float a=1.0f);

class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     const std::vector<std::string>& textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;

    float getRadius() const { return radius; }

    float getX() const { return x; }
    float getY() const { return y; }
    float getZ() const { return z; }

    void setGAngle(float a) { g_angle = a; }
    void setVAngle(float a) { v_angle = a; }
    void setRAngle(float a) { r_angle = a; }

    float getGAngle() const { return g_angle; }
    float getVAngle() const { return v_angle; }
    float getRAngle() const { return r_angle; }

    GLuint getShadowTexture(float dir_x, float dir_y, float dir_z) const;
    GLuint getTextureFromDirection(float dir_x, float dir_y, float dir_z) const;
    const std::vector<std::string>& getTextures() const { return textureFiles; }
    const std::vector<float>& getVertices() const { return vertices_; }
    const std::vector<GLuint>& getTextureIDs() const { return textureIDs; }
    int getVAngles() const { return v_angles; }

    int getTextureIndex(float dir_x, float dir_y, float dir_z) const;
    bool isVisible(float cam_x, float cam_y, float cam_z) const;

    GLuint getTextureID(int index) const { return (index >= 0 && index < (int)textureIDs.size()) ? textureIDs[index] : 0; }

    float x, y, z;
    float g_angle, v_angle, r_angle;
    float radius;
private:
    void computeRadius();

    std::vector<std::string> textureFiles;
    std::vector<GLuint> textureIDs;
    int v_angles;
    std::vector<float> vertices_;

    mutable int cachedTexIdx = -1;
    mutable float cachedDirX = 1e9f, cachedDirY = 1e9f, cachedDirZ = 1e9f;
};

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void changeSize3D(int w,int h);
void changeSize2D(int w,int h);
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
void draw_performance_hud(int win_w,int win_h);

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

#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Portal {
public:
    Portal(
        float ax, float ay, float az,
        float bx, float by, float bz,
        const std::vector<float>& vertices,
        float yawA = 0.0f, float pitchA = 0.0f, float rollA = 0.0f,
        float yawB = 0.0f, float pitchB = 0.0f, float rollB = 0.0f
    );
    ~Portal();

    void draw();                  
    void checkTeleport();        

    bool teleportRay(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                     glm::vec3& newOrigin, glm::vec3& newDir) const;

    float ax, ay, az;   
    float bx, by, bz;   
    float yawA, pitchA, rollA;
    float yawB, pitchB, rollB;
    std::vector<float> vertices;   

    glm::vec3 portalNormal(float px, float py, float pz, bool sideB = false) const;

    bool pointInPortalPolygon(const glm::vec2& point) const;

    glm::mat4 getPortalTransform(float fx, float fy, float fz,
                                  float tx, float ty, float tz) const;

    struct SideState {
        glm::vec3 prevCamPos = glm::vec3(0.0f);
        float prevSignedDist = 0.0f;
        bool prevValid = false;
    };
    SideState sideA, sideB;
};
extern std::vector<Portal*> allPortals;
#endif
