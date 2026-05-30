# AVOEngine – документация

**[Русский](#русский)** | **[English](#english)**

---

## <a name="русский"></a>Русская версия
(документация сделана глупым ии, если возникнут проблемы, то пишите в Discord сообщество)

**AVOEngine** — игровой движок на C++ (OpenGL + GLFW) с программным рендерингом через трассировку лучей для 3D-сцены. Движок предоставляет: рисование 2D/3D-примитивов, камеру, звук, загрузку текстур, псевдо-3D спрайты, освещение, туман, порталы, сохранение/загрузку карт, а также дополнительные эффекты через расширение.

**Ключевая особенность:** весь 3D-рендеринг выполняется в фрагментном шейдере с помощью трассировки лучей по BVH-структуре. Это позволяет создавать порталы, искажающие плоскости и сложные эффекты без традиционного растеризатора.

---

## Установка и сборка

### Зависимости (Debian/(b)Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libassimp-dev
```

### Компиляция

```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I./src -lGLEW -lglfw -lGLU -lGL -lSOIL -lglut -fopenmp -lassimp
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
void draw_text(const char* text, float x, float y, void* font,
               float r, float g, float b, float a = 1.0f);
```

Шрифты: `GLUT_BITMAP_HELVETICA_12`, `GLUT_BITMAP_TIMES_ROMAN_24` и т.д.

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

**Встроенный шейдер** `defaultLightingShader` поддерживает:
- До 16 источников света
- Тени
- Туман
- Портал-рендеринг
- Искажающие плоскости (WarpPlane)
- BVH-ускорение
- Панорамы

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

### HUD и отладка

```cpp
void draw_performance_hud(int win_w, int win_h);
```

---

## Расширение (avoextension)

Расширение добавляет систему тиков, текстовые эффекты, белый шум, ввод, 3D-примитивы и сохранение/загрузку карт.

### Система тиков

Тик = 50 мс (20 тиков в секунду).

```cpp
extern int tick;            // циклический 0..max_tick
extern const int max_tick;  // = 20
extern int absolute_tick;   // абсолютный счётчик

void init_tick_system();
void update_ticks();        // вызывать каждый кадр
```

### Текстовые эффекты

```cpp
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a, int ticks, bool loop = false);

void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a, int ticks, bool loop = false);
```

### Белый шум (3D)

```cpp
void play_white_noise_3d(float x, float y, float z, float volume);
```

### Обработка ввода

```cpp
void init_keyboard(GLFWwindow* window);
void init_mouse(GLFWwindow* window);
void update_mouse();
void set_mouse_capture(GLFWwindow* window, bool capture);

extern bool keys[256];
extern bool skeys[512];
extern std::map<std::string, bool> mouse;
extern int mouse_x, mouse_y;
extern bool mouse_captured;
```

### Простые 3D-примитивы

```cpp
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```

### Иконка окна

```cpp
void set_icon(const char* path);
```

### Карты (.avomap)

Бинарный формат для сохранения/загрузки сцен.

```cpp
struct MapEntity {
    float x, y, z;
    float g_angle, v_angle, r_angle;
    int v_angles;
    std::vector<std::string> textures;
    std::vector<float> vertices;
    bool castShadow;
};

struct MapData {
    std::vector<MapEntity> entities;
    struct LightData { ... };
    struct PortalData { ... };
    std::vector<LightData> lights;
    std::vector<PortalData> portals;
    
    bool fog_enabled;
    float fog_density, fog_color[3], fog_start, fog_end;
    float camera_eye[3], camera_pitch, camera_yaw;
    std::string panorama_path;
    float ambient[3];
    
    std::unordered_map<std::string, std::vector<uint8_t>> userData;
};

bool save_map(const char* filename, const MapData& map);
bool load_map(const char* filename, MapData& map);

