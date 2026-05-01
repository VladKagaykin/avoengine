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
fog_params fog;
// свет
static std::vector<Light*> activeLights;

float global_pitch = 0.0f;
float global_yaw = 0.0f;
float global_ambient[3] = {0.05f, 0.05f, 0.05f};
std::vector<pseudo_3d_entity*> allEntities;

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
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexCode, NULL);
    glCompileShader(vertex);
    checkShaderErrors(vertex, "VERTEX");

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentCode, NULL);
    glCompileShader(fragment);
    checkShaderErrors(fragment, "FRAGMENT");

    GLuint ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkShaderErrors(ID, "PROGRAM");

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
#version 120
varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec4 vClipPos;

void main() {
    vN = normalize(gl_NormalMatrix * gl_Normal);
    vec4 mvPos = gl_ModelViewMatrix * gl_Vertex;
    vP = mvPos.xyz;
    vColor = gl_Color;
    vTexCoord = gl_MultiTexCoord0.st;
    
    vWorldPos = vec3(gl_Vertex); 
    
    gl_Position = ftransform();
    vClipPos = gl_Position;
}
)";

static const char* defaultFragmentShader = R"(
#version 120
#define MAX_LIGHTS 16
#define MAX_SHADOW_CASTERS 8

struct Light {
    bool enabled;
    vec3 position;
    vec3 direction;
    vec3 diffuse;
    float cutoff;
    vec3 attenuation;
};

struct ShadowCaster {
    mat4 shadowMatrix;
    sampler2D shadowMap;
    float darkness;
    vec3 lightPos;
    vec3 lightDirection;
    float lightCutoff;
    float lightObjDist; 
};

varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec4 vClipPos;

uniform sampler2D tex;
uniform Light lights[MAX_LIGHTS];
uniform int numLights;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

uniform ShadowCaster shadowCasters[MAX_SHADOW_CASTERS];
uniform int numShadowCasters;
uniform bool receiveShadows;

uniform bool portalMode;
uniform bool portalDepthOnly;
uniform sampler2D portalTex;

void main() {
    if (portalMode) {
        if (portalDepthOnly) {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
            return;
        }
        vec2 uv = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;
        gl_FragColor = texture2D(portalTex, uv);
        return;
    }

    vec4 texColor = texture2D(tex, vTexCoord);
    if (texColor.a < 0.01) discard; 

    vec3 N = normalize(vN);
    vec3 totalLight = ambientLight;

    // Исправлено: замена min(int, int) на безопасный цикл
    int activeLightsCount = numLights;
    if (activeLightsCount > MAX_LIGHTS) activeLightsCount = MAX_LIGHTS;

    for (int i = 0; i < activeLightsCount; i++) {
        if (!lights[i].enabled) continue;

        vec3 L_vec = lights[i].position - vP;
        float distSq = dot(L_vec, L_vec);
        float dist = sqrt(distSq);
        vec3 L = L_vec / dist;

        vec3 D = normalize(lights[i].direction);
        float cosTheta = dot(-L, D);
        if (cosTheta < lights[i].cutoff) continue;

        float att = 1.0 / (lights[i].attenuation.x +
                           lights[i].attenuation.y * dist +
                           lights[i].attenuation.z * distSq);

        float diff = max(dot(N, L), 0.0);
        totalLight += lights[i].diffuse * diff * att;
    }

    vec3 finalColor = texColor.rgb * vColor.rgb * totalLight;

    if (receiveShadows && numShadowCasters > 0) {
        // Исправлено: замена min(int, int) для теней
        int activeShadows = numShadowCasters;
        if (activeShadows > MAX_SHADOW_CASTERS) activeShadows = MAX_SHADOW_CASTERS;

        for (int s = 0; s < activeShadows; s++) {
            if (shadowCasters[s].darkness <= 0.0) continue;

            vec3 fragToLight = shadowCasters[s].lightPos - vP;
            vec3 Ldir = normalize(fragToLight);
            float cosTheta = dot(-Ldir, normalize(shadowCasters[s].lightDirection));
            if (cosTheta < shadowCasters[s].lightCutoff) continue;

            vec4 shadowCoord = shadowCasters[s].shadowMatrix * vec4(vP, 1.0);
            vec3 proj = shadowCoord.xyz / shadowCoord.w;
            proj.y = 1.0 - proj.y;

            const float zNear = 0.1;
            const float zFar  = 1000.0;
            float objDepthNorm = (shadowCasters[s].lightObjDist - zNear) / (zFar - zNear);
            float bias = clamp(0.0001 * (1.0 - abs(cosTheta)), 0.00001, 0.001);

            if (proj.z > objDepthNorm + bias) {
                if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0) {
                    vec4 shadowTex = texture2D(shadowCasters[s].shadowMap, proj.xy);
                    if (shadowTex.a > 0.1) {
                        finalColor.rgb *= (1.0 - shadowCasters[s].darkness);
                    }
                }
            }
        }
    }

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
pseudo_3d_entity::pseudo_3d_entity(float x, float y, float z,
                                   float g_angle, float v_angle, float r_angle,
                                   std::vector<std::string> textures, int v_angles,
                                   const std::vector<float>& vertices)
    : x(x), y(y), z(z),
      g_angle(g_angle), v_angle(v_angle), r_angle(r_angle),
      textures(std::move(textures)), v_angles(v_angles),
      vertices_(vertices) {
    computeRadius();
}
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
    const char* tex = (tidx >= 0 && tidx < (int)textures.size()) ? textures[tidx].c_str() : nullptr;

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

    if (currentShaderProg) {
        GLint loc = glGetUniformLocation(currentShaderProg, "receiveShadows");
        if (loc != -1) glUniform1i(loc, 0);
    }

    glPushMatrix();
    glTranslatef(x, y, z);
    glMultMatrixf(mat);
    glRotatef(total_roll + 180.0f, 0, 0, 1);
    light_square(1.0f, 0, 0, 1, 1, 1, mirror ? -180.0f : 0.0f, vertices_.data(), tex);
    glPopMatrix();

    if (currentShaderProg) {
        GLint loc = glGetUniformLocation(currentShaderProg, "receiveShadows");
        if (loc != -1) glUniform1i(loc, 1);
    }
}

