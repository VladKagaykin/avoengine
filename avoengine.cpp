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
// библиотеки для многопоточности
#include <omp.h>
#include <mutex>

//              графика
// вспомогательные утилиты для opengl(матрицы, проекции и прочие нежности для немощей)
#include <GL/glu.h>
// основная библиотека opengl
#include <GLFW/glfw3.h>
#include <GL/glut.h>
// библиотека для импорта текстур
#include <SOIL/SOIL.h>

//              утилиты
// библиотека для работы со временем для замеров производительности
#include <chrono>
// библиотека для того чтобы определить названия компонентов
#include <hwinfo/hwinfo.h>
// математика(п, синусы, косинусы)
#include <cmath>
#include <algorithm>
// удобная запись в переменные через printf и прочую хрень
#include <cstdio>
// взаимодействия с консолью
#include <iostream>
// таблица номер-значение, поможет для текстур
#include <unordered_map>
// нелоховские массивы
#include <vector>
// лоховской текст
#include <string>

//              объявления
// использование пространства имён std 😲
using namespace std;
// переменные для хранения в них размеров окна и экрана
int window_w = 0, window_h = 0, screen_w = 0, screen_h = 0;
static GLFWwindow* g_window = nullptr;
// железо
string cpu_name;
string ram_v;
string gpu_name;
// создание таблицы текстур и их id 
//       имя файла текстуры  его id        
static unordered_map<string, GLuint> textureCache;
// переменная для хранения того, обрабатывает ли текстуры какой-то поток или нет(вроде бы, не знаю как точно)
static mutex textureCacheMutex;
// храним id последней загруженной текстуры
static GLuint boundTextureID = 0;

// инициализация звукового движка(ma_engine тип данных, а audio_engine название)
ma_engine audio_engine;
// вектор в котором хранятся звуки, которые играют на постоянке
static vector<ma_sound*> loopingSounds;

// инициализация камеры
CameraParams camera;

// туман
struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
static fog_params fog;

//              утилиты 
// вычисляем то, куда смотрит центр камеры и прочее
static inline void lookAtForward(float eye_x,float eye_y,float eye_z,float pitch_deg,float yaw_deg,float& cx,float& cy,float& cz,float& dx,float& dy,float& dz){
    const float p=pitch_deg*float(M_PI)/180.0f;
    const float y=yaw_deg*float(M_PI)/180.0f;
    dx=cosf(p)*sinf(y);
    dy=sinf(p);
    dz=cosf(p)*cosf(y);
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

//              шейдеры
// Переменная для хранения текущей программы шейдеров
GLuint currentShaderProg = 0;
// Функция для проверки ошибок компиляции
void checkShaderErrors(GLuint shader, string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << endl;
        }
    }
}

GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode) {
    // 1. Компиляция Vertex Shader
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexCode, NULL);
    glCompileShader(vertex);
    checkShaderErrors(vertex, "VERTEX");

    // 2. Компиляция Fragment Shader
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentCode, NULL);
    glCompileShader(fragment);
    checkShaderErrors(fragment, "FRAGMENT");

    // 3. Создание программы
    GLuint ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkShaderErrors(ID, "PROGRAM");

    // Удаляем шейдеры, они уже прилинкованы к программе
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return ID;
}

void useShader(GLuint id) {
    glUseProgram(id);
    currentShaderProg = id;
}

void stopShader() {
    glUseProgram(0);
    currentShaderProg = 0;
}

GLuint defaultLightingShader = 0;

static const char* defaultVertexShader = R"(
varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;

void main() {
    vN = normalize(gl_NormalMatrix * gl_Normal);
    vec4 mvPos = gl_ModelViewMatrix * gl_Vertex;
    vP = mvPos.xyz;
    vColor = gl_Color;
    vTexCoord = gl_MultiTexCoord0.st;
    gl_Position = ftransform();
}
)";

static const char* defaultFragmentShader = R"(
#define MAX_LIGHTS 16

struct Light {
    bool enabled;
    vec3 position;      // в координатах камеры
    vec3 direction;
    vec3 diffuse;
    float cutoff;       // косинус угла отсечки
    vec3 attenuation;   // constant, linear, quadratic
};

varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;

uniform sampler2D tex;
uniform Light lights[MAX_LIGHTS];
uniform int numLights;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

void main() {
    vec3 N = normalize(vN);
    vec4 texColor = texture2D(tex, vTexCoord);
    vec3 totalLight = ambientLight;

    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= numLights) break;
        if (!lights[i].enabled) continue;

        vec3 L = lights[i].position - vP;
        float dist = length(L);
        L = normalize(L);

        // Spot light (жёсткая отсечка, как в OpenGL)
        vec3 D = normalize(lights[i].direction);
        float cosTheta = dot(-L, D);
        if (cosTheta < lights[i].cutoff) continue;
        float spot = 1.0;

        // Attenuation
        float att = 1.0 / (lights[i].attenuation.x +
                           lights[i].attenuation.y * dist +
                           lights[i].attenuation.z * dist * dist);

        float diff = max(dot(N, L), 0.0);
        totalLight += lights[i].diffuse * diff * spot * att;
    }

    vec3 finalColor = texColor.rgb * vColor.rgb * totalLight;

    // Линейный туман
    float fogCoord = length(vP);
    float fogFactor = clamp((fogEnd - fogCoord) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = mix(fogColor, finalColor, fogFactor);

    gl_FragColor = vec4(finalColor, texColor.a * vColor.a);
}
)";

