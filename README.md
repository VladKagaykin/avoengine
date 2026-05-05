# AVOEngine – документация

**[Русский](#русский)** | **[English](#english)**

---

## <a name="русский"></a>Русская версия

**AVOEngine** — игровой движок на C++ (OpenGL + GLFW). Он предоставляет всё необходимое для создания 2D/3D-приложений: рисование примитивов, камеру, звук, загрузку текстур, псевдо-3D-спрайты, освещение, туман, тени, порталы, карты, а также дополнительные эффекты через расширение. Движок разделён на две части:

* **Ядро (avoengine)** – базовые возможности.
* **Расширение (avoextension)** – опциональные функции: текстовые эффекты, белый шум, обработка ввода, иконка окна, плоскость, сохранение/загрузка карт с порталами.

---

## Установка и сборка

### Зависимости (Debian/Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libstb-dev libassimp-dev
```

### Компиляция 
```bash
g++ -o output your_programm.cpp avoengine.cpp avoextension.cpp(if it need) -I./src -I./src/hwinfo -I/usr/include/stb -lGLEW -lglfw -lGLU -lGL -lSOIL -lglut -L. -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp -lassimp
```

При необходимости добавьте пути к `miniaudio.h` и `glm` через `-I`.

Запуск:
```bash
./output
```

---

## Ядро (avoengine)

### Глобальные переменные

Движок предоставляет следующие переменные, доступные после подключения `avoengine.h`:

| Имя | Тип | Описание |
|-----|-----|----------|
| `window_w`, `window_h` | `int` | Текущая ширина и высота окна (обновляются автоматически). |
| `screen_w`, `screen_h` | `int` | Размеры экрана (определяются один раз при запуске). |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | Информация о системе, заполняемая при инициализации. |
| `audio_engine` | `ma_engine` | Экземпляр звукового движка miniaudio. |
| `global_pitch` | `float` | Текущий pitch камеры (обновляется `move_camera`). |
| `global_yaw`   | `float` | Текущий yaw камеры (обновляется `move_camera`). |
| `global_ambient` | `float[3]` | Глобальный фоновый свет (RGB, по умолчанию `{0.05, 0.05, 0.05}`). |

### Инициализация и главный цикл

Движок использует **GLFW** для окна и событий. Шрифты для текста берутся из GLUT (требуется заголовок `GL/glut.h`). Окно движка недоступно напрямую, поэтому для главного цикла получите контекст:

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    setup_display(nullptr, nullptr, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
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

Если возникают проблемы с `glutBitmapCharacter`, добавьте `glutInit(&argc, argv);` перед `setup_display`.

### 2D-фигуры

Все фигуры рисуются в текущей 2D-проекции (после `setup_display` активен 2D-режим). Для переключения в 3D используйте `changeSize3D` или вызовите `move_camera`.

#### triangle()

```c
void triangle(float scale, float x, float y, double r, double g, double b, float rotate,
              const float* vertices, const char* texture_file = nullptr);
```

Рисует закрашенный треугольник.

- `scale` – масштаб.
- `x, y` – координаты центра.
- `r, g, b` – цвет (0..1).
- `rotate` – угол поворота в градусах (против часовой стрелки).
- `vertices` – массив из 6 чисел: координаты трёх точек относительно центра (x1,y1, x2,y2, x3,y3).
- `texture_file` – путь к текстуре (может быть `nullptr`).

#### square()

```c
void square(float local_size, float x, float y, double r, double g, double b, float rotate,
            const float* vertices, const char* texture_file = nullptr);
```

Рисует четырёхугольник.

- `vertices` – массив из 8 чисел: координаты четырёх точек (x1,y1 … x4,y4).

### Псевдо-3D сущности

Класс `pseudo_3d_entity` создаёт спрайты, всегда обращённые лицом к камере; текстура выбирается по направлению взгляда, что даёт иллюзию объёма.

```c
class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     std::vector<std::string> textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;

    void setCastShadow(bool enable);
    bool castsShadow() const;

    float getX() const, getY() const, getZ() const;
    float getGAngle() const, getVAngle() const, getRAngle() const;
    void setGAngle(float a), setVAngle(float a), setRAngle(float a);

    const std::vector<std::string>& getTextures() const;
    const std::vector<float>& getVertices() const;
    int getVAngles() const;
    float getRadius() const;
};
```

- `x, y, z` – позиция в мире.
- `g_angle, v_angle, r_angle` – углы поворота модели в градусах (горизонтальный, вертикальный, крен). Позволяют анимировать вращение.
- `textures` – список путей к текстурам для разных ракурсов (обычно упорядочены по горизонтали, затем по вертикали).
- `v_angles` – количество вертикальных слоёв (например, 1 для плоской модели).
- `vertices` – координаты 2D-квадрата (4 точки, 8 чисел).
- `setCastShadow(true)` – объект начинает отбрасывать тени от источников света (см. раздел «Освещение»).

**Пример:**
```cpp
std::vector<std::string> tex = { "sprite_0.png", "sprite_45.png", /*...*/ };
std::vector<float> verts = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };
pseudo_3d_entity enemy(10, 0, 10, 0, 0, 0, tex, 8, verts);
enemy.setCastShadow(true);

