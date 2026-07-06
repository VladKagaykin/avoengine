# AVOEngine – документация

**[Русский](#русский)** | **[English](#english)**

---

## <a name="русский"></a>Русская версия
(документация сделана глупым ии, если возникнут проблемы, то пишите в Discord сообщество)

**AVOEngine** — игровой движок на C++ (OpenGL + GLFW) с программным рендерингом через трассировку лучей для 3D-сцены. Движок предоставляет: рисование 2D/3D-примитивов, камеру, звук, загрузку текстур, псевдо-3D спрайты, освещение, туман, порталы, систему тиков, переключение сцен, а также дополнительные эффекты.

**Ключевая особенность:** весь 3D-рендеринг выполняется во фрагментном шейдере с помощью трассировки лучей по BVH-структуре. Это позволяет создавать порталы, искажающие плоскости и сложные эффекты без традиционного растеризатора.

---

## Установка и сборка

### Зависимости (Debian/(b)Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libassimp-dev
```

### Компиляция

```bash
g++ -o output your_program.cpp \
    avoengine.cpp \
    portals_rc.cpp \
    pseudo3dentity.cpp \
    light.cpp \
    ambient.cpp \
    audio_not_mini.cpp \
    textures.cpp \
    shaders.cpp \
    warp.cpp \
    baking_scene.cpp \
    ray_casting.cpp \
    tick_system.cpp \
    3d_primitives.cpp \
    2d_primitives.cpp \
    -I./src -lGLEW -lglfw -lGLU -lGL -lSOIL -fopenmp -lassimp
```

Запуск:
```bash
./output
```

---

## Ядро (avoengine)

### Глобальные переменные

| Имя | Тип | Описание |
|-----|-----|----------|
| `window_w`, `window_h` | `int` | Текущая ширина и высота окна |
| `screen_w`, `screen_h` | `int` | Размеры экрана |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | Информация о системе |
| `audio_engine` | `ma_engine` | Экземпляр звукового движка miniaudio |
| `camera` | `CameraParams` | Параметры камеры (eye, ctr, up, pitch, yaw, roll) |
| `global_ambient` | `float[3]` | Глобальный фоновый свет (по умолчанию `{0.05, 0.05, 0.05}`) |
| `allEntities` | `std::vector<pseudo_3d_entity*>` | Все зарегистрированные псевдо-3D сущности |
| `allPortals` | `std::vector<Portal*>` | Все созданные порталы |
| `Engine_settings` | `settings` | Структура с настройками движка (макс. кол-во источников света, отскоков, размер шага и т.д.) |

### Структуры

```cpp
struct CameraParams {
    float fov = 58.0f;
    float znear = 0.1f, zfar = 1000.0f;
    float eye_x, eye_y, eye_z;      // позиция
    float ctr_x, ctr_y, ctr_z;      // точка взгляда
    float up_x, up_y, up_z;         // вектор вверх
    float pitch, yaw, roll;         // углы в градусах
};

struct fog_params {
    bool enabled;
    float density;
    float color[3];
    float start, end;
};
extern fog_params fog;

struct settings {
    int MAX_LIGHTS = 16;
    int MAX_BOUNCES = 4;
    float MAX_DIST = camera.zfar;
    int MAX_TEXTURES = 2;
    int MAX_PORTALS = 8;
    int MAX_PORTAL_VERTS = 16;
    float SHADOW_BIAS = 0.001;
    float CAM_WARP_STRENGTH = 0.2;
    float SHADOW_WARP_STRENGTH = 0.2;
    float CAM_STEP_SIZE = 2;
    float SHADOW_STEP_SIZE = 2;
    int MAX_SHADOW_BOUNCES = 2;
    float RAY_MULTIPLY = 0.5;
    int TEXT_SAMPLE = 4;
};
extern settings Engine_settings;
```

### Инициализация и главный цикл

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    // Инициализация окна (2D-режим по умолчанию)
    setup_display(&argc, argv, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    
    // Настройка 3D-камеры
    setup_camera(60.0f, 0.0f, 1.7f, 5.0f, 0.0f, 0.0f);
    
    GLFWwindow* window = glfwGetCurrentContext();
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // ... отрисовка сцены ...
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    stop_all_looping_sounds();
    clearTextureCache();
    glfwTerminate();
    return 0;
}
```

**Важно:** Весь 3D-рендеринг выполняется внутри `flushDrawQueue()`, который автоматически вызывается при смене кадра.

---

### 2D-фигуры

Все фигуры накапливаются в очереди и отрисовываются при `flushDrawQueue()`.

#### square()

```cpp
void square(float local_size, float x, float y, 
            double r, double g, double b, float rotate,
            const float* vertices, const char* tex = nullptr, float alpha = 1.0f);
```

- `vertices` – массив из 8 чисел: координаты четырёх точек (x1,y1 … x4,y4)
- `tex` – путь к текстуре (опционально)

#### draw_line_2d()

```cpp
void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a, float thickness);
```

#### draw_text()

```cpp
void draw_text(const char* text, float x, float y, const char* fontPath, int fontSize,
               float r, float g, float b, float a = 1.0f);
```

- `fontPath` – путь к TrueType-шрифту (например, `"arial.ttf"`)
- `fontSize` – размер в пикселях

#### delay_text()

```cpp
void delay_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                float r, float g, float b, float a, int ticks, bool loop = false);
```
Появляется посимвольно за указанное количество тиков.

#### disappearing_text()

```cpp
void disappearing_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                       float r, float g, float b, float a, int ticks, bool loop = false);
```
Исчезает, уменьшая прозрачность, за указанное количество тиков.

#### draw_performance_hud()

```cpp
void draw_performance_hud(int win_w, int win_h, const char* font_path);
```
Показывает FPS, загрузку CPU/RAM/GPU, положение камеры.

---

### Псевдо-3D сущности (спрайты)

Класс `pseudo_3d_entity` создаёт спрайты, всегда обращённые к камере, с выбором текстуры по направлению взгляда.

```cpp
class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     const std::vector<std::string>& textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;
    bool isVisible(float cam_x, float cam_y, float cam_z) const;

    float getX() const, getY() const, getZ() const;
    float getGAngle() const, getVAngle() const, getRAngle() const;
    void setGAngle(float a), setVAngle(float a), setRAngle(float a);
    
    float getRadius() const;
    const std::vector<float>& getVertices() const;
    int getTextureIndex(float dir_x, float dir_y, float dir_z) const;
    GLuint getTextureID(int index) const;
};
```

- `x, y, z` – позиция в мире
- `g_angle, v_angle, r_angle` – углы поворота модели в градусах
- `textures` – список путей к текстурам (упорядочены по горизонтали, затем по вертикали)
- `v_angles` – количество вертикальных слоёв
- `vertices` – 2D-координаты квадрата (4 точки, 8 чисел)

**Пример:**
```cpp
std::vector<std::string> tex = { "sprite_0.png", "sprite_45.png", "sprite_90.png" };
std::vector<float> verts = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
pseudo_3d_entity enemy(10, 0, 10, 0, 0, 0, tex, 1, verts);

enemy.draw(camera.eye_x, camera.eye_y, camera.eye_z);
```

Используйте `registerEntity()` и `unregisterEntity()` для управления сценой.

---

### 3D-объекты

3D-объекты рисуются через трассировку лучей в шейдере.

```cpp
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f,
                  float alpha = 1.0f);
```

#### draw_line_3d()

```cpp
void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float a, float thickness,
                  int segments, float alpha = 1.0f);
```

Рисует 3D-линию с круглым сечением.

#### plane()

```cpp
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```
Рисует плоский прямоугольник (4 вершины, 12 чисел).

---

### Камера

```cpp
void setup_camera(float fov, float eye_x, float eye_y, float eye_z,
                  float pitch, float yaw, float roll = 0.0f);

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw, float roll = 0.0f);
```

Обе функции автоматически корректируют вектор `up` при перевороте камеры (pitch > 90°) и обновляют позицию слушателя аудиодвижка.

---

### Шейдеры

Движок использует программные шейдеры (OpenGL 2.1 / GLSL 1.20) с трассировкой лучей.

```cpp
GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();
```

**Встроенный шейдер** `defaultLightingShader` (доступен через `defaultLightingShader`) поддерживает:
- До 16 источников света
- Тени
- Туман
- Портал-рендеринг
- Искажающие плоскости (WarpPlane)
- BVH-ускорение
- Панорамы
- Настройки через `Engine_settings`

Для 2D-рендеринга используется отдельный шейдер `current2DShader`.

---

### Освещение

```cpp
class Light {
public:
    Light();
    void setPosition(float x, float y, float z);
    void setDirectionFromPitchYaw(float pitch_deg, float yaw_deg);
    void setColor(float r, float g, float b);
    void setIntensity(float intensity);
    void setRadius(float radius_deg);     // угол отсечки (180 = всенаправленный)
    void setAttenuation(float constant, float linear, float quadratic);
    void enable();
    void disable();
    bool isEnabled() const;
};
```

**Глобальные функции:**
```cpp
void set_ambient_light(float r, float g, float b);
void applyAllLights();    // обновляет uniform-переменные освещения
```

---

### Туман

```cpp
void enable_fog(float density, float r, float g, float b,
                float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

---

### Порталы

Порталы создают связанные области, визуализирующие вид с противоположной стороны.

```cpp
class Portal {
public:
    Portal(float ax, float ay, float az,
           float bx, float by, float bz,
           const std::vector<float>& vertices,
           float yawA = 0, float pitchA = 0, float rollA = 0,
           float yawB = 0, float pitchB = 0, float rollB = 0);
    
    void draw();
    void checkTeleport();
};
```

- `ax,ay,az` и `bx,by,bz` – мировые координаты двух сторон
- `vertices` – локальные координаты полигона (x,y,z для каждой вершины)
- `draw()` – добавляет портал в очередь отрисовки
- `checkTeleport()` – проверяет пересечение камеры (вызывайте каждый кадр)

**Пример:**
```cpp
std::vector<float> portalVerts = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
Portal portal(0, 0, 0, 10, 0, 0, portalVerts, 0, 0, 0, 0, 90, 0);

portal.checkTeleport();
portal.draw();
```

---

### Искажающая плоскость (WarpPlane)

Позволяет искажать лучи при прохождении через плоскость.

```cpp
class WarpPlane {
public:
    WarpPlane();
    void setDisplacementTexture(const char* filename);
    void setDisplacementFromData(int w, int h, const float* data);
    void enable();
    void disable();
    
    float originX, originY, originZ;
    float yaw, pitch, roll;
    float sizeU, sizeV;
};

extern WarpPlane* activeWarpPlane;
void set_active_warp_plane(WarpPlane* wp);
```

---

### Панорама (Skybox)

```cpp
struct sphere_panorama {
    bool enabled;
    GLuint texture;
    std::string path;
};
extern sphere_panorama sphere_sky;

void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);
```

---

### Звук

```cpp
void init_audio();    // вызывается внутри setup_display()
void play_sound(const char* filename, float volume = 1.0f);
void play_sound_loop(const char* filename, float volume = 1.0f);
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f);
void stop_all_looping_sounds();
```

---

### Текстуры

```cpp
GLuint loadTextureFromFile(const char* filename);   // кэшируется
void preloadTextures(const std::vector<std::string>& filenames);
void clearTextureCache();
```

---

### Ввод (клавиатура, мышь)

```cpp
void init_keyboard(GLFWwindow* window);
void init_mouse(GLFWwindow* window);
void update_mouse();          // сбрасывает флаги кликов и колёсика
void set_mouse_capture(GLFWwindow* window, bool capture);