void pseudo_3d_entity::computeRadius() {
    float maxDist = 0.0f;
    for (size_t i = 0; i < vertices_.size(); i += 2) {
        float vx = vertices_[i];
        float vy = vertices_[i+1];
        float dist = sqrtf(vx*vx + vy*vy);
        if (dist > maxDist) maxDist = dist;
    }
    radius = maxDist;
}

void pseudo_3d_entity::setCastShadow(bool enable) {
    if (enable == _castsShadow) return;
    _castsShadow = enable;
    if (enable) {
        shadowCasters.push_back(this);
    } else {
        auto it = std::find(shadowCasters.begin(), shadowCasters.end(), this);
        if (it != shadowCasters.end()) shadowCasters.erase(it);
    }
}

GLuint pseudo_3d_entity::getTextureFromDirection(float lx, float ly, float lz) const {
    int idx = getTextureIndex(lx, ly, lz);
    if (idx < 0 || idx >= (int)textures.size()) return 0;
    return loadTextureFromFile(textures[idx].c_str());
}

GLuint pseudo_3d_entity::getShadowTexture(float lx, float ly, float lz) const {
    int idx = getTextureIndex(lx, ly, lz);
    if (idx < 0 || idx >= (int)textures.size()) return 0;
    return loadTextureFromFile(textures[idx].c_str());
}

std::vector<pseudo_3d_entity*> shadowCasters;

