# AVOEngine – документация

**[Русский](#русский)** | **[English](#english)**

---

## <a name="русский"></a>Русская версия

**AVOEngine** — игровой движок на C++ с OpenGL и GLFW. Он предоставляет всё необходимое для создания 2D/3D-приложений: рисование примитивов, камеру, звук, загрузку текстур, псевдо-3D-спрайты, освещение, туман, тени, порталы, карты, а также дополнительные эффекты через расширение. Движок разделён на две части:

* **Ядро (avoengine)** – базовые возможности.
* **Расширение (avoextension)** – опциональные функции: текстовые эффекты, белый шум, обработка ввода, иконка окна, плоскость. Без расширения тоже можно создавать игры.

---

## Установка и сборка

### Зависимости (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev
sudo apt install libstb-dev   # для stb_image.h (обычно уже есть)
```

Для получения информации о системе (hwinfo):
```bash
git clone https://github.com/lfreist/hwinfo.git
cd hwinfo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

Библиотеку `miniaudio` достаточно скачать как заголовочный файл (`miniaudio.h`) и положить в папку с проектом или в системный include‑путь.

### Компиляция проекта

**Только ядро:**
```bash
g++ -o output your_program.cpp avoengine.cpp \
    -I/usr/include/stb -lglfw -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram \
    -fopenmp -lm -lpthread -ldl
```

**С расширением:**
```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I/usr/include/stb -lglfw -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram \
    -fopenmp -lm -lpthread -ldl
```

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

Движок использует **GLFW** для создания окна и обработки событий. Шрифты для текста по‑прежнему берутся из GLUT (заголовочный файл `glut.h`).

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    // Инициализация окна и аудио (argc/argv больше не используются)
    setup_display(nullptr, nullptr, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    setup_camera(60.0f, 0.0f, 1.7f, 5.0f, 0.0f, 0.0f);

    // Главный цикл GLFW
    while (!glfwWindowShouldEnd(g_window)) {   // g_window объявлен в avoengine
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ... отрисовка сцены ...

        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }

    stop_all_looping_sounds();
    clearTextureCache();
    glfwTerminate();
    return 0;
}
```

Функции `reshape` теперь обрабатываются через колбэк GLFW, установленный в `setup_display`.

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

#### light_square()

```c
void light_square(float local_size, float x, float y, double r, double g, double b, float rotate,
                  const float* vertices, const char* texture_file = nullptr);
```

Аналогична `square`, но использует разбивку на треугольники (веер из центра). Применяется внутри псевдо-3D сущностей для корректного освещения; при необходимости можно использовать и самостоятельно.

#### circle()

```c
void circle(float scale, float x, float y, double r, double g, double b,
            float radius, float in_radius, float rotate,
            int slices, int loops, const char* texture_file = nullptr);
```

Рисует круг или кольцо.

- `radius` – внешний радиус.
- `in_radius` – внутренний радиус (0 – сплошной круг).
- `slices` – количество сегментов.
- `loops` – количество концентрических колец.

### Псевдо-3D сущности

Класс `pseudo_3d_entity` создаёт спрайты, которые всегда обращены к камере, и выбирает текстуру в зависимости от угла обзора. Это даёт иллюзию объёмного объекта.

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
- `g_angle, v_angle, r_angle` – углы поворота модели (горизонтальный, вертикальный, крен) в градусах. Позволяют анимировать вращение.
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

void setup_camera(float fov, float eye_x, float eye_y, float eye_z, float pitch, float yaw);
void move_camera(float eye_x, float eye_y, float eye_z, float pitch, float yaw);
```

Обе функции автоматически корректируют вектор `up` при перевороте камеры (pitch > 90°), а также обновляют позицию слушателя аудиодвижка.

#### Переключение между 2D и 3D

```c
void begin_2d(int w, int h);   // сохраняет матрицу, переключает в ортографическую 2D-проекцию
void end_2d();                 // восстанавливает 3D-проекцию и состояние
```

Используйте для рисования интерфейса поверх 3D-сцены.

### Шейдеры и освещение

Движок использует программируемые шейдеры (OpenGL 2.1 / GLSL 1.20) для расчёта освещения и теней. По умолчанию создаётся встроенная шейдерная программа `defaultLightingShader`, которая включается функциями `enable_light()` / `disable_light()`.

#### Шейдеры

```c
GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();
```

- `createShaderProgram` компилирует вершинный и фрагментный шейдеры, возвращает ID программы.
- `useShader` активирует программу (автоматически запоминает текущую).
- `stopShader` возвращает фиксированный конвейер.

Необязательно вызывать вручную, если используете стандартную систему освещения.

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
void enable_light();               // включает шейдер, вычисляет и передаёт все активные источники
void disable_light();              // отключает шейдер
void applyAllLights();             // принудительно обновляет uniform-переменные света
void set_ambient_light(float r, float g, float b);
void apply_material(float r, float g, float b, float alpha = 1.0f, float shininess = 32.0f);
```

- `set_ambient_light` задаёт глобальную фоновую подсветку.
- `apply_material` управляет свойствами материала (через `glMaterial`), работает только при отключённом шейдере; при включённом шейдере используйте цвет вершины или текстуру.

#### Тени от псевдо-3D сущностей

Если для псевдо-3D сущности вызван `setCastShadow(true)`, она будет отбрасывать тени на другие объекты, использующие тот же шейдер. Тени рассчитываются в шейдере на основе текущего активного освещения. Максимальное количество одновременно рисующих тень объектов – 8.

### Туман

```c
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

- Туман работает только при активном шейдере (т.е. после `enable_light()`).
- `density` – плотность (используется для автоматического подбора start/end при вызове `set_fog_density`).
- `start, end` – расстояния начала и конца тумана в мировых единицах.

### Порталы

Класс `Portal` создаёт два связанных портала, которые визуализируют вид с противоположной стороны и могут телепортировать камеру при пересечении.

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
- `yawA, pitchA, rollA` / `yawB, pitchB, rollB` – ориентация каждой стороны в пространстве.
- `setSceneDrawCallback` – задаёт функцию, которая рисует всю сцену (она будет вызвана для рендера текстуры портала).
- `draw(recursion_depth)` – выполняет рендеринг портала с учётом рекурсии.
- `checkTeleport()` – нужно вызывать каждый кадр; если камера пересекает полигон, она мгновенно переносится к другой стороне с правильной ориентацией.

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

Формат `.avomap` позволяет сохранять и загружать сцены: псевдо-3D сущности, источники света, настройки тумана, камеры, панорамы и глобального освещения, а также произвольные пользовательские данные. Формат бинарный, с версионированием.

**Основные структуры:**

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
    std::vector<LightData> lights;   // LightData повторяет поля Light
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
void save_current_scene(const char* filename);
void registerEntity(pseudo_3d_entity* e);
void unregisterEntity(pseudo_3d_entity* e);
```

- `save_current_scene` автоматически собирает все зарегистрированные сущности, освещение, туман и параметры камеры и сохраняет в файл.
- `registerEntity` / `unregisterEntity` – для ведения глобального списка объектов.
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

Пространственные звуки используют позицию и направление камеры, обновляемые через `move_camera`.

### Текстуры

```c
GLuint loadTextureFromFile(const char* filename);       // загружает текстуру (кэширует)
void preloadTextures(const std::vector<std::string>& filenames); // многопоточная загрузка
void clearTextureCache();                               // удаляет все текстуры из памяти
```

### Панорама

```c
void set_panorama(const char* path);  // путь к файлу текстуры неба (equirectangular)
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ); // рисует сферу позади всего
```

Панорама отключает запись в буфер глубины и освещение, поэтому её следует рисовать последней (или первой с `glClear(GL_DEPTH_BUFFER_BIT)`).

### HUD и отладка

```c
void draw_text(const char* text, float x, float y, void* font, float r, float g, float b, float a = 1.0f);
void draw_performance_hud(int win_w, int win_h);  // FPS, координаты, системная информация
```

Для шрифтов используйте константы GLUT, например `GLUT_BITMAP_HELVETICA_12`.

---

## Расширение (avoextension)

Подключается файлами `avoextension.h`/`avoextension.cpp`. Добавляет удобные обёртки ввода, систему тиков, эффекты текста, белый шум и простую плоскость.

### Система тиков

Тик – логическая единица времени (50 мс), обновляемая в главном цикле. Используется для анимаций и текстовых эффектов.

```c
extern int tick;            // циклический счётчик 0..max_tick
extern const int max_tick;  // равно 20
extern int absolute_tick;   // абсолютный счётчик (никогда не сбрасывается)

