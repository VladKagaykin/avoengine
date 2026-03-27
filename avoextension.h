#pragma once
#ifndef AVO_GLB_MODEL_H
#define AVO_GLB_MODEL_H
#include <vector>
#include <string>
#include <GL/glew.h>
#include <vector>
#include <GL/gl.h>
#include "avoengine.h"
#include "tiny_gltf.h"

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

void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a, int ticks, bool loop = false);
void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a, int ticks, bool loop = false);

void play_white_noise_3d(float x, float y, float z, float volume);
class glb_model {
private:
    // Компилятор теперь точно знает, что это за типы
    tinygltf::Model model;
    tinygltf::TinyGLTF loader; 
    bool loaded = false;
    float x, y, z, rx, ry, rz, scale;

    // ВАЖНО: Проверь, чтобы сигнатуры здесь и в .cpp совпадали ТОЧЬ-В-ТОЧЬ
    void drawNode(const tinygltf::Node& node); 
    void drawMesh(const tinygltf::Mesh& mesh);

public:
    glb_model(float x = 0, float y = 0, float z = 0);
    bool load(const std::string& filename);
    void setPos(float px, float py, float pz) { x = px; y = py; z = pz; }
    void setRot(float dx, float dy, float dz) { rx = dx; ry = dy; rz = dz; }
    void setScale(float s) { scale = s; }
    void draw();
};
#endif