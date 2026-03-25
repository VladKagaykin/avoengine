# AVOEngine – документация

**[Русский](#русский)** | **[English](#english)**

---

## <a name="русский"></a>Русская версия

**AVOEngine** — игровой движок на C++ с OpenGL. Он предоставляет всё необходимое для создания 2D/3D-приложений: рисование примитивов, работу с камерой, звук, загрузку текстур, псевдо-3D-спрайты, карты, а также дополнительные эффекты через расширение. Движок разделён на две части:

* **Ядро (avoengine)** – базовые возможности.
* **Расширение (avoextension)** – опциональные функции: туман, текстовые эффекты (появление по буквам, затухание), белый шум, загрузка GLB-моделей, плоскость. Без расширения тоже можно создавать игры.

---

## Установка и сборка

### Зависимости (Debian/Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev
sudo apt install libglew-dev libsoil-dev

git clone https://github.com/lfreist/hwinfo.git
cd hwinfo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

### Компиляция проекта

**Только ядро:**
```bash
g++ -o output your_program.cpp avoengine.cpp \
    -I/usr/include/stb -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp
```

**С расширением:**
```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I/usr/include/stb -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp
```

Запуск:
```bash
./output
```

---

## Ядро (avoengine)

### Глобальные переменные

Движок предоставляет следующие переменные, доступные из любого файла, подключившего `avoengine.h`:

| Имя | Тип | Описание |
|-----|-----|----------|
| `window_w`, `window_h` | `int` | Текущая ширина и высота окна (обновляются в `reshape`). |
| `screen_w`, `screen_h` | `int` | Размеры экрана (определяются один раз при запуске). |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | Информация о системе, заполняемая при инициализации. |
| `audio_engine` | `ma_engine` | Экземпляр звукового движка miniaudio (инициализируется в `setup_display`). |

### 2D-фигуры

Все фигуры рисуются в текущей 2D-проекции (по умолчанию после `setup_display` установлена 2D). Для переключения в 3D используйте `changeSize3D`.

#### triangle()

```c
void triangle(float scale, float x, float y, double r, double g, double b, float rotate,
              const float* vertices, const char* texture_file = nullptr);
```

Рисует закрашенный треугольник.

- `scale` – масштаб (размер).
- `x, y` – координаты центра.
- `r, g, b` – цвет (0..1).
- `rotate` – угол поворота в градусах (против часовой стрелки).
- `vertices` – массив из 6 чисел: координаты трёх точек относительно центра (x1,y1, x2,y2, x3,y3).
- `texture_file` – путь к текстуре (если не указан, используется цвет).

**Пример:**
```c
triangle(1.0f, 0, 0, 1,0,0, 45,
         (float[]){-0.5f,-0.5f, 0.5f,-0.5f, 0,0.5f},
         "texture.png");
```

#### square()

```c
void square(float local_size, float x, float y, double r, double g, double b, float rotate,
            const float* vertices, const char* texture_file = nullptr);
```

Рисует четырёхугольник.

- `vertices` – массив из 8 чисел: координаты четырёх точек (x1,y1, x2,y2, x3,y3, x4,y4).

**Пример:**
```c
square(2.0f, 0, 0, 0,1,0, 0,
       (float[]){-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f},
       "grass.png");
```

#### circle()

```c
void circle(float scale, float x, float y, double r, double g, double b,
            float radius, float in_radius, float rotate,
            int slices, int loops, const char* texture_file = nullptr);
```

Рисует круг или кольцо.

- `radius` – внешний радиус.
- `in_radius` – внутренний радиус (0 – сплошной круг).
- `slices` – количество сегментов (чем больше, тем круглее).
- `loops` – количество концентрических колец (для плавного текстурирования).

**Пример:**
```c
circle(1.0f, 0, 0, 1,1,1, 1.0f, 0.2f, 0, 64, 1, "sun.png");
```

### Псевдо-3D сущности

Класс `pseudo_3d_entity` позволяет создавать объекты, которые всегда смотрят на камеру и выбирают текстуру в зависимости от угла обзора. Это даёт эффект объёмного объекта (как в классических играх, но без настоящей 3D-геометрии).

```c
class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z, float g_angle, float v_angle,
                     std::vector<const char*> textures, int v_angles, float* vertices);
    void draw(float cam_h, float cam_x, float cam_y, float cam_z) const;
    void setRadius(float r);
    float getRadius() const;
    void setGAngle(float a);
    void setVAngle(float a);
    float getGAngle() const;
    float getVAngle() const;
    float getX() const;
    float getY() const;
    float getZ() const;
};
```

- `x, y, z` – позиция в мире.
- `g_angle, v_angle` – горизонтальный и вертикальный углы поворота модели (градусы). Используются для анимации вращения.
- `textures` – список путей к текстурам для разных ракурсов. Обычно они упорядочены по горизонтали и вертикали.
- `v_angles` – количество вертикальных уровней (на сколько слоёв разбита текстура).
- `vertices` – координаты 2D-квадрата, на который накладывается текстура (4 точки, 8 чисел).

**Как подготовить текстуры:**  
Отрендерите модель с разных углов (например, 8 направлений по горизонтали и 4 по вертикали) и сохраните каждый кадр как текстуру. В игре объект будет автоматически выбирать нужную текстуру в зависимости от направления камеры.

**Пример:**
```c
std::vector<const char*> tex = {
    "sprite_0.png", "sprite_45.png", /* ... 64 текстур ... */
};
float verts[] = {-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f};
pseudo_3d_entity enemy(10, 0, 10, 0, 0, tex, 8, verts);

// В displayWrapper:
enemy.draw(cam_angle, cam_x, cam_y, cam_z);
```

### 3D-объекты и камера

#### draw3DObject()

```c
void draw3DObject(float cx, float cy, float cz, double r, double g, double b,
                  const char* texture_file,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords = {});
```

Рисует 3D-объект, заданный списком вершин и индексов треугольников.

- `cx, cy, cz` – центр объекта (к нему применяется трансляция).
- `r, g, b` – цвет (если текстура не указана).
- `texture_file` – путь к текстуре (nullptr – без текстуры).
- `vertices` – список вершин (x, y, z) в порядке обхода.
- `indices` – список индексов, образующих треугольники.
- `texcoords` – список UV-координат (должен соответствовать количеству вершин).

**Пример:**
```c
std::vector<float> verts = { -1,0,-1,  1,0,-1,  1,0,1,  -1,0,1 };
std::vector<int>   idx   = { 0,1,2,  0,2,3 };
std::vector<float> uv    = { 0,0,  1,0,  1,1,  0,1 };
draw3DObject(0, 0, 0, 1,1,1, "floor.png", verts, idx, uv);
```

#### Камера

```c
void setup_camera(float fov, float eye_x, float eye_y, float eye_z, float pitch, float yaw);
void move_camera(float eye_x, float eye_y, float eye_z, float pitch, float yaw);
void changeSize3D(int w, int h);   // вызывается автоматически в reshape
```

- `fov` – угол обзора (градусы).
- `eye_x, eye_y, eye_z` – позиция камеры.
- `pitch` – наклон вверх/вниз (градусы).
- `yaw` – поворот влево/вправо (градусы).

**Пример:**
```c
setup_camera(60.0f, 0, 1.7f, 3, 0, 0);
// позже:
move_camera(new_x, new_y, new_z, pitch, yaw);
```

#### Переключение между 2D и 3D

```c
void begin_2d(int w, int h);   // сохраняет матрицу и переключает в 2D-проекцию
void end_2d();                 // восстанавливает предыдущую матрицу
```

Используйте `begin_2d` / `end_2d` для рисования HUD поверх 3D-сцены.

### Звук

Звуковой движок (miniaudio) инициализируется автоматически при вызове `setup_display()`.

```c
void play_sound(const char* filename, float volume = 1.0f);               // однократный
void play_sound_loop(const char* filename, float volume = 1.0f);          // зацикленный
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);     // позиционный однократный
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f); // позиционный зацикленный
void stop_all_looping_sounds();                                            // остановить все зацикленные
```

**Пример:**
```c
play_sound_3d_loop("radio.wav", 10, 1.5f, 10, 0.8f);
```

### Карты (avomap)

Система карт позволяет хранить статичные 3D-объекты, псевдо-3D сущности и звуки в бинарном файле `.avm`.

**Структуры:**
```c
struct MapObject3D { ... };       // 3D-объект
struct MapEntityData { ... };     // псевдо-3D сущность
struct MapSound { ... };          // звук
struct GameMap {
    std::string name;
    std::vector<MapObject3D> objects;
    std::vector<MapEntityData> entities;
    std::vector<MapSound> sounds;
    std::vector<ProceduralLayer> layers;   // процедурные слои (не сохраняются)
};
```

**Функции:**
```c
bool save_map(const GameMap& map, const char* filename);
bool load_map(const char* filename, GameMap& out_map);
void draw_map(const GameMap& map, float cam_x, float cam_y, float cam_z);
void play_map_sounds(const GameMap& map);
```

**Процедурный слой** – функция, вызываемая каждый кадр для генерации бесконечного мира:
```c
void my_layer(float cam_x, float cam_y, float cam_z) {
    // рисование
}
map.layers.push_back(my_layer);
```

### Текстуры

```c
GLuint loadTextureFromFile(const char* filename);       // загружает текстуру (кэширует)
void clearTextureCache();                               // удаляет все текстуры из памяти
void preloadTextures(const std::vector<std::string>& filenames); // параллельная загрузка
```

### HUD

```c
void draw_text(const char* text, float x, float y, void* font, float r, float g, float b, float a = 1.0f);
void draw_performance_hud(int win_w, int win_h);
```

**Пример:**
```c
begin_2d(window_w, window_h);
draw_text("Score: 100", 10, 30, GLUT_BITMAP_HELVETICA_12, 1,1,1);
draw_performance_hud(window_w, window_h);
end_2d();
```

---

## Расширение (avoextension)

Подключается отдельным файлом `avoextension.cpp`. Добавляет эффекты, не входящие в ядро.

### Система тиков

Расширение вводит понятие «тик» – дискретную единицу времени, обновляемую в фоновом потоке. Это нужно для анимации текстовых эффектов и других плавных изменений.

**Глобальные переменные:**
```c
extern int tick;           // циклический счётчик (0..max_tick-1)
extern const int max_tick; // максимальное значение tick (по умолчанию 64)
extern int absolute_tick;  // абсолютный счётчик, никогда не сбрасывается
```

**Функции:**
```c
void init_tick_system();   // запускает внутренний таймер (должна вызываться один раз после setup_display)
void timer();              // обновляет tick и absolute_tick (вызывается автоматически)
void render_loop(int);     // вызывает glutPostRedisplay() (может использоваться для непрерывного рендера)
```

**Как это работает:**  
`init_tick_system()` устанавливает таймер GLUT, который каждые 16 мс вызывает `timer()`. Функция `timer()` увеличивает `tick` (циклически) и `absolute_tick`. Это позволяет создавать анимации, синхронизированные с частотой кадров.

### Текстовые эффекты

Оба эффекта используют глобальный счётчик `absolute_tick` для расчёта времени.

```c
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a,
                int ticks, bool loop = false);
```
- Текст появляется по буквам за `ticks` тиков. Если `loop == true`, анимация повторяется бесконечно.

```c
void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a,
                       int ticks, bool loop = false);
```
- Текст плавно исчезает за `ticks` тиков. Если `loop == true`, исчезновение повторяется.

**Важно:** Перед использованием этих функций **обязательно** вызовите `init_tick_system()`.

**Пример:**
```c
setup_display(&argc, argv, ...);
init_tick_system();   // обязательно!

delay_text("Loading...", -0.5f, 0.2f, GLUT_BITMAP_HELVETICA_18,
           1,1,1,1, 120, true);
disappearing_text("Fade out", -0.5f, -0.2f, GLUT_BITMAP_HELVETICA_18,
                  1,0.5,0.5,1, 80, true);
```

### Туман

```c
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

- `density` – плотность тумана (для экспоненциального режима; если используется линейный, не влияет).
- `start, end` – расстояние начала и конца тумана (для линейного режима).

**Пример:**
```c
enable_fog(0.05f, 0.3f, 0.4f, 0.5f);
```

### Белый шум

```c
void play_white_noise_3d(float x, float y, float z, float volume);
```
Создаёт источник белого шума в указанной точке. Шум зациклен, играет до вызова `stop_all_looping_sounds()`.

### GLB-модели

```c
int load_glb_model(const char* filename);                              // загрузить модель, вернуть ID
void draw_glb_model(int model_id, float x, float y, float z,
                    float scale = 1.0f, float rx = 0.0f, float ry = 0.0f, float rz = 0.0f);
void unload_glb_model(int model_id);                                   // выгрузить из памяти
void clear_embedded_texture_cache();                                   // очистить кэш текстур
```

Поддерживаются бинарные GLB-файлы (glTF). Текстуры извлекаются автоматически и кэшируются.

**Пример:**
```c
int model = load_glb_model("scene.glb");
draw_glb_model(model, 0, 0, 0, 0.5f, 0, 45, 0);
unload_glb_model(model);
clear_embedded_texture_cache();
```

### Плоскость

```c
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```
Рисует прямоугольник (плоскость) по четырём вершинам. Параметры аналогичны `draw3DObject`, но индексы и UV строятся автоматически.

---

## Пример главного цикла

```c
#include "avoengine.h"
#include "avoextension.h"   // если нужны эффекты

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 3D
    draw3DObject(...);
    enemy.draw(camera_angle, camera_x, camera_y, camera_z);

    // 2D HUD
    begin_2d(window_w, window_h);
    draw_text("Hello", 10, 30, GLUT_BITMAP_HELVETICA_12, 1,1,1);
    draw_performance_hud(window_w, window_h);
    end_2d();

    glutSwapBuffers();
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    setup_camera(60.0f, 0, 1.7f, 5, 0, 0);
    init_tick_system();   // обязательно для текстовых эффектов

    // ... инициализация ...

    glutDisplayFunc(displayWrapper);
    glutTimerFunc(0, timer, 0);   // если нужны тики
    glutMainLoop();

    stop_all_looping_sounds();
    clearTextureCache();
    return 0;
}
```

---

## Лицензия

LGPL-3.0 (см. файл `LICENSE.md`).

---

## <a name="english"></a>English version

**AVOEngine** is a C++ game engine with OpenGL. It provides everything needed to create 2D/3D applications: primitive drawing, camera, sound, texture loading, pseudo‑3D sprites, maps, and optional effects via the extension. The engine is split into two parts:

* **Core (avoengine)** – basic features.
* **Extension (avoextension)** – optional features: fog, text effects (typewriter and fade), white noise, GLB model loading, plane primitive. You can use the core alone for many games.

---

## Build Instructions

### Dependencies (Debian/Ubuntu)

```bash
sudo apt install build-essential
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev
sudo apt install libglew-dev libsoil-dev