void init_tick_system();    // сбрасывает таймер (вызвать один раз после setup_display)
void update_ticks();        // вызывать каждый кадр перед использованием tick/absolute_tick
```

**Пример:**
```cpp
init_tick_system();
while (!glfwWindowShouldClose(g_window)) {
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
- `disappearing_text` – плавное затухание текста за `ticks` тиков.
- Если `loop == true`, анимация повторяется.
- **Важно:** предварительно вызвать `update_ticks()`. Обе функции используют `absolute_tick`.

### Белый шум

```c
void play_white_noise_3d(float x, float y, float z, float volume);
```

Создаёт пространственный источник белого шума. Звук зациклен; останавливается функцией `stop_all_looping_sounds()`.

### Обработка ввода

Расширение автоматически настраивает колбэки GLFW для клавиатуры и мыши. Вызовы нужно сделать один раз после создания окна:

```c
GLFWwindow* win = ...; // можно получить через glfwGetCurrentContext()
init_keyboard(win);
init_mouse(win);
```

После этого становятся доступны глобальные переменные:

```c
extern bool keys[256];            // состояние основных клавиш (нажата/отпущена)
extern bool skeys[512];           // специальные клавиши (стрелки, F1…)
extern std::map<std::string, bool> mouse; // "left", "right", "middle", "wheel_up", "wheel_down", "_click" варианты
extern int mouse_x, mouse_y;      // координаты мыши (в пикселях окна)
extern bool mouse_captured;       // флаг захвата мыши
```

- `_click` версии (например, `mouse["left_click"]`) устанавливаются в `true` только на один кадр после нажатия.
- В каждом кадре после обработки событий необходимо вызывать `update_mouse()`, чтобы сбросить флаги `_click` и колёсико.

**Захват мыши:**
```c
void set_mouse_capture(GLFWwindow* window, bool capture);
```
Переключает режим невидимого курсора с бесконечным движением (подходит для FPS-камер).

### Плоскость

```c
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```

Рисует прямоугольник по четырём вершинам (x,y,z). Удобна для быстрых поверхностей пола/стен.

### Иконка окна

```c
void set_icon(const char* path);   // загружает PNG и устанавливает как иконку GLFW-окна
```

---

## Лицензия

LGPL-3.0 (см. файл `LICENSE.md`).


## <a name="english"></a>English version

**AVOEngine** is a C++ game engine using OpenGL and GLFW. It provides everything needed for 2D/3D applications: primitive drawing, camera, audio, texture loading, pseudo‑3D sprites, lighting, fog, shadows, portals, maps, plus optional effects via the extension. The engine consists of two parts:

* **Core (avoengine)** – basic features.
* **Extension (avoextension)** – optional features: text effects, white noise, input handling, window icon, plane primitive.

---

## Installation & Build

### Dependencies (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev libglfw3-dev
sudo apt install libglew-dev libsoil-dev libglm-dev libstb-dev
```

For system information (hwinfo):
```bash
git clone https://github.com/lfreist/hwinfo.git
cd hwinfo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

The `miniaudio` library is a single header; place `miniaudio.h` in your project or include directory.

### Compilation

**Core only:**
```bash
g++ -o output your_program.cpp avoengine.cpp \
    -I/usr/include/stb -lglfw -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram \
    -fopenmp -lm -lpthread -ldl
```

**With extension:**
```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I/usr/include/stb -lglfw -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram \
    -fopenmp -lm -lpthread -ldl
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

The engine uses **GLFW** for windowing and events. Glut is still required for bitmap fonts. A typical main function:

```cpp
#include "avoengine.h"

int main(int argc, char** argv) {
    setup_display(nullptr, nullptr, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    setup_camera(60.0f, 0.0f, 1.7f, 5.0f, 0.0f, 0.0f);

    while (!glfwWindowShouldClose(g_window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // ... draw scene ...
        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }

    stop_all_looping_sounds();
    clearTextureCache();
    glfwTerminate();
    return 0;
}
```

### 2D Shapes

All shapes are drawn in the current 2D projection.

- `triangle(scale, x, y, r, g, b, rotate, vertices, tex)`
- `square(local_size, x, y, r, g, b, rotate, vertices, tex)`
- `light_square(local_size, x, y, r, g, b, rotate, vertices, tex)` – triangle-fan version for correct lighting.
- `circle(scale, x, y, r, g, b, radius, in_radius, rotate, slices, loops, tex)`

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
void setup_camera(fov, eye_x, eye_y, eye_z, pitch, yaw);
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
void enable_light();   // activates shader and uploads all enabled lights
void disable_light();
void set_ambient_light(r, g, b);
void apply_material(r, g, b, a, shininess);
```

Shadows: pseudo‑3D entities with `setCastShadow(true)` cast shadows onto other objects (up to 8 simultaneously).

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

`Portal` creates two connected polygons that render a view of the other side and can teleport the camera.

```c
Portal portal(ax,ay,az, bx,by,bz, vertices,
              yawA,pitchA,rollA, yawB,pitchB,rollB);
portal.setSceneDrawCallback(scene_render_function);
// each frame:
portal.checkTeleport();
portal.draw(recursion_depth);
```

### Maps (avomap)

Binary file format for saving/loading whole scenes.

```c
struct MapEntity { ... };
struct MapData { ... };

bool save_map(filename, map);
bool load_map(filename, map);
void save_current_scene(filename);
void registerEntity(entity);
void unregisterEntity(entity);
```

Custom binary blobs can be stored in `MapData::userData`.

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
draw_panorama(camX, camY, camZ);   // draw after clearing depth
```

### HUD & Debug

```c
draw_text(text, x, y, GLUT_BITMAP_HELVETICA_12, r, g, b, a);
draw_performance_hud(win_w, win_h);
```

---

## Extension (avoextension)

### Tick System

A 50 ms logical tick, updated each frame.

```c
extern int tick, absolute_tick;
void init_tick_system();   // call once after setup_display
void update_ticks();       // call every frame
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
// then use:
extern bool keys[256], skeys[512];
extern std::map<std::string, bool> mouse;
extern int mouse_x, mouse_y;
void update_mouse();                 // reset per‑frame flags
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

## License

LGPL-3.0 (see `LICENSE.md`).