// в цикле отрисовки:
enemy.draw(camera.eye_x, camera.eye_y, camera.eye_z);
```

Для удобства сцены используйте `registerEntity(enemy)`; тогда `save_current_scene` сохранит все зарегистрированные объекты.

### 3D-объекты и камера

#### draw3DObject()

```c
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* texture_file,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords = {},
                  const std::vector<float>& normals = {});
```

Рисует 3D-объект с поддержкой нормалей и текстурных координат.

- `cx, cy, cz` – центр объекта.
- `r, g, b` – цвет (если текстура отсутствует).
- `texture_file` – путь к текстуре (`nullptr` – без текстуры).
- `vertices` – массив вершин (x, y, z).
- `indices` – индексы треугольников.
- `texcoords` – массив UV-координат (должен соответствовать количеству вершин).
- `normals` – массив нормалей (если передан, включается массив нормалей).

#### Камера

```c
struct CameraParams { ... };   // (внутренняя структура)
extern CameraParams camera;

void setup_camera(float fov, float eye_x, float eye_y, float eye_z, float pitch, float yaw, float roll = 0.0f);
void move_camera(float eye_x, float eye_y, float eye_z, float pitch, float yaw, float roll = 0.0f);
```

Обе функции автоматически корректируют вектор `up` при перевороте камеры (pitch > 90°) и обновляют позицию слушателя аудиодвижка.

#### Переключение между 2D и 3D

```c
void begin_2d(int w, int h);   // сохраняет матрицу, переключает в ортографическую 2D-проекцию
void end_2d();                 // восстанавливает 3D-проекцию и состояние
```

Используйте для рисования интерфейса поверх 3D-сцены.

### Шейдеры и освещение

Движок использует программируемые шейдеры (OpenGL 2.1 / GLSL 1.20) для расчёта освещения и теней. Встроенная программа `defaultLightingShader` включается функциями `enable_light()` / `disable_light()`.

#### Шейдеры

```c
GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();
```

#### Источники света

```c
class Light {
public:
    Light();
    void setPosition(float x, float y, float z);
    void setDirectionFromPitchYaw(float pitch_deg, float yaw_deg);
    void setColor(float r, float g, float b);
    void setIntensity(float i);
    void setRadius(float radius_deg);       // угол отсечки (180 = всенаправленный)
    void setAttenuation(float constant, float linear, float quadratic);
    void enable();
    void disable();
    bool isEnabled() const;
};
```

- После настройки вызовите `light.enable()`, чтобы добавить источник в сцену.
- Все активные источники автоматически передаются в шейдер при вызове `applyAllLights()` (вызывается внутри `enable_light()`).

**Глобальные функции:**
```c
void enable_light();               // включает шейдер, вычисляет и передаёт активные источники
void disable_light();              // отключает шейдер
void applyAllLights();             // принудительно обновляет uniform-переменные освещения
void set_ambient_light(float r, float g, float b);
void apply_material(float r, float g, float b, float alpha = 1.0f, float shininess = 32.0f);
```

- `apply_material` работает только при отключённом шейдере; при включённом шейдере используйте цвет вершины или текстуру.

#### Тени от псевдо-3D сущностей

Если для псевдо-3D сущности вызван `setCastShadow(true)`, она будет отбрасывать тени на другие объекты, использующие тот же шейдер. **Для активации теней необходимо каждый кадр вызывать `applyAllShadows()` после настройки освещения** (максимум 8 одновременно отбрасывающих тень объектов). Тени работают только для источников света с ограниченным радиусом (`setRadius`).

```cpp
enable_light();
// ... настройка камеры ...
applyAllLights();
applyAllShadows();   // <-- обязательно
// ... отрисовка объектов ...
```

### Туман

```c
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

- Туман работает только при активном шейдере (т.е. после `enable_light()`).