void applyAllShadows() {
    GLuint prog = currentShaderProg;
    if (!prog) return;

    const int MAX_SHADOW_CASTERS = 8;

    std::vector<std::tuple<float, pseudo_3d_entity*, Light*>> casters;
    for (pseudo_3d_entity* ent : shadowCasters) {
        if (!ent || !ent->castsShadow()) continue;
        float dx = ent->getX() - camera.eye_x;
        float dy = ent->getY() - camera.eye_y;
        float dz = ent->getZ() - camera.eye_z;
        float distSq = dx*dx + dy*dy + dz*dz;

        for (Light* light : activeLights) {
            if (!light->isEnabled()) continue;

            float toObj[3] = {
                ent->getX() - light->pos[0],
                ent->getY() - light->pos[1],
                ent->getZ() - light->pos[2]
            };
            float distToObj = sqrtf(toObj[0]*toObj[0] + toObj[1]*toObj[1] + toObj[2]*toObj[2]);
            if (distToObj < 0.001f) continue;
            float dirDot = (toObj[0]*light->dir[0] + toObj[1]*light->dir[1] + toObj[2]*light->dir[2]) / distToObj;
            float cutoffCos = cosf(light->cutoff * M_PI / 180.0f);
            if (dirDot < cutoffCos) continue;

            casters.emplace_back(distSq, ent, light);
        }
    }

    std::sort(casters.begin(), casters.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

    int totalCasters = std::min((int)casters.size(), MAX_SHADOW_CASTERS);
    glUniform1i(glGetUniformLocation(prog, "numShadowCasters"), totalCasters);

    if (totalCasters == 0) {
        for (int i = 0; i < MAX_SHADOW_CASTERS; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "shadowCasters[%d].darkness", i);
            glUniform1f(glGetUniformLocation(prog, buf), 0.0f);
        }
        return;
    }

    GLfloat cameraView[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, cameraView);

    float worldUpEye[3] = { cameraView[4], cameraView[5], cameraView[6] };

    auto worldToCamera = [&](float x, float y, float z) {
        float w = cameraView[3]*x + cameraView[7]*y + cameraView[11]*z + cameraView[15];
        return std::tuple<float,float,float>(
            (cameraView[0]*x + cameraView[4]*y + cameraView[8]*z + cameraView[12]) / w,
            (cameraView[1]*x + cameraView[5]*y + cameraView[9]*z + cameraView[13]) / w,
            (cameraView[2]*x + cameraView[6]*y + cameraView[10]*z + cameraView[14]) / w
        );
    };

    for (int i = 0; i < totalCasters; ++i) {
        auto [distSq, ent, light] = casters[i];

        auto [lightCamX, lightCamY, lightCamZ] = worldToCamera(light->pos[0], light->pos[1], light->pos[2]);
        auto [entCamX, entCamY, entCamZ]     = worldToCamera(ent->getX(), ent->getY(), ent->getZ());

        float toObjX = entCamX - lightCamX;
        float toObjY = entCamY - lightCamY;
        float toObjZ = entCamZ - lightCamZ;
        float objDist = sqrtf(toObjX*toObjX + toObjY*toObjY + toObjZ*toObjZ);
        if (objDist > 0.001f) {
            toObjX /= objDist;
            toObjY /= objDist;
            toObjZ /= objDist;
        }

        float lightToEnt[3] = { light->pos[0] - ent->getX(), light->pos[1] - ent->getY(), light->pos[2] - ent->getZ() };
        GLuint texID = ent->getShadowTexture(lightToEnt[0], lightToEnt[1], lightToEnt[2]);
        if (!texID) continue;

        float projSize = ent->getRadius() * 0.9;
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(-projSize, projSize, -projSize, projSize, 0.1f, 1000.0f);
        float projMat[16];
        glGetFloatv(GL_PROJECTION_MATRIX, projMat);
        glPopMatrix();

        float bias = 0.2;
        float eyeX = lightCamX + toObjX * bias;
        float eyeY = lightCamY + toObjY * bias;
        float eyeZ = lightCamZ + toObjZ * bias;

        float upX = worldUpEye[0], upY = worldUpEye[1], upZ = worldUpEye[2];
        float dotUp = toObjX*upX + toObjY*upY + toObjZ*upZ;
        if (fabsf(dotUp) > 0.999f) {
            upX = -toObjZ; upY = 0.0f; upZ = toObjX;
            float lenUp = sqrtf(upX*upX + upZ*upZ);
            if (lenUp > 0.001f) { upX /= lenUp; upZ /= lenUp; }
        }

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        gluLookAt(eyeX, eyeY, eyeZ,
                  entCamX, entCamY, entCamZ,
                  upX, upY, upZ);
        float viewMat[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, viewMat);
        glPopMatrix();

        float biasMat[16] = {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
            0.5f, 0.5f, 0.5f, 1.0f
        };
        float shadowMat[16];
        glPushMatrix();
        glLoadMatrixf(biasMat);
        glMultMatrixf(projMat);
        glMultMatrixf(viewMat);
        glGetFloatv(GL_MODELVIEW_MATRIX, shadowMat);
        glPopMatrix();

        char buf[64];

        snprintf(buf, sizeof(buf), "shadowCasters[%d].shadowMatrix", i);
        glUniformMatrix4fv(glGetUniformLocation(prog, buf), 1, GL_FALSE, shadowMat);

        snprintf(buf, sizeof(buf), "shadowCasters[%d].darkness", i);
        glUniform1f(glGetUniformLocation(prog, buf), 0.8f);

        GLfloat lightPos[3] = { lightCamX, lightCamY, lightCamZ };
        snprintf(buf, sizeof(buf), "shadowCasters[%d].lightPos", i);
        glUniform3fv(glGetUniformLocation(prog, buf), 1, lightPos);

        float lightDirView[3];
        for (int k = 0; k < 3; ++k) {
            lightDirView[k] = cameraView[0+k] * light->dir[0] +
                              cameraView[4+k] * light->dir[1] +
                              cameraView[8+k] * light->dir[2];
        }
        snprintf(buf, sizeof(buf), "shadowCasters[%d].lightDirection", i);
        glUniform3fv(glGetUniformLocation(prog, buf), 1, lightDirView);

        snprintf(buf, sizeof(buf), "shadowCasters[%d].lightCutoff", i);
        glUniform1f(glGetUniformLocation(prog, buf), cosf(light->cutoff * M_PI / 180.0f));

        snprintf(buf, sizeof(buf), "shadowCasters[%d].lightObjDist", i);
        glUniform1f(glGetUniformLocation(prog, buf), objDist);

        glActiveTexture(GL_TEXTURE1 + i);
        bindTexture(texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        snprintf(buf, sizeof(buf), "shadowCasters[%d].shadowMap", i);
        glUniform1i(glGetUniformLocation(prog, buf), 1 + i);
    }

    for (int i = totalCasters; i < MAX_SHADOW_CASTERS; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "shadowCasters[%d].darkness", i);
        glUniform1f(glGetUniformLocation(prog, buf), 0.0f);
    }

    glActiveTexture(GL_TEXTURE0);
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
    // glutInit(argc,argv);
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

    global_pitch = pitch;
    global_yaw = yaw;
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

    global_pitch = pitch;
    global_yaw = yaw;
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
        useShader(currentShaderProg);
        lighting_global = true;
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
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
    global_ambient[0] = r;
    global_ambient[1] = g;
    global_ambient[2] = b;
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
}