static void initDefaultShader() {
    if (defaultLightingShader == 0) {
        defaultLightingShader = createShaderProgram(defaultVertexShader, defaultFragmentShader);
    }
}

//              текстуры
// функция для загрузки текстуры
GLuint loadTextureFromFile(const char* filename){
    // проверяем загружена ли текстура, закрываем замок чтобы другой поток не лез одновременно
    {
        lock_guard<mutex> lock(textureCacheMutex);
        auto it=textureCache.find(filename);
        if(it!=textureCache.end()) return it->second;
    }
    // загружаем изображение и записываем ей ширину и высоту в w и h
    int w,h;
    // название текстуры / w / h / сюда можно записать сколько каналов у изображения / принудительно указываем что 4 канала, чтобы была прозрачность
    unsigned char* img=SOIL_load_image(filename,&w,&h,nullptr,SOIL_LOAD_RGBA);
    if(!img){
        cerr<<"Cannot load texture: "<<filename<<" ("<<SOIL_last_result()<<")"<<endl;
        // закрываем замок и записываем что текстуры нет
        lock_guard<mutex> lock(textureCacheMutex);
        return textureCache[filename]=0;
    }
    // создаём новый id для текстуры
    GLuint id;
    // размер / записываем id
    glGenTextures(1,&id);
    // биндим эту текстуру как 2д
    glBindTexture(GL_TEXTURE_2D, id);
    boundTextureID=id;
    // параметры текстуры
    //      указываем цель / параметр который надо изменить/ его значение
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    // передаём текстуру в видеопамять
    // формат / детализация(хз что это значит) / формат хранения / ширина / высота / граница(также хз) / входной формат / тип данных / изображение
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,img);
    // освобождаем текстуру из памяти
    SOIL_free_image_data(img);
    // закрываем замок и возвращаем id текстуры
    lock_guard<mutex> lock(textureCacheMutex);
    return textureCache[filename]=id;
}
// загружаем много текстур параллельно
void preloadTextures(const vector<string>& filenames){
    // структура для хранения загруженных с диска данных до передачи в видеопамять
    struct RawTex{string name;unsigned char* data;int w,h;};
    vector<RawTex> loaded(filenames.size());
    // параллельно грузим файлы с диска
    // schedule(dynamic) значит что потоки берут задачи по одной по мере освобождения, а не поровну сразу
    // это нужно т.к. текстуры разного размера и некоторые грузятся дольше
    #pragma omp parallel for schedule(dynamic)
    for(int i=0;i<(int)filenames.size();++i){
        // пропускаем если текстура уже загружена, закрываем замок чтобы проверить безопасно
        {
            lock_guard<mutex> lock(textureCacheMutex);
            if(textureCache.count(filenames[i])){
                loaded[i]={filenames[i],nullptr,0,0};
                continue;
            }
        }
        int w,h;
        unsigned char* img=SOIL_load_image(filenames[i].c_str(),&w,&h,nullptr,SOIL_LOAD_RGBA);
        loaded[i]={filenames[i],img,w,h};
    }
    // передаём в видеопамять строго из одного потока т.к. opengl однопоточный
    for(auto& t:loaded){
        if(!t.data) continue;
        GLuint id;
        glGenTextures(1,&id);
        glBindTexture(GL_TEXTURE_2D,id);
        boundTextureID=id;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.w,t.h,0,GL_RGBA,GL_UNSIGNED_BYTE,t.data);
        SOIL_free_image_data(t.data);
        // закрываем замок и записываем текстуру в таблицу
        lock_guard<mutex> lock(textureCacheMutex);
        textureCache[t.name]=id;
    }
}
// удаляем все текстуры из памяти
void clearTextureCache(){
    // перебираем все имена и id и удаляем их
    for(auto& [name,id] : textureCache)
        if(id) glDeleteTextures(1, &id);
    textureCache.clear();
    boundTextureID=0;
}

//              хз как это назвать
// поворачиваем текстуру вокруг точки
void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad){
    // перенос в 0 для удобного рассчёта
    const float tx=x-cx,ty=y-cy;
    // рассчёт поворота и возвращаем как было
    const float c=cosf(angle_rad),s=sinf(angle_rad);
    x=cx+tx*c-ty*s;
    y=cy+tx*s+ty*c;
}

// функция для 2д фигур: указываем название текстуры и если она существует, то биндим её, если нет, то указываем что фигура не использует текстуру
static void enableTex(const char* file){
    if(!file){
        glDisable(GL_TEXTURE_2D);
        return;
    }
    GLuint id=loadTextureFromFile(file);
    if(id){
        glEnable(GL_TEXTURE_2D);
        bindTexture(id);
    }else{
        glDisable(GL_TEXTURE_2D);
    }
}

