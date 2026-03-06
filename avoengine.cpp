//              звук
// указываем что здесь реализация библиотеки, т.к. miniaudio это только заголовочный файл и даём понять что
// это главная программа
#define MINIAUDIO_IMPLEMENTATION
// указываем что пользуемся только указанным api для воспроизведения звука(pulseaudio и прочая шняга), если они не указаны,
// то программа компилируется для всех api
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
// указываем что api некий alsa(встроенный в линукс)
#define MA_ENABLE_ALSA
// импортируем сам miniaudio
#include "miniaudio.h"

//              движок
// указываем заголовочный файл движка
#include "avoengine.h"

//              графика
// вспомогательные утилиты для opengl(матрицы, проекции и прочие нежности для немощей)
#include <GL/glu.h>
// основная библиотека opengl
#include <GL/glut.h>
// библиотека для импорта текстур
#include <SOIL/SOIL.h>

//              утилиты
// библиотека для работы со временем для замеров производительности
#include <chrono>
// математика(п, синусы, косинусы)
#include <cmath>
// удобная запись в переменные через printf и прочую хрень
#include <cstdio>
// взаимодействия с консолью
#include <iostream>
// таблица номер-значение, поможет для текстур
#include <unordered_map>
// нелоховские массивы
#include <vector>

//              объявления
// использование пространства имён std 😲
using namespace std;
// переменные для хранения в них размеров окна и экрана
int window_w = 0, window_h = 0, screen_w = 0, screen_h = 0;
// создание таблицы текстур и их id 
//       имя файла текстуры  его id        
static unordered_map<string, GLuint> textureCache;
// храним id последней загруженной текстуры
static GLuint boundTextureID = 0;

// инициализация звукового движка(ma_engine тип данных, а audio_engine название)
static ma_engine audio_engine;
// вектор в котором хранятся звуки, которые играют на постоянке
static vector<ma_sound*> loopingSounds;

// структура для зранения параметров камеры
struct CameraParams {
    float fov   = 60.0f;
    float znear = 0.1f;
    float zfar  = 1000.0f;
    float eye_x = 0, eye_y = 0, eye_z = 0;
    float ctr_x = 0, ctr_y = 0, ctr_z = 1;
    float up_x  = 0, up_y  = 1, up_z  = 0;
};
// инициализация камеры
static CameraParams camera;

//              утилиты для камеры
// вычисляем то, куда смотрит центр камеры
static inline void lookAtForward(float eye_x,float eye_y,float eye_z,float pitch_deg,float yaw_deg,float& cx,float& cy,float& cz){
    // радианы наклона по вертикали
    const float p=pitch_deg*float(M_PI)/180.0f;
    // радианы наклона по горизонтали
    const float y=yaw_deg*float(M_PI)/180.0f;
    // считаем проекцию почти по теореме пифагора
    cx = eye_x + cosf(p) * sinf(y);
    cy = eye_y + sinf(p);
    cz = eye_z + cosf(p) * cosf(y);
}
// вычисляем тоже самое, только не то куда смотрит камера, а то, куда она не смотрит(не знаю как объяснить, короче,
// куда смотрела бы камера если бы её развернуло на 180)
static inline void lookAtBackward(float eye_x,float eye_y,float eye_z,float pitch_deg,float yaw_deg,float& cx,float& cy,float& cz,float& dx,float& dy,float& dz){
    const float p=pitch_deg*float(M_PI)/180.0f;
    const float y=yaw_deg*float(M_PI)/180.0f;
    dx=-cosf(p)*sinf(y);
    dy=-sinf(p);
    dz=-cosf(p)*cosf(y);
    cx=eye_x+dx;
    cy=eye_y+dy;
    cz=eye_z+dz;
}

