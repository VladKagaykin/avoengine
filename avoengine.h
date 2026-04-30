#ifndef AVOENGINE_H
#define AVOENGINE_H
#include <GL/glew.h>
#include <GL/glut.h>
#include <string>
#include <vector>
#include <cmath>
#include <unordered_map>
#include "miniaudio.h"

struct GLFWwindow;

extern int window_w;
extern int window_h;
extern int screen_w;
extern int screen_h;

extern std::string cpu_name;
extern std::string gpu_name;
extern std::string ram_v;

//              шейдеры
// тип данных для программы шейдера
extern GLuint currentShaderProg; 

// функции
GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();

GLuint loadTextureFromFile(const char* filename);
void clearTextureCache();
void preloadTextures(const std::vector<std::string>& filenames);

extern ma_engine audio_engine;

struct CameraParams{
    float fov=58.0f;
    float znear=0.1f;
    float zfar=1000.0f;
    float eye_x=0,eye_y=0,eye_z=0;
    float ctr_x=0,ctr_y=0,ctr_z=1;
    float up_x=0,up_y=1,up_z=0;
    float dir_x=0,dir_y=0,dir_z=1;
};

extern CameraParams camera;

void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad);

void triangle(float scale,float cx,float cy,double r,double g,double b,float rotate,const float* vertices,const char* texture_file=nullptr);

void square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* tex);
void light_square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* texture_file=nullptr);

void circle(float scale,float cx,float cy,double r,double g,double b,float radius,float in_radius,float rotate,int slices,int loops,const char* texture_file=nullptr);

void draw_text(const char* text,float x,float y,void* font,float r,float g,float b,float a=1.0f);

class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     std::vector<std::string> textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;

    void setCastShadow(bool enable);
    bool castsShadow() const { return _castsShadow; }

    // Возвращает радиус (вычисляется автоматически)
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
    const std::vector<std::string>& getTextures() const { return textures; }
    const std::vector<float>& getVertices() const { return vertices_; }
    int getVAngles() const { return v_angles; }
private:
    void computeRadius();
    int getTextureIndex(float dir_x, float dir_y, float dir_z) const;
    bool isVisible(float cam_x, float cam_y, float cam_z) const;

    float x, y, z;
    float g_angle, v_angle, r_angle;
    float radius;

    std::vector<std::string> textures;
    int v_angles;
    std::vector<float> vertices_;

    mutable int cachedTexIdx = -1;
    mutable float cachedDirX = 1e9f, cachedDirY = 1e9f, cachedDirZ = 1e9f;

    bool _castsShadow = false;
};

extern std::vector<pseudo_3d_entity*> shadowCasters;

void applyAllShadows();

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h);
void changeSize3D(int w,int h);
void changeSize2D(int w,int h);
void framebuffer_size_callback(GLFWwindow* /*window*/, int w, int h);
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw);
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw);
void draw3DObject(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices,const std::vector<int>& indices,const std::vector<float>& texcoords={},const std::vector<float>& normals={});
void enable_light();
void disable_light();

#define MAX_LIGHTS 16

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

    void applyToShader(int index, GLuint program) const;

    // Поля теперь публичные
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

// Глобальная функция для передачи всех активных источников в текущий шейдер
void applyAllLights();

// ID дефолтного шейдера (будет создан при инициализации)
extern GLuint defaultLightingShader;

void set_ambient_light(float r, float g, float b);
void apply_material(float r, float g, float b, float alpha = 1.0f, float shininess = 32.0f);
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);
void begin_2d(int w,int h);
void end_2d();
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

struct MapEntity {
    float x, y, z;
    float g_angle, v_angle, r_angle;
    int v_angles;
    std::vector<std::string> textures;
    std::vector<float> vertices;
    bool castShadow = false;
};

struct MapData {
    std::vector<MapEntity> entities;

    struct LightData {
        bool enabled = false;
        float pos[3] = {0,0,0};
        float dir[3] = {0,0,-1};
        float color[3] = {1,1,1};
        float intensity = 1.0f;
        float cutoff = 180.0f;
        float constAtt = 1.0f;
        float linearAtt = 0.0f;
        float quadAtt = 0.0f;
    };
    std::vector<LightData> lights;

    bool fog_enabled = false;
    float fog_density = 0.05f;
    float fog_color[3] = {0.7f, 0.8f, 0.9f};
    float fog_start = 5.0f;
    float fog_end = 30.0f;

    float camera_eye[3] = {0,0,0};
    float camera_pitch = 0.0f;
    float camera_yaw = 0.0f;

    std::string panorama_path;
    float ambient[3] = {0.05f, 0.05f, 0.05f};

    std::unordered_map<std::string, std::vector<uint8_t>> userData;
};

bool save_map(const char* filename, const MapData& map);
bool load_map(const char* filename, MapData& map);

MapEntity entityToMapData(const pseudo_3d_entity& ent);
pseudo_3d_entity* mapDataToEntity(const MapEntity& data);

MapData::LightData lightToMapData(const Light& light);
void mapDataToLight(const MapData::LightData& data, Light& out);

struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
extern fog_params fog;

extern float global_pitch;
extern float global_yaw;
extern float global_ambient[3];

extern std::vector<pseudo_3d_entity*> allEntities;
void registerEntity(pseudo_3d_entity* e);
void unregisterEntity(pseudo_3d_entity* e);
void save_current_scene(const char* filename);


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

    void setSceneDrawCallback(std::function<void()> cb);
    void draw(int recursion_depth = 2);

    float ax, ay, az;
    float bx, by, bz;
    float yawA, pitchA, rollA;
    float yawB, pitchB, rollB;
    std::vector<float> vertices;

private:
    struct FBO {
        GLuint fbo = 0;
        GLuint colorTex = 0;
        GLuint depthTex = 0;
        int w = 0, h = 0;
    };

    void initFBOs(int w, int h);
    void destroyFBOs();
    void resizeFBOs(int w, int h);

    glm::mat4 getPortalTransform(float fx, float fy, float fz,
                                  float tx, float ty, float tz) const;
    glm::vec3 portalNormal(float px, float py, float pz, bool sideB = false) const;
    bool isFrontFacing(float px, float py, float pz,
                       float cam_x, float cam_y, float cam_z) const;

    void renderThroughPortal(float src_x, float src_y, float src_z,
                              float dst_x, float dst_y, float dst_z,
                              int depth, bool drawingA);
    void drawPortalSurface(float px, float py, float pz, GLuint tex, bool sideB);

    FBO fboA, fboB;
    std::function<void()> sceneDraw;
};

#endif