git clone https://github.com/lfreist/hwinfo.git
cd hwinfo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

### Compilation

**Core only:**
```bash
g++ -o output your_program.cpp avoengine.cpp \
    -I/usr/include/stb -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp
```

**With extension:**
```bash
g++ -o output your_program.cpp avoengine.cpp avoextension.cpp \
    -I/usr/include/stb -lGLEW -lglut -lGLU -lGL -lSOIL \
    -L/usr/local/lib -lhwinfo_cpu -lhwinfo_gpu -lhwinfo_ram -fopenmp
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
| `window_w`, `window_h` | `int` | Current window size (updated in `reshape`). |
| `screen_w`, `screen_h` | `int` | Screen size (set once at startup). |
| `cpu_name`, `gpu_name`, `ram_v` | `std::string` | System information filled during initialization. |
| `audio_engine` | `ma_engine` | Instance of the miniaudio sound engine (initialized in `setup_display`). |

### 2D Shapes

All shapes are drawn in the current 2D projection (after `setup_display` it's 2D). Use `changeSize3D` to switch to 3D.

#### triangle()

```c
void triangle(float scale, float x, float y, double r, double g, double b, float rotate,
              const float* vertices, const char* texture_file = nullptr);
```

Draws a filled triangle.

- `scale` – size.
- `x, y` – center coordinates.
- `r, g, b` – color (0..1).
- `rotate` – rotation angle in degrees (counter‑clockwise).
- `vertices` – array of 6 numbers: coordinates of three points relative to the center.
- `texture_file` – texture path (optional).

**Example:**
```c
triangle(1.0f, 0, 0, 1,0,0, 45,
         (float[]){-0.5f,-0.5f, 0.5f,-0.5f, 0,0.5f},
         "texture.png");