// проверка на то, привязана ли эта текстура или нет
static inline void bindTexture(GLuint id){
    if (id!=boundTextureID){
        glBindTexture(GL_TEXTURE_2D,id);
        boundTextureID=id;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Текстуры
// ═══════════════════════════════════════════════════════════════════════════

GLuint loadTextureFromFile(const char* filename)
{
    auto it = textureCache.find(filename);
    if (it != textureCache.end())
        return it->second;

    int w, h;
    unsigned char* img = SOIL_load_image(filename, &w, &h, nullptr, SOIL_LOAD_RGBA);
    if (!img) {
        cerr << "Cannot load texture: " << filename
             << " (" << SOIL_last_result() << ")\n";
        return textureCache[filename] = 0;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    boundTextureID = id;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img);

    SOIL_free_image_data(img);
    return textureCache[filename] = id;
}

void clearTextureCache()
{
    for (auto& [name, id] : textureCache)
        if (id) glDeleteTextures(1, &id);
    textureCache.clear();
    boundTextureID = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  2-D утилиты
// ═══════════════════════════════════════════════════════════════════════════

void rotatePoint(float& x, float& y, float cx, float cy, float angle_rad)
{
    const float tx = x - cx, ty = y - cy;
    const float c  = cosf(angle_rad), s = sinf(angle_rad);
    x = cx + tx * c - ty * s;
    y = cy + tx * s + ty * c;
}

// ── Общая логика enable/bind для текстурных примитивов ──────────────────────
static void enableTex(const char* file)
{
    if (!file) { glDisable(GL_TEXTURE_2D); return; }
    GLuint id = loadTextureFromFile(file);
    if (id) { glEnable(GL_TEXTURE_2D); bindTexture(id); }
    else      glDisable(GL_TEXTURE_2D);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Примитивы
// ═══════════════════════════════════════════════════════════════════════════

void triangle(float scale, float cx, float cy,
              double r, double g, double b,
              float rotate, const float* vertices, const char* tex)
{
    glColor3f(float(r), float(g), float(b));
    enableTex(tex);

    const float ar  = rotate * float(M_PI) / -180.0f;
    const float tc[6] = {0,1, 1,1, 0,0};

    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 3; ++i) {
        float px = vertices[i*2], py = vertices[i*2+1];
        rotatePoint(px, py, 0, 0, ar);
        if (tex) glTexCoord2f(tc[i*2], tc[i*2+1]);
        glVertex2f(cx + px * scale, cy + py * scale);
    }
    glEnd();

    if (tex) glDisable(GL_TEXTURE_2D);
}

void square(float local_size, float x, float y,
            double r, double g, double b,
            float rotate, const float* vertices, const char* tex)
{
    glColor3f(float(r), float(g), float(b));
    enableTex(tex);

    const float ar  = rotate * float(M_PI) / -180.0f;
    const float tc[8] = {0,1, 1,1, 1,0, 0,0};

    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        float px = vertices[i*2], py = vertices[i*2+1];
        rotatePoint(px, py, 0, 0, ar);
        if (tex) glTexCoord2f(tc[i*2], tc[i*2+1]);
        glVertex2f(x + px * local_size, y + py * local_size);
    }
    glEnd();

    if (tex) glDisable(GL_TEXTURE_2D);
}

void circle(float scale, float cx, float cy,
            double r, double g, double b,
            float radius, float in_radius,
            float rotate, int slices, int loops, const char* tex)
{
    glColor3f(float(r), float(g), float(b));
    enableTex(tex);

    glPushMatrix();
    glTranslatef(cx, cy, 0);
    glRotatef(-rotate, 0, 0, 1);
    glScalef(scale, scale, 1);

    // Один quadric на весь lifetime программы — не аллоцируем/удаляем каждый вызов
    static GLUquadric* q = nullptr;
    if (!q) {
        q = gluNewQuadric();
        gluQuadricTexture(q, GL_TRUE);
        gluQuadricDrawStyle(q, GLU_FILL);
    }

    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1, -1, 1);
    gluDisk(q, in_radius, radius, slices, loops);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glPopMatrix();

    if (tex) glDisable(GL_TEXTURE_2D);
}

void draw_text(const char* text, float x, float y,
               void* font, float r, float g, float b)
{
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (const char* c = text; *c; ++c)
        glutBitmapCharacter(font, *c);
}

// ═══════════════════════════════════════════════════════════════════════════
//  pseudo_3d_entity
// ═══════════════════════════════════════════════════════════════════════════

pseudo_3d_entity::pseudo_3d_entity(float x, float y, float z,
                                   float g_angle, float v_angle,
                                   vector<const char*> textures,
                                   int v_angles, float* vertices)
    : x(x), y(y), z(z), g_angle(g_angle), v_angle(v_angle),
      textures(std::move(textures)), v_angles(v_angles), vertices(vertices)
{}

// ── Frustum culling ─────────────────────────────────────────────────────────
// Проверяем пересечение сферы (позиция entity + radius) с усечённой пирамидой.
// Используем параметры из глобального camera — без обращения к GL.
bool pseudo_3d_entity::isVisible(float cam_x, float cam_y, float cam_z) const
{
    const float dx = x - cam_x, dy = y - cam_y, dz = z - cam_z;

    // Единичный вектор взгляда камеры (ctr - eye уже единичный, см. lookAt*)
    const float fx = camera.ctr_x - camera.eye_x;
    const float fy = camera.ctr_y - camera.eye_y;
    const float fz = camera.ctr_z - camera.eye_z;

    // Глубина вдоль оси взгляда
    const float depth = dx*fx + dy*fy + dz*fz;

    // Near / far planes
    if (depth + radius < camera.znear) return false;
    if (depth - radius > camera.zfar)  return false;

    // Угловая проверка: считаем диагональный half-FOV и добавляем угловой slack
    // на радиус entity. Это консервативная оценка — лучше лишний раз нарисовать,
    // чем пропустить видимую сущность.
    const float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 1e-4f) return true;

    const float aspect    = (window_h > 0) ? float(window_w) / float(window_h) : 1.0f;
    const float half_v    = camera.fov * 0.5f * float(M_PI) / 180.0f;
    const float half_h    = atanf(tanf(half_v) * aspect);
    const float half_diag = sqrtf(half_h*half_h + half_v*half_v);
    const float slack     = asinf(fminf(1.0f, radius / dist));

    return (depth / dist) >= cosf(half_diag + slack);
}