### Порталы

Класс `Portal` создаёт два связанных портала, которые визуализируют вид с противоположной стороны и могут телепортировать камеру. Для работы порталов требуется активный шейдер (вызовите `enable_light()`).

```c
class Portal {
public:
    Portal(float ax, float ay, float az,
           float bx, float by, float bz,
           const std::vector<float>& vertices,   // вершины полигона портала (x,y,z для каждой)
           float yawA=0, float pitchA=0, float rollA=0,
           float yawB=0, float pitchB=0, float rollB=0);

    void setSceneDrawCallback(std::function<void()> callback);
    void draw(int recursion_depth = 2);
    void checkTeleport();
};
```

- `ax,ay,az` и `bx,by,bz` – мировые координаты двух сторон портала.
- `vertices` – локальные координаты полигона (обычно прямоугольник в плоскости XY).
- `yawA, pitchA, rollA` / `yawB, pitchB, rollB` – ориентация каждой стороны.
- `setSceneDrawCallback` – задаёт функцию, рисующую всю сцену (она вызывается для рендера текстуры портала).
- `draw(recursion_depth)` – выполняет рендеринг портала.
- `checkTeleport()` – вызывайте каждый кадр; если камера пересекает полигон, она переносится к другой стороне с правильной ориентацией.

**Пример:**
```cpp
Portal portal(0,0,0, 10,0,0,
              { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 },
              0,0,0, 0,90,0);

portal.setSceneDrawCallback([&]{ /* код отрисовки всей сцены */ });

// в основном цикле:
portal.checkTeleport();
portal.draw(2);  // после отрисовки остальной сцены
```

### Карты (avomap)

Формат `.avomap` позволяет сохранять и загружать сцены: псевдо-3D сущности, источники света, порталы (только при использовании расширения), настройки тумана, камеры, панорамы и глобального освещения, а также произвольные пользовательские данные.

**Основные структуры (расширение):**

```cpp
struct MapEntity { ... };
struct MapData {
    std::vector<MapEntity> entities;
    std::vector<LightData> lights;
    std::vector<PortalData> portals;   // только с расширением
    bool fog_enabled;
    float fog_density, fog_color[3], fog_start, fog_end;
    float camera_eye[3], camera_pitch, camera_yaw;
    std::string panorama_path;
    float ambient[3];
    std::unordered_map<std::string, std::vector<uint8_t>> userData;
};
```

**Функции:**
```c
bool save_map(const char* filename, const MapData& map);
bool load_map(const char* filename, MapData& map);
MapEntity entityToMapData(const pseudo_3d_entity& ent);
pseudo_3d_entity* mapDataToEntity(const MapEntity& data);
MapData::PortalData portalToMapData(const Portal& p);
Portal* mapDataToPortal(const MapData::PortalData& data);
void save_current_scene(const char* filename);
void registerEntity(pseudo_3d_entity* e);
void unregisterEntity(pseudo_3d_entity* e);
```

- `save_current_scene` автоматически собирает все зарегистрированные сущности, порталы, освещение, туман и параметры камеры и сохраняет в файл.
- Пользовательские данные (`userData`) позволяют сохранить произвольные бинарные блоки под строковыми ключами.

### Звук

Звуковой движок miniaudio инициализируется автоматически при вызове `setup_display()`.

```c
void play_sound(const char* filename, float volume = 1.0f);               // однократный
void play_sound_loop(const char* filename, float volume = 1.0f);          // зацикленный
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f);
void stop_all_looping_sounds();                                            // остановить все зацикленные
```

### Текстуры

```c
GLuint loadTextureFromFile(const char* filename);       // загружает текстуру (кэширует)
void preloadTextures(const std::vector<std::string>& filenames); // многопоточная загрузка
void clearTextureCache();                               // удаляет все текстуры из памяти
```

### Панорама

```c
void set_panorama(const char* path);  // путь к текстуре неба (equirectangular)
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ); // рисует сферу позади всего
```

Панораму следует рисовать после очистки буфера глубины, например последней, или самой первой с `glClear(GL_DEPTH_BUFFER_BIT)`.

### HUD и отладка

```c
void draw_text(const char* text, float x, float y, void* font, float r, float g, float b, float a = 1.0f);
void draw_performance_hud(int win_w, int win_h);  // FPS, координаты, системная информация
```

Для шрифтов используйте константы GLUT, например `GLUT_BITMAP_HELVETICA_12`.

---

## Расширение (avoextension)