```

#### square()

```c
void square(float local_size, float x, float y, double r, double g, double b, float rotate,
            const float* vertices, const char* texture_file = nullptr);
```

Draws a quadrilateral.

- `vertices` – array of 8 numbers: four point coordinates.

**Example:**
```c
square(2.0f, 0, 0, 0,1,0, 0,
       (float[]){-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f},
       "grass.png");
```

#### circle()

```c
void circle(float scale, float x, float y, double r, double g, double b,
            float radius, float in_radius, float rotate,
            int slices, int loops, const char* texture_file = nullptr);
```

Draws a circle or ring.

- `radius` – outer radius.
- `in_radius` – inner radius (0 for a solid circle).
- `slices` – number of segments (higher = smoother).
- `loops` – number of concentric rings (for smooth texturing).

**Example:**
```c
circle(1.0f, 0, 0, 1,1,1, 1.0f, 0.2f, 0, 64, 1, "sun.png");
```

### Pseudo‑3D Entities

The `pseudo_3d_entity` class creates sprites that always face the camera and pick a texture based on the viewing angle. This gives the illusion of a volumetric object (like in classic games, but without true 3D geometry).

```c
class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z, float g_angle, float v_angle,
                     std::vector<const char*> textures, int v_angles, float* vertices);
    void draw(float cam_h, float cam_x, float cam_y, float cam_z) const;
    void setRadius(float r);
    float getRadius() const;
    void setGAngle(float a);
    void setVAngle(float a);
    float getGAngle() const;
    float getVAngle() const;
    float getX() const;
    float getY() const;
    float getZ() const;
};
```

- `x, y, z` – world position.
- `g_angle, v_angle` – horizontal and vertical rotation angles (degrees) for animation.
- `textures` – list of texture paths for different angles.
- `v_angles` – number of vertical layers.
- `vertices` – 2D quad coordinates (4 points, 8 numbers).

**How to prepare textures:**  
Render the model from different angles (e.g., 8 horizontal directions and 4 vertical) and save each frame as a texture. In the game, the object automatically selects the correct texture based on camera direction.

**Example:**
```c
std::vector<const char*> tex = {
    "sprite_0.png", "sprite_45.png", /* ... 64 textures ... */
};
float verts[] = {-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f};
pseudo_3d_entity enemy(10, 0, 10, 0, 0, tex, 8, verts);