// ── Вычисление индекса текстуры (с кэшированием) ────────────────────────────
// Пересчёт нужен только если углы камеры изменились относительно прошлого кадра.
int pseudo_3d_entity::getTextureIndex(float cam_h, float cam_v) const
{
    // Порог 0.5° — ниже этого изменение текстуры незаметно
    if (fabsf(cam_h - cachedCamH) < 0.5f && fabsf(cam_v - cachedCamV) < 0.5f)
        return cachedTexIdx;

    cachedCamH = cam_h;
    cachedCamV = cam_v;

    if (textures.empty()) { cachedTexIdx = -1; return -1; }
    const int total   = int(textures.size());
    const int h_count = total / v_angles;
    if (h_count <= 0)  { cachedTexIdx = -1; return -1; }

    const float ch     = cam_h * float(M_PI) / 180.0f;
    const float cv     = cam_v * float(M_PI) / 180.0f;
    const float cos_cv = cosf(cv);
    const float dir_x  = cos_cv * sinf(ch);
    const float dir_y  = sinf(cv);
    const float dir_z  = cos_cv * cosf(ch);

    const float ga     = g_angle * float(M_PI) / 180.0f;
    const float va     = v_angle * float(M_PI) / 180.0f;

    const float cos_ga = cosf(-ga), sin_ga = sinf(-ga);
    const float lx     =  dir_x * cos_ga + dir_z * sin_ga;
    const float ly     =  dir_y;
    const float lz     = -dir_x * sin_ga + dir_z * cos_ga;

    const float cos_va = cosf(va), sin_va = sinf(va);
    const float fx     =  lx;
    const float fy     =  ly * cos_va + lz * sin_va;
    const float fz     = -ly * sin_va + lz * cos_va;

    const float local_v = atan2f(fy, sqrtf(fx*fx + fz*fz)) * 180.0f / float(M_PI);
    const float local_h = (fabsf(local_v) > 88.0f)
                          ? 0.0f
                          : atan2f(fx, fz) * 180.0f / float(M_PI);

    const float v_rel   = fmaxf(0.0f, fminf(180.0f, local_v + 90.0f));
    const int   v_index = int(fminf(v_rel / (180.0f / v_angles), float(v_angles - 1)));

    const float step_h  = 360.0f / h_count;
    const int   h_index = int((fmodf(local_h + 360.0f, 360.0f) + step_h * 0.5f)
                              / step_h) % h_count;

    cachedTexIdx = v_index * h_count + h_index;
    if (cachedTexIdx >= total) cachedTexIdx = total - 1;
    return cachedTexIdx;
}

