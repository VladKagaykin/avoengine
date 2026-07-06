#ifndef AVOEXTENSION_H
#define AVOEXTENSION_H
#include <vector>
#include <string>
#include <vector>
#include "avoengine.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>
#include <unordered_map>

#include "portals_rc.h"
#include "pseudo3dentity.h"

struct GLFWwindow;

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

void delay_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                float r, float g, float b, float a, int ticks, bool loop=0);
void disappearing_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                       float r, float g, float b, float a, int ticks, bool loop=0);

void play_white_noise_3d(float x, float y, float z, float volume);

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

    struct PortalData {
        float ax, ay, az, bx, by, bz;
        float yawA, pitchA, rollA;
        float yawB, pitchB, rollB;
        std::vector<float> vertices;
    };
    std::vector<PortalData> portals;

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

// bool save_map(const char* filename, const MapData& map);
// bool load_map(const char* filename, MapData& map);

// MapEntity entityToMapData(const pseudo_3d_entity& ent);
// pseudo_3d_entity* mapDataToEntity(const MapEntity& data);

// MapData::LightData lightToMapData(const Light& light);
// void mapDataToLight(const MapData::LightData& data, Light& out);

// void registerEntity(pseudo_3d_entity* e);
// void unregisterEntity(pseudo_3d_entity* e);
// void save_current_scene(const char* filename);

// extern std::vector<pseudo_3d_entity*> allEntities;

// MapData::PortalData portalToMapData(const Portal& p);
// Portal* mapDataToPortal(const MapData::PortalData& data);

#endif