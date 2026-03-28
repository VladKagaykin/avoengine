#pragma once
#ifndef AVO_GLB_MODEL_H
#define AVO_GLB_MODEL_H
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

extern int tick;
extern const int max_tick;
extern int absolute_tick;

void timer();
void render_loop(int);
void init_tick_system();

void plane(float cx, float cy, float cz, double r, double g, double b,
          const char* tex, const std::vector<float>& vertices);

void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);
void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a, int ticks, bool loop = false);
void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a, int ticks, bool loop = false);

void play_white_noise_3d(float x, float y, float z, float volume);
class glb_model {
public:
    float x, y, z;
    float rx, ry, rz; // Вращение по 3 осям
    float scale;
    bool loaded;

    glb_model(float _x = 0, float _y = 0, float _z = 0);
    bool load(const std::string& filename);
    void updateAnimation(float time, int animIndex = 0);
    void draw();
    
    void setRotation(float _rx, float _ry, float _rz) { rx = _rx; ry = _ry; rz = _rz; }
    void setScale(float s) { scale = s; }

private:
    Assimp::Importer importer;
    const aiScene* scene;
    std::map<int, GLuint> embedded_textures; 
    std::vector<std::vector<aiVector3D>> base_vertices; // "Сейф" для оригинальных вершин

    void loadTextures();
    void readNodeHierarchy(float time, aiAnimation* anim, aiNode* node, const aiMatrix4x4& parent, std::map<std::string, aiMatrix4x4>& out);
};
#endif