void disable_fog(){
    fog.enabled = false;
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
        useShader(currentShaderProg);
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
// панорама
sphere_panorama sphere_sky;
static GLuint skybox_list = 0; 
void set_panorama(const char* path) {
    if (sphere_sky.enabled) remove_panorama();
    sphere_sky.texture = SOIL_load_OGL_texture(
        path,
        SOIL_LOAD_AUTO,
        SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT
    );

    if (sphere_sky.texture == 0) {
        printf("ERROR: Could not load texture from %s. Reason: %s\n", path, SOIL_last_result());
        system("pwd"); 
        return;
    }

    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);

    skybox_list = glGenLists(1);
    glNewList(skybox_list, GL_COMPILE);
        float radius = 180.0f;
        int stacks = 32, slices = 32;
        for (int i = 0; i < stacks; i++) {
            float lat0 = M_PI * (-0.5f + (float)i / stacks);
            float z0 = sin(lat0), zr0 = cos(lat0);
            float lat1 = M_PI * (-0.5f + (float)(i + 1) / stacks);
            float z1 = sin(lat1), zr1 = cos(lat1);
            glBegin(GL_QUAD_STRIP);
            for (int j = 0; j <= slices; j++) {
                float lng = 2 * M_PI * (float)j / slices;
                float x = cos(lng), y = sin(lng);
                glTexCoord2f((float)j / slices, (float)i / stacks);
                glVertex3f(x * zr0 * radius, y * zr0 * radius, z0 * radius);
                glTexCoord2f((float)j / slices, (float)(i + 1) / stacks);
                glVertex3f(x * zr1 * radius, y * zr1 * radius, z1 * radius);
            }
            glEnd();
        }
    glEndList();
    sphere_sky.enabled = true;
    sphere_sky.path = path;
    printf("Panorama loaded successfully: %s\n", path);
}
void remove_panorama(){
    if (sphere_sky.enabled) {
        glDeleteTextures(1, &sphere_sky.texture);
        glDeleteLists(skybox_list, 1);
        sphere_sky.enabled = false;
    }
}
void draw_panorama(float camX, float camY, float camZ){
    if (!sphere_sky.enabled || sphere_sky.texture == 0) return;
    
    GLuint prevShader = currentShaderProg;
    if (prevShader) stopShader();

    glPushAttrib(GL_ALL_ATTRIB_BITS); 
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);         
    glDepthMask(GL_FALSE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glColor4f(1, 1, 1, 1);     
    glPushMatrix();
    glTranslatef(camX, camY, camZ);
    glRotatef(90, 1, 0, 0); 
    glCallList(skybox_list); 
    glPopMatrix();
    glPopAttrib();

    if (prevShader) useShader(prevShader);
}
// карта
static inline void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}

static inline void write_float(std::vector<uint8_t>& buf, float v) {
    uint32_t tmp;
    memcpy(&tmp, &v, 4);
    write_u32(buf, tmp);
}