//              простые 2д фигуры
// треугольник
void triangle(float scale,float cx,float cy,double r,double g,double b,float rotate,const float* vertices,const char* tex){
    // задаём цвет
    glColor3f(float(r), float(g), float(b));
    // задаём/не задаём текстуру
    enableTex(tex);
    // преобразуем поворот в радианы
    const float ar=rotate*float(M_PI)/-180.0f;
    // задаём координаты текстуры
    const float tc[6]={0,1, 1,1, 0,0};
    // задаём что фигура-треугольник
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 3; ++i) {
        // берём координаты вершины и рассчитываем их поворот
        float px=vertices[i*2], py=vertices[i*2+1];
        rotatePoint(px,py,0,0,ar);
        // отправляем координату текстуры
        if(tex)glTexCoord2f(tc[i*2],tc[i*2+1]);
        // отправляем вершину в opengl
        glVertex2f(cx+px*scale,cy+py*scale);
    }
    // объявляем что рисовка фигуры завершена
    glEnd();
    // выключаем текстуру
    if(tex)glDisable(GL_TEXTURE_2D);
}
// всё тоже самое, только квадрат
void square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* tex){
    glColor3f(float(r),float(g),float(b));
    enableTex(tex);
    const float ar=rotate*float(M_PI)/-180.0f;
    const float tc[8]={0,1, 1,1, 1,0, 0,0};
    glBegin(GL_QUADS);
    for (int i=0;i<4;++i){
        float px=vertices[i*2],py=vertices[i*2+1];
        rotatePoint(px,py,0,0,ar);
        if(tex)glTexCoord2f(tc[i*2],tc[i*2+1]);
        glVertex2f(x+px*local_size,y+py*local_size);
    }
    glEnd();
    if(tex)glDisable(GL_TEXTURE_2D);
}
void light_square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* tex){
    glColor3f(float(r),float(g),float(b));
    enableTex(tex);
    const float ar=rotate*float(M_PI)/-180.0f;
    const float tc[10] = {
                            0,1,
                            1,1, 
                            1,0, 
                            0,0, 
                            0.5f,0.5f
                            };
    
    float center_x = (vertices[0] + vertices[2] + vertices[4] + vertices[6]) / 4.0f;
    float center_y = (vertices[1] + vertices[3] + vertices[5] + vertices[7]) / 4.0f;
    
    float all_vertices[10] = {
                                vertices[0], vertices[1],
                                vertices[2], vertices[3],
                                vertices[4], vertices[5],
                                vertices[6], vertices[7],
                                center_x, center_y
                            };
    
    const int triangles[4][3] = {
                                    {0, 1, 4},
                                    {1, 2, 4},
                                    {2, 3, 4},
                                    {3, 0, 4}
                                };
    
    glBegin(GL_TRIANGLES);
    for (int tri = 0; tri < 4; ++tri) {
        for (int i = 0; i < 3; ++i) {
            int vidx = triangles[tri][i];
            float px = all_vertices[vidx * 2];
            float py = all_vertices[vidx * 2 + 1];
            rotatePoint(px, py, 0, 0, ar);
            if(tex) glTexCoord2f(tc[vidx * 2], tc[vidx * 2 + 1]);
            glVertex2f(x + px * local_size, y + py * local_size);
        }
    }
    glEnd();
    if(tex) glDisable(GL_TEXTURE_2D);
}
// круг
void circle(float scale,float cx,float cy,double r,double g,double b,float radius,float in_radius,float rotate,int slices,int loops,const char* tex){
    // уже было
    glColor3f(float(r),float(g),float(b));
    enableTex(tex);
    // как я понял мы делаем круг в отдельной матрице(квадрика), а потом прибавляем к основной
    glPushMatrix();
    glTranslatef(cx,cy,0);
    glRotatef(-rotate,0,0,1);
    glScalef(scale,scale,1);
    static GLUquadric* q=nullptr;
    if (!q){
        q=gluNewQuadric();
        gluQuadricTexture(q,GL_TRUE);
        gluQuadricDrawStyle(q,GLU_FILL);
    }
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1,-1,1);
    gluDisk(q,in_radius,radius,slices,loops);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    if(tex)glDisable(GL_TEXTURE_2D);
}
// рисовка текста
void draw_text(const char* text,float x,float y,void* font,float r,float g,float b,float a){
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    // задаём цвет
    glColor4f(r,g,b,a);
    // задаём позицию
    glRasterPos2f(x,y);
    // рисуем текст по символам
    for(const char* c=text;*c;++c){
        glutBitmapCharacter(font,*c);
    }
}