extern bool keys[256];          // обычные клавиши
extern bool skeys[512];         // спецклавиши (GLFW_KEY_*)
extern std::map<std::string, bool> mouse;  // "left", "right", "middle", "left_click", "right_click", "middle_click", "wheel_up", "wheel_down"
extern int mouse_x, mouse_y;
extern bool mouse_captured;
```

---

### Система тиков

Тик = 50 мс (20 тиков в секунду).

```cpp
extern int tick;            // циклический 0..max_tick
extern const int max_tick;  // = 20
extern int absolute_tick;   // абсолютный счётчик

void init_tick_system();
void update_ticks();        // вызывать каждый кадр
```

---

### Управление сценами (Baking Scene)

Позволяет задать функцию, которая будет автоматически перестраивать BVH при каждом изменении сцены.

```cpp
using Function = void(*)();
extern bool is_scene_changed;
extern Function current_scene;

void fixed_scene(Function scene);   // задаёт сцену
void clean_scene();                 // очищает сцену
```

---

### HUD и отладка

```cpp
void draw_performance_hud(int win_w, int win_h, const char* font_path);
```

---

## Дополнительные утилиты

### 2D-примитивы

- `square()`, `draw_line_2d()`, `draw_text()`, `delay_text()`, `disappearing_text()` – описаны выше.

### 3D-примитивы

- `draw_line_3d()`, `draw3DObject()`, `plane()` – описаны выше.

### Иконка окна

```cpp
void set_icon(const char* path);
```

---

## Сообщество

Discord-сервер: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## Лицензия

LGPL-3.0


## <a name="english"></a>English Version
(The documentation was made by a stupid AI. If any issues arise, write to the Discord community)

**AVOEngine** is a C++ game engine using OpenGL and GLFW with software ray-traced rendering for 3D scenes. The engine provides: 2D/3D primitive drawing, camera, audio, texture loading, pseudo-3D sprites, lighting, fog, portals, tick system, scene switching, and additional utilities.

**Key feature:** All 3D rendering is performed in the fragment shader using ray marching against a BVH structure. This enables portals, warp planes, and complex effects without traditional rasterization.

---

## Installation & Build

### Dependencies (Debian/(b)Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libassimp-dev
```