static inline void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    write_u32(buf, (uint32_t)s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

static inline bool read_u32(const uint8_t*& data, size_t& remaining, uint32_t& out) {
    if (remaining < 4) return false;
    out = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    data += 4; remaining -= 4;
    return true;
}

static inline bool read_float(const uint8_t*& data, size_t& remaining, float& out) {
    uint32_t tmp;
    if (!read_u32(data, remaining, tmp)) return false;
    memcpy(&out, &tmp, 4);
    return true;
}

static inline bool read_string(const uint8_t*& data, size_t& remaining, std::string& out) {
    uint32_t len;
    if (!read_u32(data, remaining, len)) return false;
    if (len > remaining) return false;
    out.assign((const char*)data, len);
    data += len; remaining -= len;
    return true;
}

enum class ChunkType : uint32_t {
    ENTY = 0x59544E45,
    LITE = 0x4554494C,
    FOGS = 0x53474F46,
    CAME = 0x454D4143,
    PANO = 0x4F4E4150,
    AMBI = 0x49424D41,
    USER = 0x52455355
};

static void write_chunk(std::vector<uint8_t>& out, ChunkType type, const std::vector<uint8_t>& data) {
    uint32_t fourcc = static_cast<uint32_t>(type);
    out.push_back(fourcc & 0xFF);
    out.push_back((fourcc >> 8) & 0xFF);
    out.push_back((fourcc >> 16) & 0xFF);
    out.push_back((fourcc >> 24) & 0xFF);
    write_u32(out, (uint32_t)data.size());
    out.insert(out.end(), data.begin(), data.end());
}

static bool load_map_internal(const std::vector<uint8_t>& filedata, MapData& map) {
    const uint8_t* p = filedata.data();
    size_t remaining = filedata.size();

    if (remaining < 4) return false;
    if (memcmp(p, "AVOM", 4) != 0) return false;
    p += 4; remaining -= 4;

    if (remaining < 4) return false;
    uint16_t ver_major = p[0] | (p[1] << 8);
    uint16_t ver_minor = p[2] | (p[3] << 8);
    p += 4; remaining -= 4;

    if (ver_major != 1) {
        std::cerr << "Unsupported .avomap major version " << ver_major << std::endl;
        return false;
    }

    while (remaining >= 8) {
        uint32_t fourcc;
        memcpy(&fourcc, p, 4);
        p += 4; remaining -= 4;

        uint32_t chunkSize;
        if (!read_u32(p, remaining, chunkSize)) return false;
        if (chunkSize > remaining) return false;

        const uint8_t* chunkData = p;
        p += chunkSize;
        remaining -= chunkSize;

        ChunkType ctype = static_cast<ChunkType>(fourcc);

        switch (ctype) {
            case ChunkType::ENTY: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                MapEntity ent;
                if (!read_float(d, r, ent.x) || !read_float(d, r, ent.y) || !read_float(d, r, ent.z)) break;
                if (!read_float(d, r, ent.g_angle) || !read_float(d, r, ent.v_angle) || !read_float(d, r, ent.r_angle)) break;
                if (!read_u32(d, r, (uint32_t&)ent.v_angles)) break;
                uint32_t texCount;
                if (!read_u32(d, r, texCount)) break;
                ent.textures.resize(texCount);
                for (auto& tex : ent.textures) {
                    if (!read_string(d, r, tex)) break;
                }
                uint32_t vertCount;
                if (!read_u32(d, r, vertCount)) break;
                ent.vertices.resize(vertCount);
                for (auto& v : ent.vertices) {
                    if (!read_float(d, r, v)) break;
                }
                if (d <= p) {
                    uint8_t shadowByte = 0;
                    if (r > 0) {
                        shadowByte = d[0];
                        d++; r--;
                    }
                    ent.castShadow = (shadowByte != 0);
                }
                map.entities.push_back(ent);
                break;
            }
            case ChunkType::LITE: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                MapData::LightData light;
                uint32_t enabled32;
                if (!read_u32(d, r, enabled32)) break;
                light.enabled = (enabled32 != 0);
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.pos[i])) break;
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.dir[i])) break;
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.color[i])) break;
                if (!read_float(d, r, light.intensity)) break;
                if (!read_float(d, r, light.cutoff)) break;
                if (!read_float(d, r, light.constAtt)) break;
                if (!read_float(d, r, light.linearAtt)) break;
                if (!read_float(d, r, light.quadAtt)) break;
                map.lights.push_back(light);
                break;
            }
            case ChunkType::FOGS: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                uint32_t enabled32;
                if (!read_u32(d, r, enabled32)) break;
                map.fog_enabled = (enabled32 != 0);
                if (!read_float(d, r, map.fog_density)) break;
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.fog_color[i])) break;
                if (!read_float(d, r, map.fog_start)) break;
                if (!read_float(d, r, map.fog_end)) break;
                break;
            }
            case ChunkType::CAME: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.camera_eye[i])) break;
                if (!read_float(d, r, map.camera_pitch)) break;
                if (!read_float(d, r, map.camera_yaw)) break;
                break;
            }
            case ChunkType::PANO: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                if (!read_string(d, r, map.panorama_path)) break;
                break;
            }
            case ChunkType::AMBI: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.ambient[i])) break;
                break;
            }
            case ChunkType::USER: {
                if (chunkSize < 1) break;
                uint8_t ver = chunkData[0];
                if (ver != 0) break;
                const uint8_t* d = chunkData + 1;
                size_t r = chunkSize - 1;
                uint32_t numEntries;
                if (!read_u32(d, r, numEntries)) break;
                for (uint32_t i = 0; i < numEntries; ++i) {
                    std::string key;
                    if (!read_string(d, r, key)) break;
                    uint32_t dataLen;
                    if (!read_u32(d, r, dataLen)) break;
                    if (dataLen > r) break;
                    std::vector<uint8_t> blob(d, d + dataLen);
                    d += dataLen; r -= dataLen;
                    map.userData[key] = std::move(blob);
                }
                break;
            }
            default:
                break;
        }
    }
    return true;
}

bool save_map(const char* filename, const MapData& map) {
    std::vector<uint8_t> out;

    out.push_back('A'); out.push_back('V'); out.push_back('O'); out.push_back('M');
    out.push_back(1); out.push_back(0);      // major = 1 (little‑endian)
    out.push_back(0); out.push_back(0);

    for (const auto& ent : map.entities) {
        std::vector<uint8_t> data;
        data.push_back(0);
        write_float(data, ent.x);
        write_float(data, ent.y);
        write_float(data, ent.z);
        write_float(data, ent.g_angle);
        write_float(data, ent.v_angle);
        write_float(data, ent.r_angle);
        write_u32(data, ent.v_angles);
        write_u32(data, (uint32_t)ent.textures.size());
        for (const auto& t : ent.textures) write_string(data, t);
        write_u32(data, (uint32_t)ent.vertices.size());
        for (float v : ent.vertices) write_float(data, v);
        data.push_back(ent.castShadow ? 1 : 0);
        write_chunk(out, ChunkType::ENTY, data);
    }

    for (const auto& light : map.lights) {
        std::vector<uint8_t> data;
        data.push_back(0);
        write_u32(data, light.enabled ? 1 : 0);
        for (int i = 0; i < 3; ++i) write_float(data, light.pos[i]);
        for (int i = 0; i < 3; ++i) write_float(data, light.dir[i]);
        for (int i = 0; i < 3; ++i) write_float(data, light.color[i]);
        write_float(data, light.intensity);
        write_float(data, light.cutoff);
        write_float(data, light.constAtt);
        write_float(data, light.linearAtt);
        write_float(data, light.quadAtt);
        write_chunk(out, ChunkType::LITE, data);
    }

    {
        std::vector<uint8_t> data;
        data.push_back(0);
        write_u32(data, map.fog_enabled ? 1 : 0);
        write_float(data, map.fog_density);
        for (int i = 0; i < 3; ++i) write_float(data, map.fog_color[i]);
        write_float(data, map.fog_start);
        write_float(data, map.fog_end);
        write_chunk(out, ChunkType::FOGS, data);
    }

    {
        std::vector<uint8_t> data;
        data.push_back(0);
        for (int i = 0; i < 3; ++i) write_float(data, map.camera_eye[i]);
        write_float(data, map.camera_pitch);
        write_float(data, map.camera_yaw);
        write_chunk(out, ChunkType::CAME, data);
    }

    if (!map.panorama_path.empty()) {
        std::vector<uint8_t> data;
        data.push_back(0);
        write_string(data, map.panorama_path);
        write_chunk(out, ChunkType::PANO, data);
    }

    {
        std::vector<uint8_t> data;
        data.push_back(0);
        for (int i = 0; i < 3; ++i) write_float(data, map.ambient[i]);
        write_chunk(out, ChunkType::AMBI, data);
    }

    if (!map.userData.empty()) {
        std::vector<uint8_t> data;
        data.push_back(0);
        write_u32(data, (uint32_t)map.userData.size());
        for (const auto& [key, blob] : map.userData) {
            write_string(data, key);
            write_u32(data, (uint32_t)blob.size());
            data.insert(data.end(), blob.begin(), blob.end());
        }
        write_chunk(out, ChunkType::USER, data);
    }

    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}