void pseudo_3d_entity::draw(float cam_h, float cam_x, float cam_y, float cam_z) const
{
    // ── Frustum culling: самая дешёвая проверка — первой ────────────────────
    if (!isVisible(cam_x, cam_y, cam_z)) return;

    const float dx   = cam_x - x, dy = cam_y - y, dz = cam_z - z;
    const float dist = sqrtf(dx*dx + dy*dy + dz*dz);

    const float pitch = atan2f(dy, sqrtf(dx*dx + dz*dz)) * 180.0f / float(M_PI);

    // Индекс текстуры из кэша
    const int   tidx  = getTextureIndex(cam_h, pitch);
    const char* tex   = (tidx >= 0) ? textures[tidx] : nullptr;

    // Единичный вектор "от entity к камере"
    const float fx = (dist > 1e-4f) ? dx / dist : 0.0f;
    const float fy = (dist > 1e-4f) ? dy / dist : 1.0f;
    const float fz = (dist > 1e-4f) ? dz / dist : 0.0f;

    // Ортонормированный базис billboard
    float wx = 0, wy = 1, wz = 0;
    if (fabsf(fy) > 0.999f) { wx = 0; wy = 0; wz = 1; }

    float rx = wy*fz - wz*fy, ry = wz*fx - wx*fz, rz = wx*fy - wy*fx;
    const float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rlen > 1e-4f) { rx /= rlen; ry /= rlen; rz /= rlen; }

    const float ux = fy*rz - fz*ry, uy = fz*rx - fx*rz, uz = fx*ry - fy*rx;

    const float mat[16] = {
        rx, ry, rz, 0,
        ux, uy, uz, 0,
        fx, fy, fz, 0,
         0,  0,  0, 1
    };

    const float ga = g_angle * float(M_PI) / 180.0f;
    const float va = v_angle * float(M_PI) / 180.0f;

    float eu_x = -sinf(ga) * sinf(va);
    float eu_y = -cosf(va);
    float eu_z = -cosf(ga) * sinf(va);

    float dot  = eu_x*fx + eu_y*fy + eu_z*fz;
    float pu_x = eu_x - dot*fx, pu_y = eu_y - dot*fy, pu_z = eu_z - dot*fz;
    float plen = sqrtf(pu_x*pu_x + pu_y*pu_y + pu_z*pu_z);

    if (plen < 0.01f) {
        const float ef_x = cosf(va) * sinf(ga);
        const float ef_y = -sinf(va);
        const float ef_z = cosf(va) * cosf(ga);
        const float d2   = ef_x*fx + ef_y*fy + ef_z*fz;
        pu_x = ef_x - d2*fx; pu_y = ef_y - d2*fy; pu_z = ef_z - d2*fz;
    }

    const float roll = atan2f(
        -(pu_x*rx + pu_y*ry + pu_z*rz),
          pu_x*ux + pu_y*uy + pu_z*uz
    ) * 180.0f / float(M_PI);

    const bool mirror = (tidx == 0);

    glPushMatrix();
    glTranslatef(x, y, z);
    glMultMatrixf(mat);
    glRotatef(roll + 180.0f, 0, 0, 1);
    square(1.0f, 0, 0, 1, 1, 1, mirror ? -180.0f : 0.0f, vertices, tex);
    glPopMatrix();
}

// ═══════════════════════════════════════════════════════════════════════════
//  OpenGL / окно
// ═══════════════════════════════════════════════════════════════════════════

void changeSize3D(int w, int h)
{
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(camera.fov, float(w) / float(h), camera.znear, camera.zfar);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(camera.eye_x, camera.eye_y, camera.eye_z,
              camera.ctr_x, camera.ctr_y, camera.ctr_z,
              camera.up_x,  camera.up_y,  camera.up_z);
}