// In displayWrapper:
enemy.draw(cam_angle, cam_x, cam_y, cam_z);
```

### 3D Objects and Camera

#### draw3DObject()

```c
void draw3DObject(float cx, float cy, float cz, double r, double g, double b,
                  const char* texture_file,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords = {});
```

Draws a 3D object defined by vertices and triangle indices.

- `cx, cy, cz` – object center (translation applied).
- `r, g, b` – color (if no texture).
- `texture_file` – texture path (nullptr for no texture).
- `vertices` – list of vertices (x, y, z).
- `indices` – list of triangle indices.
- `texcoords` – list of UV coordinates (must match vertex count).

**Example:**
```c
std::vector<float> verts = { -1,0,-1,  1,0,-1,  1,0,1,  -1,0,1 };
std::vector<int>   idx   = { 0,1,2,  0,2,3 };
std::vector<float> uv    = { 0,0,  1,0,  1,1,  0,1 };
draw3DObject(0, 0, 0, 1,1,1, "floor.png", verts, idx, uv);
```

#### Camera

```c
void setup_camera(float fov, float eye_x, float eye_y, float eye_z, float pitch, float yaw);
void move_camera(float eye_x, float eye_y, float eye_z, float pitch, float yaw);
void changeSize3D(int w, int h);   // automatically called in reshape
```

- `fov` – field of view (degrees).
- `eye_x, eye_y, eye_z` – camera position.
- `pitch` – up/down tilt (degrees).
- `yaw` – left/right rotation (degrees).

**Example:**
```c
setup_camera(60.0f, 0, 1.7f, 3, 0, 0);
// later:
move_camera(new_x, new_y, new_z, pitch, yaw);
```

#### Switching Between 2D and 3D

```c
void begin_2d(int w, int h);   // saves matrix and switches to 2D projection
void end_2d();                 // restores previous matrix
```

Use `begin_2d` / `end_2d` to draw HUD on top of the 3D scene.

### Sound

The audio engine (miniaudio) is automatically initialized by `setup_display()`.

```c
void play_sound(const char* filename, float volume = 1.0f);               // one‑shot
void play_sound_loop(const char* filename, float volume = 1.0f);          // looping
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);     // positional one‑shot
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f); // positional looping
void stop_all_looping_sounds();                                            // stop all looping sounds
```

**Example:**
```c
play_sound_3d_loop("radio.wav", 10, 1.5f, 10, 0.8f);
```

### Maps (avomap)

The map system allows you to store static 3D objects, pseudo‑3D entities, and sounds in a binary `.avm` file.

**Structures:**
```c
struct MapObject3D { ... };       // 3D object
struct MapEntityData { ... };     // pseudo‑3D entity
struct MapSound { ... };          // sound
struct GameMap {
    std::string name;
    std::vector<MapObject3D> objects;
    std::vector<MapEntityData> entities;
    std::vector<MapSound> sounds;
    std::vector<ProceduralLayer> layers;   // procedural layers (not saved)
};
```

**Functions:**
```c
bool save_map(const GameMap& map, const char* filename);
bool load_map(const char* filename, GameMap& out_map);
void draw_map(const GameMap& map, float cam_x, float cam_y, float cam_z);
void play_map_sounds(const GameMap& map);
```

**Procedural layer** – a function called every frame to generate an infinite world:
```c
void my_layer(float cam_x, float cam_y, float cam_z) {
    // drawing
}
map.layers.push_back(my_layer);
```

### Textures

```c
GLuint loadTextureFromFile(const char* filename);       // loads texture (cached)
void clearTextureCache();                               // removes all textures from memory
void preloadTextures(const std::vector<std::string>& filenames); // parallel loading
```

### HUD

```c
void draw_text(const char* text, float x, float y, void* font, float r, float g, float b, float a = 1.0f);
void draw_performance_hud(int win_w, int win_h);
```

**Example:**
```c
begin_2d(window_w, window_h);
draw_text("Score: 100", 10, 30, GLUT_BITMAP_HELVETICA_12, 1,1,1);
draw_performance_hud(window_w, window_h);
end_2d();
```

---

## Extension (avoextension)

Include `avoextension.cpp` to add these optional features.

### Tick System

The extension introduces a “tick” – a discrete time unit updated in the background. This is used for text effect animations and other smooth changes.

**Global variables:**
```c
extern int tick;           // cyclic counter (0..max_tick-1)
extern const int max_tick; // maximum tick value (default 64)
extern int absolute_tick;  // absolute counter, never resets
```

**Functions:**
```c
void init_tick_system();   // starts the internal timer (must be called once after setup_display)
void timer();              // updates tick and absolute_tick (called automatically)
void render_loop(int);     // calls glutPostRedisplay() (can be used for continuous rendering)
```

**How it works:**  
`init_tick_system()` sets up a GLUT timer that calls `timer()` every 16 ms. `timer()` increments `tick` (cyclic) and `absolute_tick`. This allows animations synchronized with the frame rate.

### Text Effects

Both effects use the global counter `absolute_tick` to calculate timing.

```c
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a,
                int ticks, bool loop = false);