bool load_map(const char* filename, MapData& map) {
    FILE* f = fopen(filename, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);
    return load_map_internal(data, map);
}

MapEntity entityToMapData(const pseudo_3d_entity& ent) {
    MapEntity me;
    me.x = ent.getX();
    me.y = ent.getY();
    me.z = ent.getZ();
    me.g_angle = ent.getGAngle();
    me.v_angle = ent.getVAngle();
    me.r_angle = ent.getRAngle();
    me.v_angles = ent.getVAngles();
    me.textures = ent.getTextures();
    me.vertices = ent.getVertices();
    me.castShadow = ent.castsShadow();
    return me;
}

pseudo_3d_entity* mapDataToEntity(const MapEntity& data) {
    return new pseudo_3d_entity(data.x, data.y, data.z,
                                data.g_angle, data.v_angle, data.r_angle,
                                data.textures, data.v_angles, data.vertices);
}

MapData::LightData lightToMapData(const Light& light) {
    MapData::LightData l;
    l.enabled = light.isEnabled();
    memcpy(l.pos, light.pos, sizeof(l.pos));
    memcpy(l.dir, light.dir, sizeof(l.dir));
    memcpy(l.color, light.color, sizeof(l.color));
    l.intensity = light.intensity;
    l.cutoff = light.cutoff;
    l.constAtt = light.constAtt;
    l.linearAtt = light.linearAtt;
    l.quadAtt = light.quadAtt;
    return l;
}

void mapDataToLight(const MapData::LightData& data, Light& out) {
    out.setPosition(data.pos[0], data.pos[1], data.pos[2]);
    memcpy(out.dir, data.dir, sizeof(out.dir));
    out.setColor(data.color[0], data.color[1], data.color[2]);
    out.setIntensity(data.intensity);
    out.setRadius(data.cutoff);
    out.setAttenuation(data.constAtt, data.linearAtt, data.quadAtt);
    if (data.enabled) out.enable(); else out.disable();
}

void registerEntity(pseudo_3d_entity* e) {
    if (std::find(allEntities.begin(), allEntities.end(), e) == allEntities.end()) {
        allEntities.push_back(e);
    }
}

void unregisterEntity(pseudo_3d_entity* e) {
    auto it = std::find(allEntities.begin(), allEntities.end(), e);
    if (it != allEntities.end()) allEntities.erase(it);
}

void save_current_scene(const char* filename) {
    MapData map;

    for (auto* e : allEntities) {
        map.entities.push_back(entityToMapData(*e));
    }

    for (auto* l : activeLights) {
        map.lights.push_back(lightToMapData(*l));
    }

    map.fog_enabled = fog.enabled;
    map.fog_density = fog.density;
    memcpy(map.fog_color, fog.color, sizeof(fog.color));
    map.fog_start = fog.start;
    map.fog_end = fog.end;

    map.camera_eye[0] = camera.eye_x;
    map.camera_eye[1] = camera.eye_y;
    map.camera_eye[2] = camera.eye_z;
    map.camera_pitch = global_pitch;
    map.camera_yaw = global_yaw;

    extern sphere_panorama sphere_sky;
    if (sphere_sky.enabled) {
        map.panorama_path = sphere_sky.path;
    }

    memcpy(map.ambient, global_ambient, sizeof(global_ambient));

    save_map(filename, map);
}
Portal::Portal(float ax, float ay, float az,
               float bx, float by, float bz,
               const std::vector<float>& verts,
               float yawA, float pitchA, float rollA,
               float yawB, float pitchB, float rollB)
    : ax(ax), ay(ay), az(az)
    , bx(bx), by(by), bz(bz)
    , vertices(verts)
    , yawA(yawA), pitchA(pitchA), rollA(rollA)
    , yawB(yawB), pitchB(pitchB), rollB(rollB)
{
    initFBOs(window_w > 0 ? window_w : 800, window_h > 0 ? window_h : 600);
}

Portal::~Portal() {
    destroyFBOs();
}

void Portal::setSceneDrawCallback(std::function<void()> cb) {
    sceneDraw = cb;
}