### Compilation

```bash
g++ -o output your_program.cpp \
    avoengine.cpp \
    portals_rc.cpp \
    pseudo3dentity.cpp \
    light.cpp \
    ambient.cpp \
    audio_not_mini.cpp \
    textures.cpp \
    shaders.cpp \
    warp.cpp \
    baking_scene.cpp \
    ray_casting.cpp \
    tick_system.cpp \
    3d_primitives.cpp \
    2d_primitives.cpp \
    -I./src -lGLEW -lglfw -lGLU -lGL -lSOIL -fopenmp -lassimp
```

### Run

```bash
./output
```

---

## Core (avoengine)

### Global Variables

| Name | Type | Description |
|------|------|-------------|
| `window_w`, `window_h` | `int` | Current window dimensions |
| `screen_w`, `screen_h` | `int` | Screen dimensions |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | System information |
| `audio_engine` | `ma_engine` | miniaudio engine instance |
| `camera` | `CameraParams` | Camera parameters (eye, ctr, up, pitch, yaw, roll) |
| `global_ambient` | `float[3]` | Global ambient light (default `{0.05, 0.05, 0.05}`) |
| `allEntities` | `std::vector<pseudo_3d_entity*>` | All registered pseudo-3D entities |
| `allPortals` | `std::vector<Portal*>` | All created portals |
| `Engine_settings` | `settings` | Engine settings (max lights, bounces, step sizes, etc.) |