//              класс для рисовки псевдо 3д существ
// проверяем есть ли на экране
bool pseudo_3d_entity::isVisible(float cam_x, float cam_y, float cam_z) const{
    const float dx=x-cam_x,dy=y-cam_y,dz=z-cam_z;
    const float fx=camera.ctr_x-camera.eye_x;
    const float fy=camera.ctr_y-camera.eye_y;
    const float fz=camera.ctr_z-camera.eye_z;

    const float depth=dx*fx+dy*fy+dz*fz;

    if(depth+radius<camera.znear)return false;
    if(depth-radius>camera.zfar)return false;

    const float dist=sqrtf(dx*dx+dy*dy+dz*dz);
    if(dist<1e-4f)return true;

    const float aspect=(window_h>0)?float(window_w)/float(window_h):1.0f;
    const float half_v=camera.fov*0.5f*float(M_PI)/180.0f;
    const float half_h=atanf(tanf(half_v)*aspect);
    const float half_diag=sqrtf(half_h*half_h+half_v*half_v);
    const float slack=asinf(fminf(1.0f,radius/dist));

    return (depth/dist)>=cosf(half_diag+slack);
}
// вычисляем какую текстуру поставить
int pseudo_3d_entity::getTextureIndex(float dir_x, float dir_y, float dir_z) const {
    if (fabsf(dir_x - cachedDirX) < 0.01f &&
        fabsf(dir_y - cachedDirY) < 0.01f &&
        fabsf(dir_z - cachedDirZ) < 0.01f)
        return cachedTexIdx;

    cachedDirX = dir_x;
    cachedDirY = dir_y;
    cachedDirZ = dir_z;

    if (textures.empty()) {
        cachedTexIdx = -1;
        return -1;
    }

    const int total = int(textures.size());
    const int h_count = total / v_angles;
    if (h_count <= 0) {
        cachedTexIdx = -1;
        return -1;
    }
    const float ga = g_angle * float(M_PI) / 180.0f;
    const float va = v_angle * float(M_PI) / 180.0f;
    const float ra = r_angle * float(M_PI) / 180.0f;


    float lx = dir_x, ly = dir_y, lz = dir_z;

    float cos_ra = cosf(-ra), sin_ra = sinf(-ra);
    float tx = lx * cos_ra - ly * sin_ra;
    float ty = lx * sin_ra + ly * cos_ra;
    lx = tx; ly = ty;

    float cos_va = cosf(-va), sin_va = sinf(-va);
    tx = lx;
    ty = ly * cos_va - lz * sin_va;
    float tz = ly * sin_va + lz * cos_va;
    lx = tx; ly = ty; lz = tz;

    float cos_ga = cosf(-ga), sin_ga = sinf(-ga);
    tx = lx * cos_ga + lz * sin_ga;
    tz = -lx * sin_ga + lz * cos_ga;
    lx = tx; lz = tz;

    float local_h = atan2f(lx, lz) * 180.0f / float(M_PI);
    float local_v = atan2f(ly, sqrtf(lx*lx + lz*lz)) * 180.0f / float(M_PI);

    float v_rel = fmaxf(0.0f, fminf(180.0f, local_v + 90.0f));
    int v_index = int(fminf(v_rel / (180.0f / v_angles), float(v_angles - 1)));

    const float step_h = 360.0f / h_count;
    if (local_h < 0) local_h += 360.0f;
    int h_index = int((local_h + step_h * 0.5f) / step_h) % h_count;

    cachedTexIdx = v_index * h_count + h_index;
    if (cachedTexIdx >= total) cachedTexIdx = total - 1;
    return cachedTexIdx;
}
// рисуем сущность
void pseudo_3d_entity::draw(float cam_x, float cam_y, float cam_z) const {
    if (!isVisible(cam_x, cam_y, cam_z)) return;

    const float dx = cam_x - x;
    const float dy = cam_y - y;
    const float dz = cam_z - z;
    const float dist = sqrtf(dx*dx + dy*dy + dz*dz);

    const int tidx = getTextureIndex(dx, dy, dz);
    const char* tex = (tidx >= 0) ? textures[tidx] : nullptr;

    const float fx = (dist > 1e-4f) ? dx / dist : 0.0f;
    const float fy = (dist > 1e-4f) ? dy / dist : 1.0f;
    const float fz = (dist > 1e-4f) ? dz / dist : 0.0f;

    float wx = 0, wy = 1, wz = 0;
    if (fabsf(fy) > 0.999f) { wx = 0; wy = 0; wz = 1; }
    float rx = wy * fz - wz * fy;
    float ry = wz * fx - wx * fz;
    float rz = wx * fy - wy * fx;
    const float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rlen > 1e-4f) { rx /= rlen; ry /= rlen; rz /= rlen; }

    const float ux = fy * rz - fz * ry;
    const float uy = fz * rx - fx * rz;
    const float uz = fx * ry - fy * rx;

    const float mat[16] = {
        rx, ry, rz, 0,
        ux, uy, uz, 0,
        fx, fy, fz, 0,
        0,  0,  0,  1
    };

    const float ga = g_angle * float(M_PI) / 180.0f;
    const float va = v_angle * float(M_PI) / 180.0f;

    float eu_x = -sinf(ga) * sinf(va);
    float eu_y = -cosf(va);
    float eu_z = -cosf(ga) * sinf(va);

    float dot = eu_x * fx + eu_y * fy + eu_z * fz;
    float pu_x = eu_x - dot * fx;
    float pu_y = eu_y - dot * fy;
    float pu_z = eu_z - dot * fz;
    float plen = sqrtf(pu_x*pu_x + pu_y*pu_y + pu_z*pu_z);

    if (plen < 0.01f) {
        const float ef_x = cosf(va) * sinf(ga);
        const float ef_y = -sinf(va);
        const float ef_z = cosf(va) * cosf(ga);
        const float d2 = ef_x * fx + ef_y * fy + ef_z * fz;
        pu_x = ef_x - d2 * fx;
        pu_y = ef_y - d2 * fy;
        pu_z = ef_z - d2 * fz;
    }

    float billboard_roll = atan2f(-(pu_x * rx + pu_y * ry + pu_z * rz),
                                   pu_x * ux + pu_y * uy + pu_z * uz) * 180.0f / float(M_PI);

    float total_roll = billboard_roll + r_angle;

    const bool mirror = (tidx == 0);

    glPushMatrix();
    glTranslatef(x, y, z);
    glMultMatrixf(mat);
    glRotatef(total_roll + 180.0f, 0, 0, 1);
    light_square(1.0f, 0, 0, 1, 1, 1, mirror ? -180.0f : 0.0f, vertices, tex);
    glPopMatrix();
}