void Portal::initFBOs(int w, int h) {
    auto createFBO = [](FBO& fb, int w, int h) {
        fb.w = w; fb.h = h;

        glGenTextures(1, &fb.colorTex);
        glBindTexture(GL_TEXTURE_2D, fb.colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &fb.depthTex);
        glBindTexture(GL_TEXTURE_2D, fb.depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fb.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.colorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, fb.depthTex, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            cerr << "Portal FBO incomplete\n";

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    createFBO(fboA, w, h);
    createFBO(fboB, w, h);
}

void Portal::destroyFBOs() {
    auto del = [](FBO& fb) {
        if (fb.fbo)      { glDeleteFramebuffers(1, &fb.fbo);  fb.fbo = 0; }
        if (fb.colorTex) { glDeleteTextures(1, &fb.colorTex); fb.colorTex = 0; }
        if (fb.depthTex) { glDeleteTextures(1, &fb.depthTex); fb.depthTex = 0; }
    };
    del(fboA); del(fboB);
}

void Portal::resizeFBOs(int w, int h) {
    if (fboA.w == w && fboA.h == h) return;
    destroyFBOs();
    initFBOs(w, h);
}

glm::vec3 Portal::portalNormal(float px, float py, float pz, bool sideB) const {
    glm::vec3 localNormal(0.0f, 0.0f, -1.0f);

    float yaw   = sideB ? yawB   : yawA;
    float pitch = sideB ? pitchB : pitchA;
    float roll  = sideB ? rollB  : rollA;

    glm::mat4 rot4 = glm::mat4(1.0f);
    rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat3 rot = glm::mat3(rot4);

    return rot * localNormal;
}

bool Portal::isFrontFacing(float px, float py, float pz,
                            float cam_x, float cam_y, float cam_z) const {
    return true;
}

glm::mat4 Portal::getPortalTransform(float fx, float fy, float fz,
                                      float tx, float ty, float tz) const {
    int n = (int)vertices.size() / 3;
    glm::vec3 centerA(0,0,0), centerB(0,0,0);
    for (int i = 0; i < n; i++) {
        centerA += glm::vec3(fx + vertices[i*3], fy + vertices[i*3+1], fz + vertices[i*3+2]);
        centerB += glm::vec3(tx + vertices[i*3], ty + vertices[i*3+1], tz + vertices[i*3+2]);
    }
    centerA /= (float)n;
    centerB /= (float)n;

    glm::vec3 nA = portalNormal(fx, fy, fz, false); 
    glm::vec3 nB = portalNormal(tx, ty, tz, true);  

    auto makeBasis = [](float yaw, float pitch, float roll) {
        glm::mat4 rot4 = glm::mat4(1.0f);
        rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::mat3(rot4);
    };

    glm::mat3 rotA = makeBasis(yawA, pitchA, rollA);
    glm::mat3 rotB = makeBasis(yawB, pitchB, rollB);

    glm::mat4 fromA(1.0f);
    fromA[0] = glm::vec4(rotA[0], 0.0f);
    fromA[1] = glm::vec4(rotA[1], 0.0f);
    fromA[2] = glm::vec4(rotA[2], 0.0f);
    fromA[3] = glm::vec4(centerA, 1.0f);

    glm::mat4 toB(1.0f);
    toB[0] = glm::vec4(rotB[0], 0.0f);
    toB[1] = glm::vec4(rotB[1], 0.0f);
    toB[2] = glm::vec4(rotB[2], 0.0f);
    toB[3] = glm::vec4(centerB, 1.0f);

    return toB * glm::inverse(fromA);
}

static void portalSetUniforms(GLuint prog, GLuint tex, bool depthOnly) {
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "portalMode"),      1);
    glUniform1i(glGetUniformLocation(prog, "portalDepthOnly"), depthOnly ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog, "portalTex"),       1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(GL_TEXTURE0);
}

static void portalClearUniforms(GLuint prog) {
    glUniform1i(glGetUniformLocation(prog, "portalMode"),      0);
    glUniform1i(glGetUniformLocation(prog, "portalDepthOnly"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void Portal::drawPortalSurface(float px, float py, float pz, GLuint tex, bool sideB) {
    int n = (int)vertices.size() / 3;
    if (n < 3) return;

    GLuint prog = currentShaderProg;

    glDisable(GL_CULL_FACE);

    portalSetUniforms(prog, tex, false);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);

    glPushMatrix();
    glTranslatef(px, py, pz);
    if (sideB) {
        glRotatef(yawB,   0.0f, 1.0f, 0.0f);
        glRotatef(pitchB, 1.0f, 0.0f, 0.0f);
        glRotatef(rollB,  0.0f, 0.0f, 1.0f);
    } else {
        glRotatef(yawA,   0.0f, 1.0f, 0.0f);
        glRotatef(pitchA, 1.0f, 0.0f, 0.0f);
        glRotatef(rollA,  0.0f, 0.0f, 1.0f);
    }

    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < n; i++)
        glVertex3f(vertices[i*3], vertices[i*3+1], vertices[i*3+2]);
    glEnd();
    glPopMatrix();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    portalClearUniforms(prog);

    portalSetUniforms(prog, 0, true);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);

    glPushMatrix();
    glTranslatef(px, py, pz);
    if (sideB) {
        glRotatef(yawB,   0.0f, 1.0f, 0.0f);
        glRotatef(pitchB, 1.0f, 0.0f, 0.0f);
        glRotatef(rollB,  0.0f, 0.0f, 1.0f);
    } else {
        glRotatef(yawA,   0.0f, 1.0f, 0.0f);
        glRotatef(pitchA, 1.0f, 0.0f, 0.0f);
        glRotatef(rollA,  0.0f, 0.0f, 1.0f);
    }

    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < n; i++)
        glVertex3f(vertices[i*3], vertices[i*3+1], vertices[i*3+2]);
    glEnd();
    glPopMatrix();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    portalClearUniforms(prog);

    glEnable(GL_CULL_FACE);
}