```
- Characters appear one by one over `ticks` ticks. If `loop == true`, the animation repeats forever.

```c
void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a,
                       int ticks, bool loop = false);
```
- The text fades out over `ticks` ticks. If `loop == true`, the fade repeats.

**Important:** You **must** call `init_tick_system()` before using these functions.

**Example:**
```c
setup_display(&argc, argv, ...);
init_tick_system();   // required!

delay_text("Loading...", -0.5f, 0.2f, GLUT_BITMAP_HELVETICA_18,
           1,1,1,1, 120, true);
disappearing_text("Fade out", -0.5f, -0.2f, GLUT_BITMAP_HELVETICA_18,
                  1,0.5,0.5,1, 80, true);
```

### Fog

```c
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_density(float density);
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
```

- `density` – fog density (for exponential mode; ignored for linear).
- `start, end` – distance where fog begins and ends (for linear mode).

**Example:**
```c
enable_fog(0.05f, 0.3f, 0.4f, 0.5f);
```

### White Noise

```c
void play_white_noise_3d(float x, float y, float z, float volume);
```
Creates a white noise source at the specified point. The noise loops until `stop_all_looping_sounds()` is called.

### GLB Models

```c
int load_glb_model(const char* filename);                              // load model, return ID
void draw_glb_model(int model_id, float x, float y, float z,
                    float scale = 1.0f, float rx = 0.0f, float ry = 0.0f, float rz = 0.0f);