//              opengl
// настройка изменения размеров в 3д режиме
void changeSize3D(int w,int h){
    // проверка чтобы избежать деления на 0
    if(h==0)h=1;
    // задаём область вывода от координат 0,0 до координат w,h 
    glViewport(0,0,w,h);
    // переключение матрицы в проекцию(хз что это значит)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // настройка перспективы
    // fov | соотношение сторон / ближняя плоскость где не отображаем / дальняя плоскость где не отображаем 
    gluPerspective(camera.fov,float(w)/float(h),camera.znear,camera.zfar);
    // переключение матрицы обратно в модельно-видовую(хз что это значит) 
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // настройка камеры
    gluLookAt(camera.eye_x,camera.eye_y,camera.eye_z,
              camera.ctr_x,camera.ctr_y,camera.ctr_z,
              camera.up_x,camera.up_y,camera.up_z);
    window_w=w;
    window_h=h;
}
// настройка изменения размеров в 2д режиме
void changeSize2D(int w,int h){
    // уже было
    if(h==0)h=1;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // соотношение сторон
    const float ratio=float(w)/float(h);
    // установка 2д проекции чтобы всегда была одна и таже система координат
    if(w<=h)glOrtho(-1,1,-1/ratio,1/ratio,1,-1);
    else glOrtho(-ratio,ratio,-1,1,1,-1);
    // уже было
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // выводим размеры окна в переменные, чтобы разработчик игры их мог использовать
    window_w=w;
    window_h=h;
}
// переключение изменения размеров
static bool is3D = false;
void framebuffer_size_callback(GLFWwindow* /*window*/, int w, int h){
    if (is3D) {
        changeSize3D(w, h);
    } else {
        changeSize2D(w, h);
    }
    window_w = w;
    window_h = h;
}
// инициализация окна
void setup_display(int* argc, char** argv, float r, float g, float b, float a, const char* name, int w, int h) {
    init_audio();
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW\n";
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    screen_w = mode->width;
    screen_h = mode->height;

    GLFWwindow* window = glfwCreateWindow(w, h, name, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        cerr << "Failed to create GLFW window\n";
        exit(EXIT_FAILURE);
    }
    glfwSetWindowPos(window, screen_w / 4, screen_h / 8);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        cerr << "Failed to initialize GLEW\n";
    }

    // Колбэк изменения размера (замена glutReshapeFunc)
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int width, int height) {
        window_w = width;
        window_h = height;
        // Логика выбора changeSize2D / changeSize3D остаётся за вами (вы уже исправили)
    });

    // Системная информация (как раньше)
    auto cpus = hwinfo::getAllCPUs();
    cpu_name = cpus.empty() ? "Unknown" : cpus[0].modelName();
    hwinfo::Memory mem = hwinfo::Memory();
    ram_v = to_string(mem.total_Bytes() / (1024 * 1024)) + " MB";
    auto gpus = hwinfo::getAllGPUs();
    gpu_name = gpus.empty() ? "Unknown" : gpus[0].name();

    window_w = w;
    window_h = h;

    glClearColor(r, g, b, a);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    initDefaultShader();
    changeSize2D(w, h);
}
// настройка камеры
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw){
    // задаём параметры камеры
    camera.fov=fov;
    camera.znear=0.1f;
    camera.zfar=1000.0f;
    camera.eye_x=eye_x; 
    camera.eye_y=eye_y;
    camera.eye_z=eye_z;

    float norm_pitch = fmod(pitch, 360.0f);
    if (norm_pitch < 0) norm_pitch += 360.0f;

    float adj_pitch = norm_pitch;
    float up_x = 0, up_y = 1, up_z = 0;

    ma_engine_listener_set_position(&audio_engine,0,eye_x,eye_y,eye_z);
    ma_engine_listener_set_direction(&audio_engine,0,camera.dir_x, camera.dir_y, camera.dir_z);

    if (norm_pitch > 90.0f && norm_pitch < 270.0f) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;                    
        yaw += 180.0f;
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, -1.0f, 0.0f); 
    } else {
        if (norm_pitch > 270.0f) adj_pitch = norm_pitch - 360.0f;
        up_y = 1.0f;
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, 1.0f, 0.0f);
    }

    camera.up_x = up_x;
    camera.up_y = up_y;
    camera.up_z = up_z;
    // вычисляем точку взгляда
    lookAtForward(eye_x,eye_y,eye_z,adj_pitch,yaw,camera.ctr_x,camera.ctr_y,camera.ctr_z,camera.dir_x, camera.dir_y, camera.dir_z);

    // настройка матрицы на проекцию
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect=(window_h>0)? float(window_w)/float(window_h):1.0f;
    gluPerspective(fov,aspect,camera.znear,camera.zfar);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x,eye_y,eye_z,camera.ctr_x,camera.ctr_y,camera.ctr_z, up_x, up_y, up_z);
}
// перемещение камеры
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw){
    // обновляем параметры камеры
    camera.eye_x=eye_x;
    camera.eye_y=eye_y;
    camera.eye_z=eye_z;

    float norm_pitch = fmod(pitch, 360.0f);
    if (norm_pitch < 0) norm_pitch += 360.0f;

    float adj_pitch = norm_pitch;
    float up_x = 0, up_y = 1, up_z = 0;

    ma_engine_listener_set_position(&audio_engine,0,eye_x,eye_y,eye_z);
    ma_engine_listener_set_direction(&audio_engine,0,camera.dir_x, camera.dir_y, camera.dir_z);

    if (norm_pitch > 90.0f && norm_pitch < 270.0f) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;                    
        yaw += 180.0f;
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, -1.0f, 0.0f); 
    } else {
        if (norm_pitch > 270.0f) adj_pitch = norm_pitch - 360.0f;
        up_y = 1.0f;
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, 1.0f, 0.0f);
    }

    camera.up_x = up_x;
    camera.up_y = up_y;
    camera.up_z = up_z;

    // считаем направление взгляда
    lookAtForward(eye_x,eye_y,eye_z,adj_pitch,yaw,camera.ctr_x,camera.ctr_y,camera.ctr_z, camera.dir_x, camera.dir_y, camera.dir_z);

    // обновляем матрицу
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x,eye_y,eye_z,camera.ctr_x,camera.ctr_y,camera.ctr_z, up_x, up_y, up_z);
}