void Portal::renderThroughPortal(float src_x, float src_y, float src_z,
                                  float dst_x, float dst_y, float dst_z,
                                  int depth, bool drawingA) {
    if (!sceneDraw) return;

    FBO& fbo = drawingA ? fboA : fboB;
    resizeFBOs(window_w > 0 ? window_w : 800, window_h > 0 ? window_h : 600);

    glm::mat4 portalMat = getPortalTransform(src_x, src_y, src_z, dst_x, dst_y, dst_z);

    glm::vec3 camPos = glm::vec3(portalMat * glm::vec4(camera.eye_x, camera.eye_y, camera.eye_z, 1.0f));

    glm::mat3 rotB = glm::mat3(portalMat);

    glm::vec3 newDir = glm::normalize(glm::inverse(rotB) * glm::vec3(camera.dir_x, camera.dir_y, camera.dir_z));

    float new_pitch = glm::degrees(asinf(newDir.y));
    float new_yaw   = glm::degrees(atan2f(newDir.x, newDir.z));

    glm::vec3 dstNorm = portalNormal(dst_x, dst_y, dst_z, !drawingA);
    int n = vertices.size() / 3;
    glm::vec3 dstCenter(0,0,0);
    for (int i = 0; i < n; i++)
        dstCenter += glm::vec3(dst_x + vertices[i*3], dst_y + vertices[i*3+1], dst_z + vertices[i*3+2]);
    dstCenter /= (float)n;

    GLdouble clipPlane[4] = {
        (GLdouble)dstNorm.x,
        (GLdouble)dstNorm.y,
        (GLdouble)dstNorm.z,
        -(GLdouble)glm::dot(dstNorm, dstCenter)
    };

    float savedEyeX = camera.eye_x, savedEyeY = camera.eye_y, savedEyeZ = camera.eye_z;
    float savedCtrX = camera.ctr_x, savedCtrY = camera.ctr_y, savedCtrZ = camera.ctr_z;
    float savedDirX = camera.dir_x, savedDirY = camera.dir_y, savedDirZ = camera.dir_z;
    float savedUpX = camera.up_x, savedUpY = camera.up_y, savedUpZ = camera.up_z;
    float savedPitch = global_pitch, savedYaw = global_yaw;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glViewport(0, 0, fbo.w, fbo.h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera.eye_x = camPos.x; camera.eye_y = camPos.y; camera.eye_z = camPos.z;
    camera.dir_x = newDir.x; camera.dir_y = newDir.y; camera.dir_z = newDir.z;
    camera.ctr_x = camPos.x + newDir.x;
    camera.ctr_y = camPos.y + newDir.y;
    camera.ctr_z = camPos.z + newDir.z;
    global_pitch = new_pitch;
    global_yaw = new_yaw;

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    gluLookAt(camPos.x, camPos.y, camPos.z,
              camera.ctr_x, camera.ctr_y, camera.ctr_z,
              camera.up_x, camera.up_y, camera.up_z);

    glEnable(GL_CLIP_PLANE0);
    glClipPlane(GL_CLIP_PLANE0, clipPlane);

    sceneDraw();

    glDisable(GL_CLIP_PLANE0);
    glPopMatrix();

    camera.eye_x = savedEyeX; camera.eye_y = savedEyeY; camera.eye_z = savedEyeZ;
    camera.ctr_x = savedCtrX; camera.ctr_y = savedCtrY; camera.ctr_z = savedCtrZ;
    camera.dir_x = savedDirX; camera.dir_y = savedDirY; camera.dir_z = savedDirZ;
    camera.up_x = savedUpX; camera.up_y = savedUpY; camera.up_z = savedUpZ;
    global_pitch = savedPitch; global_yaw = savedYaw;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(savedEyeX, savedEyeY, savedEyeZ,
              savedEyeX + savedDirX, savedEyeY + savedDirY, savedEyeZ + savedDirZ,
              savedUpX, savedUpY, savedUpZ);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
}

void Portal::draw(int recursion_depth) {
    resizeFBOs(window_w > 0 ? window_w : 800, window_h > 0 ? window_h : 600);

    renderThroughPortal(ax, ay, az, bx, by, bz, recursion_depth, false);
    drawPortalSurface(ax, ay, az, fboB.colorTex, false);

    renderThroughPortal(bx, by, bz, ax, ay, az, recursion_depth, true);
    drawPortalSurface(bx, by, bz, fboA.colorTex, true);
}