MapEntity entityToMapData(const pseudo_3d_entity& ent);
pseudo_3d_entity* mapDataToEntity(const MapEntity& data);
MapData::PortalData portalToMapData(const Portal& p);
Portal* mapDataToPortal(const MapData::PortalData& data);

void registerEntity(pseudo_3d_entity* e);
void unregisterEntity(pseudo_3d_entity* e);
void save_current_scene(const char* filename);
```

---

## Сообщество

Discord-сервер: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## Лицензия

LGPL-3.0


## <a name="english"></a>English Version
(The documentation was made by a stupid AI. If any issues arise, write to the Discord community)

**AVOEngine** is a C++ game engine using OpenGL and GLFW with software ray-traced rendering for 3D scenes. The engine provides: 2D/3D primitive drawing, camera, audio, texture loading, pseudo-3D sprites, lighting, fog, portals, map saving/loading, and additional effects via the extension.

**Key feature:** All 3D rendering is performed in the fragment shader using ray marching against a BVH structure. This enables portals, warp planes, and complex effects without traditional rasterization.

---

## Installation & Build

### Dependencies (Debian/(b)Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libassimp-dev
```

### Compilation

```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I./src -lGLEW -lglfw -lGLU -lGL -lSOIL -lglut -fopenmp -lassimp
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
void draw_text(const char* text, float x, float y, void* font,
               float r, float g, float b, float a = 1.0f);
```

Fonts: `GLUT_BITMAP_HELVETICA_12`, `GLUT_BITMAP_TIMES_ROMAN_24`, etc.

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

### HUD & Debug

```cpp
void draw_performance_hud(int win_w, int win_h);
```

---

## Extension (avoextension)

### Tick System

Tick = 50 ms (20 ticks per second).

```cpp
extern int tick;            // cyclic 0..max_tick
extern const int max_tick;  // = 20
extern int absolute_tick;

void init_tick_system();
void update_ticks();
```

### Text Effects

```cpp
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a, int ticks, bool loop = false);

void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a, int ticks, bool loop = false);
```

### White Noise (3D)

```cpp
void play_white_noise_3d(float x, float y, float z, float volume);
```

### Input Handling

```cpp
void init_keyboard(GLFWwindow* window);
void init_mouse(GLFWwindow* window);
void update_mouse();
void set_mouse_capture(GLFWwindow* window, bool capture);

extern bool keys[256];
extern bool skeys[512];
extern std::map<std::string, bool> mouse;
extern int mouse_x, mouse_y;
extern bool mouse_captured;
```

### Simple 3D Primitives

```cpp
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```

### Window Icon

```cpp
void set_icon(const char* path);
```

### Maps (.avomap)

```cpp
struct MapEntity {
    float x, y, z;
    float g_angle, v_angle, r_angle;
    int v_angles;
    std::vector<std::string> textures;
    std::vector<float> vertices;
    bool castShadow;
};

struct MapData {
    std::vector<MapEntity> entities;
    struct LightData { ... };
    struct PortalData { ... };
    std::vector<LightData> lights;
    std::vector<PortalData> portals;
    
    bool fog_enabled;
    float fog_density, fog_color[3], fog_start, fog_end;
    float camera_eye[3], camera_pitch, camera_yaw;
    std::string panorama_path;
    float ambient[3];
    
    std::unordered_map<std::string, std::vector<uint8_t>> userData;
};

bool save_map(const char* filename, const MapData& map);
bool load_map(const char* filename, MapData& map);

MapEntity entityToMapData(const pseudo_3d_entity& ent);
pseudo_3d_entity* mapDataToEntity(const MapEntity& data);
MapData::PortalData portalToMapData(const Portal& p);
Portal* mapDataToPortal(const MapData::PortalData& data);

void registerEntity(pseudo_3d_entity* e);
void unregisterEntity(pseudo_3d_entity* e);
void save_current_scene(const char* filename);
```

---

## Community

Discord server: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## License

LGPL-3.0
```