Расширение добавляет удобные обёртки ввода, систему тиков, текстовые эффекты, белый шум, простую плоскость и поддержку порталов в картах.

### Система тиков

Тик – логическая единица времени (50 мс), обновляемая в главном цикле.

```c
extern int tick;            // циклический счётчик 0..max_tick
extern const int max_tick;  // равно 20
extern int absolute_tick;   // абсолютный счётчик (никогда не сбрасывается)

void init_tick_system();    // сброс таймера (после setup_display)
void update_ticks();        // вызывать каждый кадр перед использованием tick/absolute_tick
```

**Пример:**
```cpp
init_tick_system();
while (!glfwWindowShouldClose(window)) {
    update_ticks();
    // использование absolute_tick...
}
```

### Текстовые эффекты

```c
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a,
                int ticks, bool loop = false);

void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a,
                       int ticks, bool loop = false);
```

- `delay_text` – появление текста по буквам за `ticks` тиков.
- `disappearing_text` – плавное затухание за `ticks` тиков.
- Если `loop == true`, анимация повторяется.
- **Важно:** предварительно вызывать `update_ticks()`.

### Белый шум

```c
void play_white_noise_3d(float x, float y, float z, float volume);
```

Создаёт пространственный источник белого шума (зациклен). Останавливается функцией `stop_all_looping_sounds()`.

### Обработка ввода

После создания окна настройте ввод:

```c
GLFWwindow* window = glfwGetCurrentContext();
init_keyboard(window);
init_mouse(window);
```

Становятся доступны глобальные переменные:

```c
extern bool keys[256];            // основные клавиши (нажата/отпущена)
extern bool skeys[512];           // специальные клавиши (стрелки, F1…)
extern std::map<std::string, bool> mouse; // "left", "right", "middle", "wheel_up", "wheel_down", "_click" варианты
extern int mouse_x, mouse_y;      // координаты мыши (в пикселях окна)
extern bool mouse_captured;       // флаг захвата мыши
```

- `_click` версии (`mouse["left_click"]`) устанавливаются в `true` только на один кадр после нажатия.
- В каждом кадре после обработки событий вызывайте `update_mouse()`.

**Захват мыши:**
```c
void set_mouse_capture(GLFWwindow* window, bool capture);
```

### Плоскость

```c
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```

Рисует прямоугольник по четырём вершинам (x,y,z).

### Иконка окна

```c
void set_icon(const char* path);   // загружает PNG и устанавливает как иконку GLFW-окна
```

---

## Сообщество

Discord-сервер: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## Лицензия

LGPL-3.0 (см. файл `LICENSE.md`).


## <a name="english"></a>English version

**AVOEngine** is a C++ game engine using OpenGL and GLFW. It provides everything needed for 2D/3D applications: primitive drawing, camera, audio, texture loading, pseudo‑3D sprites, lighting, fog, shadows, portals, maps, plus optional effects via the extension. The engine consists of two parts:

* **Core (avoengine)** – basic features.
* **Extension (avoextension)** – optional features: text effects, white noise, input handling, window icon, plane primitive, map save/load with portals.

---

## Installation & Build

### Dependencies (Debian/Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libstb-dev libassimp-dev
```

### Compilation

```bash
g++ -o output your_programm.cpp avoengine.cpp avoextension.cpp(if it need) -I./src -I./src/hwinfo -I/usr/include/stb -lGLEW -lglfw -lGLU -lGL -lSOIL -lglut -L. -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp -lassimp
```

Run:
```bash
./output
```

---

## Core (avoengine)

### Global Variables

| Name | Type | Description |
|------|------|-------------|
| `window_w`, `window_h` | `int` | Current window dimensions. |
| `screen_w`, `screen_h` | `int` | Screen dimensions (set at startup). |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | System information strings. |
| `audio_engine` | `ma_engine` | miniaudio engine instance. |
| `global_pitch`, `global_yaw` | `float` | Current camera pitch / yaw. |
| `global_ambient` | `float[3]` | Global ambient light colour. |

### Initialization and Main Loop

The engine uses GLFW for windowing and events. Glut is still required for bitmap fonts. Obtain the window context for the main loop:

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    setup_display(nullptr, nullptr, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
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

If text rendering fails, try adding `glutInit(&argc, argv);` before `setup_display`.

### 2D Shapes

All shapes are drawn in the current 2D projection (active after `setup_display`).

- `triangle(scale, x, y, r, g, b, rotate, vertices, tex)`
- `square(local_size, x, y, r, g, b, rotate, vertices, tex)`

### Pseudo‑3D Entities

The `pseudo_3d_entity` class draws a billboarded sprite that selects a texture based on view direction.

```c
pseudo_3d_entity(float x, float y, float z,
                 float g_angle, float v_angle, float r_angle,
                 std::vector<std::string> textures, int v_angles,
                 const std::vector<float>& vertices);