//              3д(может быть потом ещё что-то будет)
// рисуем 3д объект, указывая вершины треугольников
void draw3DObject(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices,const std::vector<int>& indices,const std::vector<float>& texcoords,const std::vector<float>& normals){
    // цвет
    glColor3f(float(r),float(g),float(b));
    // текстура
    if(tex){
        GLuint id=loadTextureFromFile(tex);
        if(id){
            glEnable(GL_TEXTURE_2D);
            bindTexture(id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }else{
            glDisable(GL_TEXTURE_2D);
        }
    }else{
        glDisable(GL_TEXTURE_2D);
    }
    // позиционирование
    glPushMatrix();
    glTranslatef(cx,cy,cz);
    // делаем объект
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,vertices.data());
    // делаем текстуру
    const bool hasTex=(!texcoords.empty()&&tex);
    if(hasTex){
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2,GL_FLOAT,0,texcoords.data());
    }
    // рендерим
    if (!normals.empty()) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, normals.data());
    }
    glDrawElements(GL_TRIANGLES,int(indices.size()),GL_UNSIGNED_INT,indices.data());
    if (!normals.empty()) glDisableClientState(GL_NORMAL_ARRAY);
    // очищаем память
    glDisableClientState(GL_VERTEX_ARRAY);
    if(hasTex)glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glPopMatrix();
    if(tex)glDisable(GL_TEXTURE_2D);
}
// свет
static bool lighting_global = false;

void enable_light() {
    if (!lighting_global) {
        initDefaultShader();
        useShader(defaultLightingShader);   // currentShaderProg = defaultLightingShader
        lighting_global = true;
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

        // Передаём начальные uniform-значения в текущий шейдер
        set_ambient_light(0.05f, 0.05f, 0.05f);
        if (fog.enabled) {
            glUniform3f(glGetUniformLocation(currentShaderProg, "fogColor"),
                        fog.color[0], fog.color[1], fog.color[2]);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogStart"), fog.start);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogEnd"), fog.end);
        }
        applyAllLights();
    }
}

void disable_light() {
    if (lighting_global) {
        stopShader();
        lighting_global = false;
    }
}

static std::vector<Light*> activeLights;

Light::Light() {
    // По умолчанию источник выключен
}

void Light::setPosition(float x, float y, float z) {pos[0] = x; pos[1] = y; pos[2] = z;}

void Light::setDirectionFromPitchYaw(float pitch_deg, float yaw_deg) {
    float pitch = pitch_deg * M_PI / 180.0f;
    float yaw   = yaw_deg   * M_PI / 180.0f;
    dir[0] = cosf(pitch) * sinf(yaw);
    dir[1] = sinf(pitch);
    dir[2] = cosf(pitch) * cosf(yaw);
}

void Light::setColor(float r, float g, float b) {
    color[0] = r; color[1] = g; color[2] = b;
}

void Light::setIntensity(float i) {
    intensity = i;
}

void Light::setRadius(float radius_deg) {
    cutoff = (radius_deg >= 360.0f) ? 180.0f : radius_deg;
}

void Light::setAttenuation(float constant, float linear, float quadratic) {
    constAtt = constant;
    linearAtt = linear;
    quadAtt = quadratic;
}

void Light::enable() {
    if (!enabled) {
        enabled = true;
        activeLights.push_back(this);
    }
}

void Light::disable() {
    if (enabled) {
        enabled = false;
        auto it = std::find(activeLights.begin(), activeLights.end(), this);
        if (it != activeLights.end()) activeLights.erase(it);
    }
}

void Light::applyToShader(int index, GLuint prog) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "lights[%d].enabled", index);
    glUniform1i(glGetUniformLocation(prog, buf), enabled ? 1 : 0);
    if (!enabled) return;

    snprintf(buf, sizeof(buf), "lights[%d].position", index);
    glUniform3fv(glGetUniformLocation(prog, buf), 1, pos);

    snprintf(buf, sizeof(buf), "lights[%d].direction", index);
    glUniform3fv(glGetUniformLocation(prog, buf), 1, dir);

    float diff[3] = { color[0]*intensity, color[1]*intensity, color[2]*intensity };
    snprintf(buf, sizeof(buf), "lights[%d].diffuse", index);
    glUniform3fv(glGetUniformLocation(prog, buf), 1, diff);

    snprintf(buf, sizeof(buf), "lights[%d].cutoff", index);
    glUniform1f(glGetUniformLocation(prog, buf), cosf(cutoff * M_PI / 180.0f));

    snprintf(buf, sizeof(buf), "lights[%d].attenuation", index);
    glUniform3f(glGetUniformLocation(prog, buf), constAtt, linearAtt, quadAtt);
}