### Structures

```cpp
struct CameraParams {
    float fov = 58.0f;
    float znear = 0.1f, zfar = 1000.0f;
    float eye_x, eye_y, eye_z;      // position
    float ctr_x, ctr_y, ctr_z;      // look-at point
    float up_x, up_y, up_z;         // up vector
    float pitch, yaw, roll;         // angles in degrees
};

struct fog_params {
    bool enabled;
    float density;
    float color[3];
    float start, end;
};
extern fog_params fog;

struct settings {
    int MAX_LIGHTS = 16;
    int MAX_BOUNCES = 4;
    float MAX_DIST = camera.zfar;
    int MAX_TEXTURES = 2;
    int MAX_PORTALS = 8;
    int MAX_PORTAL_VERTS = 16;
    float SHADOW_BIAS = 0.001;
    float CAM_WARP_STRENGTH = 0.2;
    float SHADOW_WARP_STRENGTH = 0.2;
    float CAM_STEP_SIZE = 2;
    float SHADOW_STEP_SIZE = 2;
    int MAX_SHADOW_BOUNCES = 2;
    float RAY_MULTIPLY = 0.5;
    int TEXT_SAMPLE = 4;
};
extern settings Engine_settings;
```

### Initialization & Main Loop

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    setup_camera(60.0f, 0.0f, 1.7f, 5.0f, 0.0f, 0.0f);
    
    GLFWwindow* window = glfwGetCurrentContext();
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // ... draw scene ...
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    stop_all_looping_sounds();
    clearTextureCache();
    glfwTerminate();
    return 0;
}
```

---

### 2D Shapes

#### square()

```cpp
void square(float local_size, float x, float y, 
            double r, double g, double b, float rotate,
            const float* vertices, const char* tex = nullptr, float alpha = 1.0f);
```

#### draw_line_2d()

```cpp
void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a, float thickness);
```

#### draw_text()

```cpp
void draw_text(const char* text, float x, float y, const char* fontPath, int fontSize,
               float r, float g, float b, float a = 1.0f);
```

#### delay_text()

```cpp
void delay_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                float r, float g, float b, float a, int ticks, bool loop = false);
```

#### disappearing_text()

```cpp
void disappearing_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                       float r, float g, float b, float a, int ticks, bool loop = false);
```

#### draw_performance_hud()

```cpp
void draw_performance_hud(int win_w, int win_h, const char* font_path);
```

---

### Pseudo-3D Entities (Sprites)

```cpp
class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     const std::vector<std::string>& textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;
    bool isVisible(float cam_x, float cam_y, float cam_z) const;

    float getX() const, getY() const, getZ() const;
    float getGAngle() const, getVAngle() const, getRAngle() const;
    void setGAngle(float a), setVAngle(float a), setRAngle(float a);
    
    float getRadius() const;
    const std::vector<float>& getVertices() const;
    int getTextureIndex(float dir_x, float dir_y, float dir_z) const;
    GLuint getTextureID(int index) const;
};
```

**Example:**
```cpp
std::vector<std::string> tex = { "sprite_0.png", "sprite_45.png", "sprite_90.png" };
std::vector<float> verts = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
pseudo_3d_entity enemy(10, 0, 10, 0, 0, 0, tex, 1, verts);