void unload_glb_model(int model_id);                                   // unload from memory
void clear_embedded_texture_cache();                                   // clear texture cache
```

Supports binary GLB files (glTF). Textures are extracted automatically and cached.

**Example:**
```c
int model = load_glb_model("scene.glb");
draw_glb_model(model, 0, 0, 0, 0.5f, 0, 45, 0);
unload_glb_model(model);
clear_embedded_texture_cache();
```

### Plane

```c
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices);
```
Draws a rectangle (plane) from four vertices. Parameters are similar to `draw3DObject`, but indices and UVs are generated automatically.

---

## Example Main Loop

```c
#include "avoengine.h"
#include "avoextension.h"   // if you need effects

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 3D
    draw3DObject(...);
    enemy.draw(camera_angle, camera_x, camera_y, camera_z);

    // 2D HUD
    begin_2d(window_w, window_h);
    draw_text("Hello", 10, 30, GLUT_BITMAP_HELVETICA_12, 1,1,1);
    draw_performance_hud(window_w, window_h);
    end_2d();

    glutSwapBuffers();
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.2f, 1.0f, "My Game", 800, 600);
    setup_camera(60.0f, 0, 1.7f, 5, 0, 0);
    init_tick_system();   // required for text effects

    // ... initialization ...

    glutDisplayFunc(displayWrapper);
    glutTimerFunc(0, timer, 0);   // optional if you need ticks
    glutMainLoop();

    stop_all_looping_sounds();
    clearTextureCache();
    return 0;
}
```

---

## License

LGPL-3.0 (see `LICENSE.md`).