void changeSize2D(int w, int h)
{
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float ratio = float(w) / float(h);
    if (w <= h) glOrtho(-1, 1, -1/ratio, 1/ratio, 1, -1);
    else        glOrtho(-ratio, ratio, -1, 1, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    window_w = w;
    window_h = h;
}

void setup_display(int* argc, char** argv,
                   float r, float g, float b, float a,
                   const char* name, int w, int h)
{
    init_audio();

    glutInit(argc, argv);
    screen_w = glutGet(GLUT_SCREEN_WIDTH);
    screen_h = glutGet(GLUT_SCREEN_HEIGHT);
    window_w = w;
    window_h = h;

    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(screen_w / 4, screen_h / 8);
    glutInitWindowSize(w, h);
    glutCreateWindow(name);
    glutReshapeFunc(changeSize2D);

    glClearColor(r, g, b, a);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void setup_camera(float fov,
                  float eye_x, float eye_y, float eye_z,
                  float pitch, float yaw)
{
    camera.fov   = fov;
    camera.znear = 0.1f;
    camera.zfar  = 1000.0f;
    camera.eye_x = eye_x; camera.eye_y = eye_y; camera.eye_z = eye_z;
    camera.up_x  = 0; camera.up_y = 1; camera.up_z = 0;

    lookAtForward(eye_x, eye_y, eye_z, pitch, yaw,
                  camera.ctr_x, camera.ctr_y, camera.ctr_z);

    // Проекция с корректным aspect (используем текущий размер окна)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = (window_h > 0) ? float(window_w) / float(window_h) : 1.0f;
    gluPerspective(fov, aspect, camera.znear, camera.zfar);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x, eye_y, eye_z,
              camera.ctr_x, camera.ctr_y, camera.ctr_z,
              0, 1, 0);

    ma_engine_listener_set_position(&audio_engine, 0, eye_x, eye_y, eye_z);
}

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw)
{
    camera.eye_x = eye_x; camera.eye_y = eye_y; camera.eye_z = eye_z;

    // Оригинальная конвенция: look-at строится через ОТРИЦАТЕЛЬНОЕ направление.
    // display() компенсирует это, передавая -cam_pitch.
    // Менять знак здесь нельзя — иначе сломается W/S и горизонт.
    float dx, dy, dz;
    lookAtBackward(eye_x, eye_y, eye_z, pitch, yaw,
                   camera.ctr_x, camera.ctr_y, camera.ctr_z,
                   dx, dy, dz);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x, eye_y, eye_z,
              camera.ctr_x, camera.ctr_y, camera.ctr_z,
              0, 1, 0);

    ma_engine_listener_set_position (&audio_engine, 0, eye_x, eye_y, eye_z);
    ma_engine_listener_set_direction(&audio_engine, 0, dx, dy, dz);
}

// ═══════════════════════════════════════════════════════════════════════════
//  3-D объекты
// ═══════════════════════════════════════════════════════════════════════════

void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const vector<float>& vertices,
                  const vector<int>&   indices,
                  const vector<float>& texcoords)
{
    glColor3f(float(r), float(g), float(b));

    if (tex) {
        GLuint id = loadTextureFromFile(tex);
        if (id) { glEnable(GL_TEXTURE_2D); bindTexture(id); }
        else      glDisable(GL_TEXTURE_2D);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glPushMatrix();
    glTranslatef(cx, cy, cz);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertices.data());

    const bool hasTex = (!texcoords.empty() && tex);
    if (hasTex) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords.data());
    }

    glDrawElements(GL_TRIANGLES, int(indices.size()), GL_UNSIGNED_INT, indices.data());

    glDisableClientState(GL_VERTEX_ARRAY);
    if (hasTex) glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glPopMatrix();

    if (tex) glDisable(GL_TEXTURE_2D);
}

// ═══════════════════════════════════════════════════════════════════════════
//  2-D overlay
// ═══════════════════════════════════════════════════════════════════════════

void begin_2d(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
}

void end_2d()
{
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Аудио
// ═══════════════════════════════════════════════════════════════════════════

void init_audio()
{
    if (ma_engine_init(nullptr, &audio_engine) != MA_SUCCESS) {
        cerr << "Failed to init audio engine\n";
        return;
    }
    cout << "Audio: " << ma_engine_get_channels(&audio_engine) << " ch, "
         << ma_engine_get_sample_rate(&audio_engine) << " Hz\n";
}

void play_sound(const char* filename, float volume)
{
    auto* sound = new ma_sound;
    if (ma_sound_init_from_file(&audio_engine, filename,
            MA_SOUND_FLAG_ASYNC, nullptr, nullptr, sound) != MA_SUCCESS) {
        delete sound;
        return;
    }
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);
    // Автоудаление по окончании
    ma_sound_set_end_callback(sound, [](void*, ma_sound* s) {
        ma_sound_uninit(s);
        delete s;
    }, nullptr);
}

void play_sound_3d(const char* filename, float x, float y, float z, float volume)
{
    auto* sound = new ma_sound;
    if (ma_sound_init_from_file(&audio_engine, filename,
            MA_SOUND_FLAG_ASYNC, nullptr, nullptr, sound) != MA_SUCCESS) {
        delete sound;
        return;
    }
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_position(sound, x, y, z);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);
    ma_sound_set_end_callback(sound, [](void*, ma_sound* s) {
        ma_sound_uninit(s);
        delete s;
    }, nullptr);
}