void applyAllLights() {
    GLuint prog = currentShaderProg;
    if (prog == 0) return;

    // Собираем все включенные источники
    std::vector<Light*> candidates;
    for (Light* light : activeLights) {
        if (light->isEnabled()) {
            candidates.push_back(light);
        }
    }

    if (candidates.empty()) {
        glUniform1i(glGetUniformLocation(prog, "numLights"), 0);
        return;
    }

    // Сортировка по важности 
    const float camX = camera.eye_x;
    const float camY = camera.eye_y;
    const float camZ = camera.eye_z;
    const float camDirX = camera.dir_x;
    const float camDirY = camera.dir_y;
    const float camDirZ = camera.dir_z;

    std::sort(candidates.begin(), candidates.end(),
        [&](Light* a, Light* b) {
            float dx_a = a->pos[0] - camX;
            float dy_a = a->pos[1] - camY;
            float dz_a = a->pos[2] - camZ;
            float dist_a = sqrtf(dx_a*dx_a + dy_a*dy_a + dz_a*dz_a) + 0.001f;

            float dx_b = b->pos[0] - camX;
            float dy_b = b->pos[1] - camY;
            float dz_b = b->pos[2] - camZ;
            float dist_b = sqrtf(dx_b*dx_b + dy_b*dy_b + dz_b*dz_b) + 0.001f;

            float dot_a = (dx_a * camDirX + dy_a * camDirY + dz_a * camDirZ) / dist_a;
            float dot_b = (dx_b * camDirX + dy_b * camDirY + dz_b * camDirZ) / dist_b;

            float dirFactor_a = std::max(0.2f, dot_a);
            float dirFactor_b = std::max(0.2f, dot_b);

            float weight_a = a->intensity * dirFactor_a / (dist_a * dist_a);
            float weight_b = b->intensity * dirFactor_b / (dist_b * dist_b);

            return weight_a > weight_b;
        });

    int count = std::min((int)candidates.size(), MAX_LIGHTS);
    glUniform1i(glGetUniformLocation(prog, "numLights"), count);

    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    GLfloat mv3[9] = {
        mv[0], mv[1], mv[2],
        mv[4], mv[5], mv[6],
        mv[8], mv[9], mv[10]
    };

    for (int i = 0; i < count; ++i) {
        Light* light = candidates[i];

        float worldPos[4] = { light->pos[0], light->pos[1], light->pos[2], 1.0f };
        float viewPos[4] = {0,0,0,0};
        for (int r = 0; r < 4; ++r) {
            viewPos[r] = mv[r]   * worldPos[0] +
                         mv[r+4] * worldPos[1] +
                         mv[r+8] * worldPos[2] +
                         mv[r+12]* worldPos[3];
        }

        float worldDir[3] = { light->dir[0], light->dir[1], light->dir[2] };
        float viewDir[3] = {0,0,0};
        for (int r = 0; r < 3; ++r) {
            viewDir[r] = mv3[r]   * worldDir[0] +
                         mv3[r+3] * worldDir[1] +
                         mv3[r+6] * worldDir[2];
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
        glUniform1i(glGetUniformLocation(prog, buf), 1);

        snprintf(buf, sizeof(buf), "lights[%d].position", i);
        glUniform3fv(glGetUniformLocation(prog, buf), 1, viewPos);

        snprintf(buf, sizeof(buf), "lights[%d].direction", i);
        glUniform3fv(glGetUniformLocation(prog, buf), 1, viewDir);

        float diff[3] = { light->color[0] * light->intensity,
                          light->color[1] * light->intensity,
                          light->color[2] * light->intensity };
        snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
        glUniform3fv(glGetUniformLocation(prog, buf), 1, diff);

        snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
        glUniform1f(glGetUniformLocation(prog, buf), cosf(light->cutoff * M_PI / 180.0f));

        snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
        glUniform3f(glGetUniformLocation(prog, buf), light->constAtt, light->linearAtt, light->quadAtt);
    }
}

void set_ambient_light(float r, float g, float b) {
    if (currentShaderProg) {
        glUniform3f(glGetUniformLocation(currentShaderProg, "ambientLight"), r, g, b);
    }
}

void apply_material(float r, float g, float b, float alpha, float shininess){
    GLfloat mat_ambient[]  = {r*0.3f, g*0.3f, b*0.3f, alpha};
    GLfloat mat_diffuse[]  = {r,      g,      b,      alpha};
    GLfloat mat_specular[] = {0.5f, 0.5f, 0.5f, alpha};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,   mat_ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,   mat_diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  mat_specular);
    glMaterialf (GL_FRONT_AND_BACK, GL_SHININESS, shininess);
}

//              туман
void enable_fog(float density, float r, float g, float b, float start, float end) {
    fog.enabled = true;
    fog.density = density;
    fog.color[0] = r; fog.color[1] = g; fog.color[2] = b;
    fog.start = start;
    fog.end = end;

    if (currentShaderProg) {
        glUniform3f(glGetUniformLocation(currentShaderProg, "fogColor"), r, g, b);
        glUniform1f(glGetUniformLocation(currentShaderProg, "fogStart"), start);
        glUniform1f(glGetUniformLocation(currentShaderProg, "fogEnd"), end);
    }
    // Убираем glEnable(GL_FOG) и настройки фиксированного тумана
}

void disable_fog(){
    fog.enabled = false;
    // Убираем glDisable(GL_FOG);
}

void set_fog_density(float density) {
    fog.density = density;
    if (fog.enabled) {
        fog.start = 2.0f / density;
        fog.end = 15.0f / density;
        if (currentShaderProg) {
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogStart"), fog.start);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogEnd"), fog.end);
        }
    }
}

void set_fog_color(float r, float g, float b) {
    fog.color[0] = r; fog.color[1] = g; fog.color[2] = b;
    if (fog.enabled) {
        if (currentShaderProg) {
            glUniform3f(glGetUniformLocation(currentShaderProg, "fogColor"), r, g, b);
        }
    }
}

