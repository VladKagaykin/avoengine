#pragma once // Должно быть на самой первой строке

#include <vector>
#include <string>

#include "avoengine.h"

extern int tick;
extern const int max_tick;
extern int absolute_tick;

void timer();
void render_loop(int);
void init_tick_system();

void plane(float cx, float cy, float cz, double r, double g, double b, const char* tex, const std::vector<float>& vertices);

void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);

void delay_text(const char* text, float x, float y, void* font, float r, float g, float b, float a, int ticks, bool loop = false);
void disappearing_text(const char* text, float x, float y, void* font, float r, float g, float b, float a, int ticks, bool loop = false);

void play_white_noise_3d(float x, float y, float z, float volume);

// ========== Импорт GLB-моделей (с оптимизацией) ==========

struct GLBPrimitive {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint texVbo;
    GLuint normVbo; 
    int indexCount;
    GLenum indexType; // <--- ДОБАВИТЬ ЭТО
    GLuint textureId;
};

struct GLBModel {
    std::string name;
    std::vector<GLBPrimitive> primitives;
};

// Загружает модель из файла .glb, создаёт VAO/VBO/EBO и кэширует текстуры.
// Возвращает ID модели (индекс в глобальном списке) или -1 при ошибке.
int load_glb_model(const char* filename);

// Рисует модель с заданными трансформациями.
void draw_glb_model(int model_id, float x, float y, float z, float scale=1.0f, float rx=0.0f, float ry=0.0f, float rz=0.0f);

// Удаляет VAO/VBO/EBO модели из памяти. Текстуры остаются в кэше.
void unload_glb_model(int model_id);

// Очищает кэш текстур, загруженных из .glb (вызывать при завершении).
void clear_embedded_texture_cache();