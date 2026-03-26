#pragma once

#include <vector>
#include <string>
#include <GL/glew.h>

#include "avoengine.h"

// ---------------------------------------------------------------------
// Глобальные переменные и функции (из вашего кода, не трогаем)
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// КЛАСС GLBModel (инкапсулирует загрузку, отрисовку и анимации)
// ---------------------------------------------------------------------
class GLBModel {
public:
    GLBModel();
    ~GLBModel();

    // Загрузка из .glb (использует ту же логику, что и ваша рабочая load_glb_model)
    bool load(const char* filename);

    // Отрисовка с трансформациями (аналогично draw_glb_model)
    void draw(float x, float y, float z,
              float scale = 1.0f,
              float rx = 0.0f, float ry = 0.0f, float rz = 0.0f) const;

    // Освобождение ресурсов
    void unload();

    // Управление анимацией
    void playAnimation(int index, float speed = 1.0f, bool restart = true);
    void stopAnimation();
    void update(float deltaTime);
    int getAnimationCount() const;
    const char* getAnimationName(int index) const;

private:
    // Структуры, аналогичные вашим исходным (GLBPrimitive, GLBModel)
    struct Primitive {
        GLuint vao = 0;
        GLuint vbo = 0;        // позиции
        GLuint normVbo = 0;    // нормали
        GLuint texVbo = 0;     // UV
        GLuint ebo = 0;
        GLuint textureId = 0;
        int indexCount = 0;
        GLenum indexType = GL_UNSIGNED_INT; // 5123 = unsigned short, 5125 = unsigned int
        int nodeIdx = -1;
    };

    struct Node {
        std::vector<int> children;
        std::vector<int> primitives;
        float localMat[16];
        float worldMat[16];
        float trans[3];
        float rot[4];
        float scale[3];
        bool dirty = false;
    };

    struct Sampler {
        std::vector<float> input;
        std::vector<float> output;
        int stride = 0;
    };

    struct Channel {
        int nodeIdx = -1;
        int samplerIdx = -1;
        int path = 0; // 0=translation, 1=rotation, 2=scale
    };

    struct Animation {
        std::string name;
        std::vector<Sampler> samplers;
        std::vector<Channel> channels;
        float duration = 0.0f;
    };

    std::vector<Primitive> primitives;
    std::vector<Node> nodes;
    std::vector<GLuint> textures;
    std::vector<Animation> animations;

    int currentAnim = -1;
    float currentTime = 0.0f;
    float animSpeed = 1.0f;
    bool playing = false;

    // Вспомогательные методы
    void updateWorldMatrices(int nodeIdx, const float* parentMat);
    void drawNode(int nodeIdx) const;
    static void matIdentity(float* m);
    static void matMul(const float* a, const float* b, float* out);
    static void matTranslate(float* m, float x, float y, float z);
    static void matScale(float* m, float x, float y, float z);
    static void matFromQuat(float* m, const float* q);
    static void trsToMatrix(float* out, const float* t, const float* r, const float* s);
    static void lerpVec3(float t, const std::vector<float>& times,
                         const std::vector<float>& values, float* out);
    static void slerpQuat(float t, const std::vector<float>& times,
                          const std::vector<float>& values, float* out);
};

// ---------------------------------------------------------------------
// Функции-обёртки (для обратной совместимости)
// ---------------------------------------------------------------------
int load_glb_model(const char* filename);
void draw_glb_model(int model_id, float x, float y, float z,
                    float scale = 1.0f, float rx = 0.0f, float ry = 0.0f, float rz = 0.0f);
void unload_glb_model(int model_id);
void clear_embedded_texture_cache();

void play_model_animation(int model_id, int anim_index, float speed = 1.0f, bool restart = true);
void stop_model_animation(int model_id);
void update_model_animation(int model_id, float delta_time);
int get_model_animation_count(int model_id);
const char* get_model_animation_name(int model_id, int anim_index);