```

- `r_angle` – roll angle.
- `setCastShadow(true)` enables shadow casting from active lights.

### 3D Objects & Camera

```c
void draw3DObject(cx, cy, cz, r, g, b, tex, vertices, indices, texcoords, normals);
void setup_camera(fov, eye_x, eye_y, eye_z, pitch, yaw, roll = 0);
void move_camera(...);   // also updates audio listener
```

Switch between 2D and 3D with `begin_2d(w, h)` / `end_2d()`.

### Shaders & Lighting

Programmable pipeline with a built‑in phong‑style shader.

```c
GLuint createShaderProgram(vertexSrc, fragmentSrc);
void useShader(id);
void stopShader();
```

**Light sources:**
```c
Light light;
light.setPosition(0,5,0);
light.setColor(1,1,1);
light.setIntensity(2.0f);
light.setRadius(45.0f);   // spotlight cone
light.enable();
```

Global functions:
```c
void enable_light();   // activates shader and uploads enabled lights
void disable_light();
void set_ambient_light(r, g, b);
void apply_material(r, g, b, a, shininess);
```

#### Shadows

Entities with `setCastShadow(true)` cast shadows. **Call `applyAllShadows()` every frame** after updating lights (max 8 shadow casters):

```cpp
enable_light();
applyAllLights();
applyAllShadows();
// ... render scene ...
```

### Fog

```c
void enable_fog(density, r, g, b, start, end);
void disable_fog();
void set_fog_density(d);
void set_fog_range(start, end);
void set_fog_color(r, g, b);
```
Fog is only applied when the lighting shader is active.

### Portals

`Portal` creates two connected polygons that render the other side and can teleport the camera. The lighting shader must be active.

```c
Portal portal(ax,ay,az, bx,by,bz, vertices,
              yawA,pitchA,rollA, yawB,pitchB,rollB);
portal.setSceneDrawCallback(scene_render_function);
// each frame:
portal.checkTeleport();
portal.draw(recursion_depth);
```

### Maps (avomap)

Binary format for saving/loading scenes. With the extension, portals are also saved/loaded.

```c
struct MapEntity { ... };
struct MapData { ... };

bool save_map(filename, map);
bool load_map(filename, map);
void save_current_scene(filename);
void registerEntity(entity);
void unregisterEntity(entity);
// portal support (extension):
MapData::PortalData portalToMapData(const Portal& p);
Portal* mapDataToPortal(const MapData::PortalData& data);
```

### Audio

```c
play_sound(file, volume);
play_sound_loop(file, volume);
play_sound_3d(file, x, y, z, volume);
play_sound_3d_loop(file, x, y, z, volume);
stop_all_looping_sounds();
```

### Textures

```c
GLuint loadTextureFromFile(filename);  // cached
void preloadTextures(filenames);       // threaded
void clearTextureCache();
```

### Sky Panorama

```c
set_panorama("sky.jpg");
remove_panorama();
draw_panorama(camX, camY, camZ);
```

### HUD & Debug

```c
draw_text(text, x, y, GLUT_BITMAP_HELVETICA_12, r, g, b, a);
draw_performance_hud(win_w, win_h);
```

---

## Extension (avoextension)

### Tick System

50 ms logical tick.

```c
extern int tick, absolute_tick;
void init_tick_system();
void update_ticks();  // call every frame
```

### Text Effects

```c
void delay_text(text, x, y, font, r, g, b, a, ticks, loop);
void disappearing_text(text, x, y, font, r, g, b, a, ticks, loop);
```

### White Noise

```c
void play_white_noise_3d(x, y, z, volume);
```

### Input Handling

```c
init_keyboard(window);
init_mouse(window);
// use:
extern bool keys[256], skeys[512];
extern std::map<std::string, bool> mouse;
extern int mouse_x, mouse_y;
void update_mouse();
void set_mouse_capture(window, bool);
```

### Plane Primitive

```c
void plane(cx, cy, cz, r, g, b, tex, vertices);
```

### Window Icon

```c
void set_icon("icon.png");
```

---

## Community

Discord server: [https://discord.gg/QZe2s9r4u](https://discord.gg/QZe2s9r4u)

## License

LGPL-3.0 (see `LICENSE.md`).