void set_fog_range(float start, float end) {
    fog.start = start;
    fog.end = end;
    if (fog.enabled) {
        if (currentShaderProg) {
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogStart"), start);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogEnd"), end);
        }
    }
}

//              включение/выключение 3д т.к. опенжиэль не может рисовать одновременно и так и так
// переключаем матрицу на 2д
void begin_2d(int w, int h) {
    is3D = false;
    stopShader();                  
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
// переключаем матрицу на 3д(невероятно)
void end_2d() {
    is3D = true;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_FOG);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    changeSize3D(window_w, window_h);

    if (lighting_global) {
        useShader(defaultLightingShader);
        // Восстанавливаем uniform тумана и освещения
        if (fog.enabled) {
            glUniform3f(glGetUniformLocation(currentShaderProg, "fogColor"),
                        fog.color[0], fog.color[1], fog.color[2]);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogStart"), fog.start);
            glUniform1f(glGetUniformLocation(currentShaderProg, "fogEnd"), fog.end);
        }
        applyAllLights();
    }
}

//              аудио
// инициализация аудио
void init_audio(){
    if(ma_engine_init(nullptr,&audio_engine)!=MA_SUCCESS){
        cerr<<"Failed to init audio engine\n";
        return;
    }
    cout<<"Audio: "<<ma_engine_get_channels(&audio_engine)<<" ch, "<<ma_engine_get_sample_rate(&audio_engine)<<" Hz"<<endl;
}
// проигрывание звука(просто)
void play_sound(const char* filename,float volume){
    // проверка на существование звука
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,MA_SOUND_FLAG_ASYNC,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // указываем что звук вне пространства
    ma_sound_set_spatialization_enabled(sound, MA_FALSE);
    // звук
    ma_sound_set_volume(sound, volume);
    // проигрывание
    ma_sound_start(sound);
}
void play_sound_loop(const char* filename,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    // отключаем пространственную обработку
    ma_sound_set_spatialization_enabled(sound,MA_FALSE);
    // устанавливаем громкость
    ma_sound_set_volume(sound,volume);
    // включаем зацикливание
    ma_sound_set_looping(sound,MA_TRUE);
    // проигрываем звук
    ma_sound_start(sound);
    // добавляем в вектор для последующей очистки
    loopingSounds.push_back(sound);
}
// проигрываем звук в 3д(сложно)
void play_sound_3d(const char* filename,float x,float y,float z,float volume){
    // проверка
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // задаём где слушатель и звук и другие параметры
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    // проигрываем звук
    ma_sound_start(sound);
}
// проигрываем звук в 3д бесконечно(всё то же самое, только бесконечно)
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    ma_sound_set_looping(sound,MA_TRUE);
    ma_sound_start(sound);
    loopingSounds.push_back(sound);
}
// останавливаем все бесконечные звуки(тут из названий всё понятно)
void stop_all_looping_sounds(){
    for(auto* s:loopingSounds){
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    loopingSounds.clear();
}
//              оверлей
// сколько заполнено оперативки/процессора
void draw_performance_hud(int win_w,int win_h){
    // переменные для рассчётов
    static long prev_cpu=0;
    static double cpu_pct=0.0;
    static long ram_kb=0;
    static int frame_cnt=0;
    static double fps=0.0;
    static auto prev_time=chrono::steady_clock::now();
    // счётчик кадров
    ++frame_cnt;
    auto now=chrono::steady_clock::now();
    double elapsed=chrono::duration<double>(now-prev_time).count();
    // обновление статистики
    if(elapsed>=1.0){
        fps=frame_cnt/elapsed;
        frame_cnt=0;

        // объявляем до параллельного блока т.к. внутри они должны быть видны обоим потокам
        long local_ram=0;
        long utime=0,stime=0;

        // параллельно читаем оба файла /proc т.к. это два независимых чтения с диска
        // sections значит что каждый кусок кода помеченный как section выполняется в отдельном потоке
        #pragma omp parallel sections
        {
            #pragma omp section
            {
                if(FILE* f=fopen("/proc/self/status","r")){
                    char line[128];
                    while(fgets(line,sizeof(line),f))
                        if(sscanf(line,"VmRSS: %ld",&local_ram)==1)break;
                    fclose(f);
                }
            }
            #pragma omp section
            {
                if(FILE* s=fopen("/proc/self/stat","r")){
                    fscanf(s,"%*d %*s %*c %*d %*d %*d %*d %*d "
                              "%*u %*u %*u %*u %*u %ld %ld",&utime,&stime);
                    fclose(s);
                }
            }
        }

        ram_kb=local_ram;
        long cur_cpu=utime+stime;
        cpu_pct=(cur_cpu-prev_cpu)/(double)sysconf(_SC_CLK_TCK)/elapsed*10.0;
        prev_cpu=cur_cpu;
        prev_time=now;
    }
    // вывод статистики в левом верхнем углу
    char buf[256];
    snprintf(buf,sizeof(buf),"FPS: %.0f  RAM: %ld MB  CPU: %.1f%%",fps,ram_kb / 1024,cpu_pct);
    begin_2d(win_w,win_h);
    draw_text(buf,10.0f,float(win_h)-20.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"X: %.10f  Y: %.10f  Z: %.10f",camera.eye_x,camera.eye_y,camera.eye_z);
    draw_text(buf,10.0f,float(win_h)-32.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"CPU: %s  RAM: %s  GPU: %s",cpu_name.c_str(),ram_v.c_str(),gpu_name.c_str());
    draw_text(buf,10.0f,float(win_h)-44.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    end_2d();
}