enemy.draw(camera.eye_x, camera.eye_y, camera.eye_z);
```

Use `registerEntity()` and `unregisterEntity()` for scene management.

---

### 3D Objects

```cpp
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f,
                  float alpha = 1.0f);
```

#### draw_line_3d()

```cpp
void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float a, float thickness,
                  int segments, float alpha = 1.0f);
```

#### plane()

```cpp
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```

---

### Camera

```cpp
void setup_camera(float fov, float eye_x, float eye_y, float eye_z,
                  float pitch, float yaw, float roll = 0.0f);

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw, float roll = 0.0f);
```

---

### Shaders

```cpp
GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();
```

**Built-in shader** `defaultLightingShader` supports:
- Up to 16 lights
- Shadows
- Fog
- Portal rendering
- Warp planes
- BVH acceleration
- Panoramas
- Settings via `Engine_settings`

A separate 2D shader (`current2DShader`) is used for 2D rendering.

---

### Lighting

```cpp
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
    bool isEnabled() const;
};

void set_ambient_light(float r, float g, float b);
void applyAllLights();
```

---

### Fog

```cpp
void enable_fog(float density, float r, float g, float b,
                float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

---

### Portals

```cpp
class Portal {
public:
    Portal(float ax, float ay, float az,
           float bx, float by, float bz,
           const std::vector<float>& vertices,
           float yawA = 0, float pitchA = 0, float rollA = 0,
           float yawB = 0, float pitchB = 0, float rollB = 0);
    
    void draw();
    void checkTeleport();
};
```

**Example:**
```cpp
std::vector<float> portalVerts = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
Portal portal(0, 0, 0, 10, 0, 0, portalVerts, 0, 0, 0, 0, 90, 0);

portal.checkTeleport();
portal.draw();
```

---

### Warp Plane

```cpp
class WarpPlane {
public:
    WarpPlane();
    void setDisplacementTexture(const char* filename);
    void setDisplacementFromData(int w, int h, const float* data);
    void enable();
    void disable();
    
    float originX, originY, originZ;
    float yaw, pitch, roll;
    float sizeU, sizeV;
};

extern WarpPlane* activeWarpPlane;
void set_active_warp_plane(WarpPlane* wp);
```

---

### Panorama (Skybox)

```cpp
struct sphere_panorama {
    bool enabled;
    GLuint texture;
    std::string path;
};
extern sphere_panorama sphere_sky;

void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);
```

---

### Audio

```cpp
void init_audio();
void play_sound(const char* filename, float volume = 1.0f);
void play_sound_loop(const char* filename, float volume = 1.0f);
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f);
void stop_all_looping_sounds();
```

---

### Textures

```cpp
GLuint loadTextureFromFile(const char* filename);
void preloadTextures(const std::vector<std::string>& filenames);
void clearTextureCache();
```

---

### Input (Keyboard, Mouse)

```cpp
void init_keyboard(GLFWwindow* window);
void init_mouse(GLFWwindow* window);
void update_mouse();
void set_mouse_capture(GLFWwindow* window, bool capture);

extern bool keys[256];
extern bool skeys[512];
extern std::map<std::string, bool> mouse;  // "left", "right", "middle", "left_click", "right_click", "middle_click", "wheel_up", "wheel_down"
extern int mouse_x, mouse_y;
extern bool mouse_captured;
```

---

### Tick System

Tick = 50 ms (20 ticks per second).

```cpp
extern int tick;            // cyclic 0..max_tick
extern const int max_tick;  // = 20
extern int absolute_tick;

void init_tick_system();
void update_ticks();
```

---

### Scene Management (Baking Scene)

Allows setting a function that automatically rebuilds the BVH when the scene changes.

```cpp
using Function = void(*)();
extern bool is_scene_changed;
extern Function current_scene;

void fixed_scene(Function scene);
void clean_scene();
```

---

### HUD & Debug

```cpp
void draw_performance_hud(int win_w, int win_h, const char* font_path);
```

---

## Additional Utilities

### 2D Primitives

- `square()`, `draw_line_2d()`, `draw_text()`, `delay_text()`, `disappearing_text()` – described above.

### 3D Primitives

- `draw_line_3d()`, `draw3DObject()`, `plane()` – described above.

### Window Icon

```cpp
void set_icon(const char* path);
```

---

## Community

Discord server: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## License

LGPL-3.0