void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume)
{
    auto* sound = new ma_sound;
    // Без ASYNC: файл должен быть полностью загружен перед зацикливанием
    if (ma_sound_init_from_file(&audio_engine, filename,
            0, nullptr, nullptr, sound) != MA_SUCCESS) {
        cerr << "Cannot load looping sound: " << filename << '\n';
        delete sound;
        return;
    }
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_position(sound, x, y, z);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_set_looping(sound, MA_TRUE);
    ma_sound_start(sound);

    loopingSounds.push_back(sound);   // регистрируем для последующей очистки
}

// Статические ресурсы белого шума (один экземпляр)
static ma_noise        noise_src;
static ma_audio_buffer* noise_buf  = nullptr;
static ma_sound         noise_snd;
static bool             noise_init = false;
static float            noiseData[48000 * 2];   // 1 с, стерео @ 48 kHz

void play_white_noise_3d(float x, float y, float z, float volume)
{
    if (!noise_init) {
        ma_noise_config cfg = ma_noise_config_init(
            ma_format_f32, 2, ma_noise_type_white, 0, 0.3f);
        ma_noise_init(&cfg, nullptr, &noise_src);
        ma_noise_read_pcm_frames(&noise_src, noiseData, 48000, nullptr);

        ma_audio_buffer_config bcfg =
            ma_audio_buffer_config_init(ma_format_f32, 2, 48000, noiseData, nullptr);
        ma_audio_buffer_alloc_and_init(&bcfg, &noise_buf);
        ma_sound_init_from_data_source(&audio_engine, noise_buf, 0, nullptr, &noise_snd);
        noise_init = true;
    }

    ma_sound_set_positioning(&noise_snd, ma_positioning_absolute);
    ma_sound_set_position(&noise_snd, x, y, z);
    ma_sound_set_spatialization_enabled(&noise_snd, MA_TRUE);
    ma_sound_set_volume(&noise_snd, volume);
    ma_sound_set_looping(&noise_snd, MA_TRUE);
    ma_sound_start(&noise_snd);
}

void stop_all_looping_sounds()
{
    for (auto* s : loopingSounds) {
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    loopingSounds.clear();

    if (noise_init) {
        ma_sound_stop(&noise_snd);
        ma_sound_uninit(&noise_snd);
        ma_audio_buffer_uninit_and_free(noise_buf);
        noise_buf  = nullptr;
        noise_init = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Performance HUD
// ═══════════════════════════════════════════════════════════════════════════

void draw_performance_hud(int win_w, int win_h)
{
    static long   prev_cpu  = 0;
    static double cpu_pct   = 0.0;
    static long   ram_kb    = 0;
    static int    frame_cnt = 0;
    static double fps       = 0.0;
    static auto   prev_time = chrono::steady_clock::now();

    ++frame_cnt;
    auto  now     = chrono::steady_clock::now();
    double elapsed = chrono::duration<double>(now - prev_time).count();

    if (elapsed >= 1.0) {
        fps = frame_cnt / elapsed;
        frame_cnt = 0;

        // RAM
        if (FILE* f = fopen("/proc/self/status", "r")) {
            char line[128];
            while (fgets(line, sizeof(line), f))
                if (sscanf(line, "VmRSS: %ld", &ram_kb) == 1) break;
            fclose(f);
        }

        // CPU (jiffies)
        long utime = 0, stime = 0;
        if (FILE* s = fopen("/proc/self/stat", "r")) {
            fscanf(s, "%*d %*s %*c %*d %*d %*d %*d %*d "
                      "%*u %*u %*u %*u %*u %ld %ld", &utime, &stime);
            fclose(s);
        }
        long cur_cpu = utime + stime;
        // delta_jiffies / CLK_TCK = CPU-секунды за период
        // делим на elapsed (стенные секунды) и умножаем на 100 → реальный %
        cpu_pct      = (cur_cpu - prev_cpu) / (double)sysconf(_SC_CLK_TCK) / elapsed * 10.0;
        prev_cpu     = cur_cpu;
        prev_time    = now;
    }

    char buf[80];
    snprintf(buf, sizeof(buf), "FPS: %.0f  RAM: %ld MB  CPU: %.1f%%",
             fps, ram_kb / 1024, cpu_pct);

    begin_2d(win_w, win_h);
    draw_text(buf, 10.0f, float(win_h) - 20.0f,
              GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);
    end_2d();
}