//              звук
// указываем что здесь реализация библиотеки, т.к. miniaudio это только заголовочный файл и даём понять что
// это главная программа
#define MINIAUDIO_IMPLEMENTATION
// указываем что пользуемся только указанным api для воспроизведения звука(pulseaudio и прочая шняга)(шиндовс или линукс)
#ifdef _WIN32
  #define MA_ENABLE_WASAPI
#else
  #define MA_ENABLE_ALSA
#endif
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
// импортируем сам miniaudio
#include "src/miniaudio.h"

#include <glm/glm.hpp>

#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
#ifdef __linux__
extern "C" {
    int NvOptimusEnablement = 1;
    int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

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
std::vector<Light*> activeLights;

float global_ambient[3] = {0.05f, 0.05f, 0.05f};
std::vector<pseudo_3d_entity*> allEntities;
std::vector<Portal*> allPortals;
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
static bool lighting_global = false;
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

    glBindAttribLocation(ID, 0, "aVertex");
    glBindAttribLocation(ID, 2, "aNormal");
    glBindAttribLocation(ID, 3, "aColor");
    glBindAttribLocation(ID, 8, "aTexCoord");

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
attribute vec4 aVertex;    
attribute vec3 aNormal;   
attribute vec4 aColor;    
attribute vec2 aTexCoord;  

varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec4 vClipPos;

void main() {
    vN = normalize(gl_NormalMatrix * aNormal);
    vec4 mvPos = gl_ModelViewMatrix * aVertex;
    vP = mvPos.xyz;
    vColor = aColor;
    vTexCoord = aTexCoord;
    vWorldPos = aVertex.xyz;   
    gl_Position = gl_ModelViewProjectionMatrix * aVertex;
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

const int MAX_SHADOW_CASTERS = 8;

// кэш uniform-локаций для основного шейдера
static GLint loc_tex = -1;
static GLint loc_numLights = -1;
static GLint loc_ambientLight = -1;
static GLint loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;
static GLint loc_numShadowCasters = -1, loc_receiveShadows = -1;
static GLint loc_portalMode = -1, loc_portalDepthOnly = -1, loc_portalTex = -1;
static GLint loc_lightEnabled[MAX_LIGHTS];
static GLint loc_lightPosition[MAX_LIGHTS];
static GLint loc_lightDirection[MAX_LIGHTS];
static GLint loc_lightDiffuse[MAX_LIGHTS];
static GLint loc_lightCutoff[MAX_LIGHTS];
static GLint loc_lightAttenuation[MAX_LIGHTS];
static GLint loc_shadowMatrix[MAX_SHADOW_CASTERS];
static GLint loc_shadowDarkness[MAX_SHADOW_CASTERS];
static GLint loc_shadowLightPos[MAX_SHADOW_CASTERS];
static GLint loc_shadowLightDir[MAX_SHADOW_CASTERS];
static GLint loc_shadowLightCutoff[MAX_SHADOW_CASTERS];
static GLint loc_shadowLightObjDist[MAX_SHADOW_CASTERS];
static GLint loc_shadowMap[MAX_SHADOW_CASTERS];

static void initDefaultShader() {
    if (defaultLightingShader == 0) {
        defaultLightingShader = createShaderProgram(defaultVertexShader, defaultFragmentShader);
        loc_tex = glGetUniformLocation(defaultLightingShader, "tex");
        loc_numLights = glGetUniformLocation(defaultLightingShader, "numLights");
        loc_ambientLight = glGetUniformLocation(defaultLightingShader, "ambientLight");
        loc_fogColor = glGetUniformLocation(defaultLightingShader, "fogColor");
        loc_fogStart = glGetUniformLocation(defaultLightingShader, "fogStart");
        loc_fogEnd = glGetUniformLocation(defaultLightingShader, "fogEnd");
        loc_numShadowCasters = glGetUniformLocation(defaultLightingShader, "numShadowCasters");
        loc_receiveShadows = glGetUniformLocation(defaultLightingShader, "receiveShadows");
        loc_portalMode = glGetUniformLocation(defaultLightingShader, "portalMode");
        loc_portalDepthOnly = glGetUniformLocation(defaultLightingShader, "portalDepthOnly");
        loc_portalTex = glGetUniformLocation(defaultLightingShader, "portalTex");
        char buf[64];
        for (int i = 0; i < MAX_LIGHTS; ++i) {
            snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
            loc_lightEnabled[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].position", i);
            loc_lightPosition[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].direction", i);
            loc_lightDirection[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
            loc_lightDiffuse[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
            loc_lightCutoff[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
            loc_lightAttenuation[i] = glGetUniformLocation(defaultLightingShader, buf);
        }
        for (int i = 0; i < MAX_SHADOW_CASTERS; ++i) {
            snprintf(buf, sizeof(buf), "shadowCasters[%d].shadowMatrix", i);
            loc_shadowMatrix[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].darkness", i);
            loc_shadowDarkness[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].lightPos", i);
            loc_shadowLightPos[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].lightDirection", i);
            loc_shadowLightDir[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].lightCutoff", i);
            loc_shadowLightCutoff[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].lightObjDist", i);
            loc_shadowLightObjDist[i] = glGetUniformLocation(defaultLightingShader, buf);
            snprintf(buf, sizeof(buf), "shadowCasters[%d].shadowMap", i);
            loc_shadowMap[i] = glGetUniformLocation(defaultLightingShader, buf);
        }
    }
}

static const char* simple2DVertexShader = R"(
#version 120
attribute vec4 aVertex;    
attribute vec4 aColor;     
attribute vec2 aTexCoord;  

varying vec4 vColor;
varying vec2 vTexCoord;

void main() {
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = gl_ModelViewProjectionMatrix * aVertex;
}
)";

static const char* simple2DFragmentShader = R"(
#version 120
varying vec4 vColor;
varying vec2 vTexCoord;
uniform sampler2D tex;
void main() {
    gl_FragColor = texture2D(tex, vTexCoord) * vColor;
}
)";

static GLuint simple2DShader = 0;
static void initSimple2DShader() {
    if (!simple2DShader)
        simple2DShader = createShaderProgram(simple2DVertexShader, simple2DFragmentShader);
}
//              вэбэо
enum DrawCommandType : int {
    CMD_TRIANGLE,
    CMD_SQUARE,
    CMD_TEXT,
    CMD_3DOBJECT,
    CMD_PSEUDO3D
};

struct DrawCommand {
    DrawCommandType type;

    float scale, cx, cy, r, g, b, rotate;
    float verts[8];
    int vertCount;
    std::string tex;

    std::string text;
    float x, y;
    void* font;
    float a;

    float obj_cx, obj_cy, obj_cz;
    float obj_r, obj_g, obj_b;
    std::string obj_tex;
    std::vector<float> obj_vertices;
    std::vector<int> obj_indices;
    std::vector<float> obj_texcoords;
    std::vector<float> obj_normals;
    float radius = 0.0f;             

    const pseudo_3d_entity* entity;
    float cam_x, cam_y, cam_z;

    GLuint shaderID = 0;
};

bool g_useDrawQueue = true;

static std::vector<DrawCommand> drawQueue;
static std::mutex drawQueueMutex;

static GLuint whiteTex = 0;

static void ensureWhiteTex() {
    if (whiteTex == 0) {
        unsigned char white[4] = {255,255,255,255};
        glGenTextures(1, &whiteTex);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// Вспомогательная функция для пересоздания GL-буфера, если его размер меньше требуемого
static GLuint vao3D = 0;
static GLuint vbo_pos = 0, vbo_norm = 0, vbo_uv = 0, ibo = 0;
static size_t cap_pos = 0, cap_norm = 0, cap_uv = 0, cap_idx = 0;

static void ensureBuffer(GLuint &buf, size_t &currentSize, size_t requiredSize, GLenum target) {
    if (requiredSize > currentSize) {
        size_t newSize = std::max(requiredSize, currentSize * 2);
        if (buf) glDeleteBuffers(1, &buf);
        glGenBuffers(1, &buf);
        glBindBuffer(target, buf);
        glBufferData(target, newSize, nullptr, GL_DYNAMIC_DRAW);
        currentSize = newSize;
    } else {
        glBindBuffer(target, buf);
    }
}

static float frustumPlanes[6][4]; 

bool sphereInFrustum(float x, float y, float z, float radius) {
    for (int i = 0; i < 6; i++) {
        float dist = frustumPlanes[i][0] * x +
                     frustumPlanes[i][1] * y +
                     frustumPlanes[i][2] * z +
                     frustumPlanes[i][3];
        if (dist <= -radius)
            return false;
    }
    return true;
} 

void extractFrustumPlanes() {
    float proj[16], modl[16], clip[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, modl);

   
    clip[ 0] = modl[ 0]*proj[ 0] + modl[ 1]*proj[ 4] + modl[ 2]*proj[ 8] + modl[ 3]*proj[12];
    clip[ 1] = modl[ 0]*proj[ 1] + modl[ 1]*proj[ 5] + modl[ 2]*proj[ 9] + modl[ 3]*proj[13];
    clip[ 2] = modl[ 0]*proj[ 2] + modl[ 1]*proj[ 6] + modl[ 2]*proj[10] + modl[ 3]*proj[14];
    clip[ 3] = modl[ 0]*proj[ 3] + modl[ 1]*proj[ 7] + modl[ 2]*proj[11] + modl[ 3]*proj[15];
    clip[ 4] = modl[ 4]*proj[ 0] + modl[ 5]*proj[ 4] + modl[ 6]*proj[ 8] + modl[ 7]*proj[12];
    clip[ 5] = modl[ 4]*proj[ 1] + modl[ 5]*proj[ 5] + modl[ 6]*proj[ 9] + modl[ 7]*proj[13];
    clip[ 6] = modl[ 4]*proj[ 2] + modl[ 5]*proj[ 6] + modl[ 6]*proj[10] + modl[ 7]*proj[14];
    clip[ 7] = modl[ 4]*proj[ 3] + modl[ 5]*proj[ 7] + modl[ 6]*proj[11] + modl[ 7]*proj[15];
    clip[ 8] = modl[ 8]*proj[ 0] + modl[ 9]*proj[ 4] + modl[10]*proj[ 8] + modl[11]*proj[12];
    clip[ 9] = modl[ 8]*proj[ 1] + modl[ 9]*proj[ 5] + modl[10]*proj[ 9] + modl[11]*proj[13];
    clip[10] = modl[ 8]*proj[ 2] + modl[ 9]*proj[ 6] + modl[10]*proj[10] + modl[11]*proj[14];
    clip[11] = modl[ 8]*proj[ 3] + modl[ 9]*proj[ 7] + modl[10]*proj[11] + modl[11]*proj[15];
    clip[12] = modl[12]*proj[ 0] + modl[13]*proj[ 4] + modl[14]*proj[ 8] + modl[15]*proj[12];
    clip[13] = modl[12]*proj[ 1] + modl[13]*proj[ 5] + modl[14]*proj[ 9] + modl[15]*proj[13];
    clip[14] = modl[12]*proj[ 2] + modl[13]*proj[ 6] + modl[14]*proj[10] + modl[15]*proj[14];
    clip[15] = modl[12]*proj[ 3] + modl[13]*proj[ 7] + modl[14]*proj[11] + modl[15]*proj[15];

    
    frustumPlanes[0][0] = clip[ 3] - clip[ 0];
    frustumPlanes[0][1] = clip[ 7] - clip[ 4];
    frustumPlanes[0][2] = clip[11] - clip[ 8];
    frustumPlanes[0][3] = clip[15] - clip[12];
   
    frustumPlanes[1][0] = clip[ 3] + clip[ 0];
    frustumPlanes[1][1] = clip[ 7] + clip[ 4];
    frustumPlanes[1][2] = clip[11] + clip[ 8];
    frustumPlanes[1][3] = clip[15] + clip[12];
   
    frustumPlanes[2][0] = clip[ 3] + clip[ 1];
    frustumPlanes[2][1] = clip[ 7] + clip[ 5];
    frustumPlanes[2][2] = clip[11] + clip[ 9];
    frustumPlanes[2][3] = clip[15] + clip[13];
    
    frustumPlanes[3][0] = clip[ 3] - clip[ 1];
    frustumPlanes[3][1] = clip[ 7] - clip[ 5];
    frustumPlanes[3][2] = clip[11] - clip[ 9];
    frustumPlanes[3][3] = clip[15] - clip[13];
  
    frustumPlanes[4][0] = clip[ 3] - clip[ 2];
    frustumPlanes[4][1] = clip[ 7] - clip[ 6];
    frustumPlanes[4][2] = clip[11] - clip[10];
    frustumPlanes[4][3] = clip[15] - clip[14];
 
    frustumPlanes[5][0] = clip[ 3] + clip[ 2];
    frustumPlanes[5][1] = clip[ 7] + clip[ 6];
    frustumPlanes[5][2] = clip[11] + clip[10];
    frustumPlanes[5][3] = clip[15] + clip[14];

    for (int i = 0; i < 6; i++) {
        float len = sqrtf(frustumPlanes[i][0]*frustumPlanes[i][0] +
                          frustumPlanes[i][1]*frustumPlanes[i][1] +
                          frustumPlanes[i][2]*frustumPlanes[i][2]);
        frustumPlanes[i][0] /= len;
        frustumPlanes[i][1] /= len;
        frustumPlanes[i][2] /= len;
        frustumPlanes[i][3] /= len;
    }
}

static bool currentIs2D = false;

void flushDrawQueue() {
    if (drawQueue.empty()) return;

    extractFrustumPlanes();

    std::sort(drawQueue.begin(), drawQueue.end(), [](const DrawCommand& a, const DrawCommand& b) {
        if (a.type != b.type) return (int)a.type > (int)b.type;
        const char* texA = nullptr, *texB = nullptr;
        switch (a.type) {
            case CMD_TRIANGLE: texA = a.tex.empty() ? nullptr : a.tex.c_str(); break;
            case CMD_SQUARE:   texA = a.tex.empty() ? nullptr : a.tex.c_str(); break;
            case CMD_3DOBJECT: texA = a.obj_tex.empty() ? nullptr : a.obj_tex.c_str(); break;
            case CMD_PSEUDO3D:
                texA = (a.entity->getTextures().empty() ? nullptr : a.entity->getTextures()[0].c_str());
                texB = (b.entity->getTextures().empty() ? nullptr : b.entity->getTextures()[0].c_str());
                break;
            default: break;
        }
        return texA < texB;
    });

    GLuint currentShader = 0;
    const char* currentTexName = nullptr;
    int currentDimension = 1;

    static GLuint tri_vao = 0, tri_vbo = 0;
    static bool tri_init = false;
    if (!tri_init) {
        tri_init = true;
        glGenVertexArrays(1, &tri_vao);
        glGenBuffers(1, &tri_vbo);
        glBindVertexArray(tri_vao);
        glBindBuffer(GL_ARRAY_BUFFER, tri_vbo);
        glBufferData(GL_ARRAY_BUFFER, 21 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));
        glBindVertexArray(0);
        ensureWhiteTex();
    }

    static GLuint sq_vao = 0, sq_vbo = 0, sq_ibo = 0;
    static bool sq_init = false;
    if (!sq_init) {
        sq_init = true;
        glGenVertexArrays(1, &sq_vao);
        glGenBuffers(1, &sq_vbo);
        glGenBuffers(1, &sq_ibo);
        glBindVertexArray(sq_vao);
        glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
        glBufferData(GL_ARRAY_BUFFER, 4 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));
        GLuint indices[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sq_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glBindVertexArray(0);
        ensureWhiteTex();
    }

    static GLuint vao3D = 0, vbo_pos = 0, vbo_norm = 0, vbo_uv = 0, ibo3d = 0;
    static size_t cap_pos = 0, cap_norm = 0, cap_uv = 0, cap_idx = 0;
    if (vao3D == 0) {
        glGenVertexArrays(1, &vao3D);
    }

    GLuint boundTexUnit0 = 0;
    bool lightsApplied = false;
    bool shadowsApplied = false;

    for (const auto& cmd : drawQueue) {
        int dim = (cmd.type == CMD_TRIANGLE || cmd.type == CMD_SQUARE || cmd.type == CMD_TEXT) ? 0 : 1;
        if (dim != currentDimension) {
            if (dim == 0) {
                currentIs2D = true;
                initSimple2DShader();
                glUseProgram(simple2DShader);
                currentShader = simple2DShader;

                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glOrtho(0, window_w, 0, window_h, -1, 1);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();

                glDisable(GL_DEPTH_TEST);
                glDisable(GL_LIGHTING);
                glDisable(GL_FOG);
                glDisable(GL_CULL_FACE);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                GLint loc = glGetUniformLocation(simple2DShader, "tex");
                if (loc != -1) glUniform1i(loc, 0);

                currentTexName = nullptr;
                boundTexUnit0 = 0;
                lightsApplied = false;
                shadowsApplied = false;
            } else {
                currentIs2D = false;

                glEnable(GL_DEPTH_TEST);
                glEnable(GL_TEXTURE_2D);
                glEnable(GL_CULL_FACE);

                glMatrixMode(GL_MODELVIEW);
                glPopMatrix();
                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);

                changeSize3D(window_w, window_h);

                if (lighting_global) {
                    useShader(currentShaderProg);
                    if (fog.enabled) {
                        if (loc_fogColor != -1) glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
                        if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
                        if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);
                    }
                    applyAllLights();
                    applyAllShadows();
                    lightsApplied = true;
                    shadowsApplied = true;
                }

                currentTexName = nullptr;
                boundTexUnit0 = 0;
            }
            currentDimension = dim;
        }

        if (cmd.type == CMD_3DOBJECT && cmd.radius > 0.0f) {
            if (!sphereInFrustum(cmd.obj_cx, cmd.obj_cy, cmd.obj_cz, cmd.radius))
                continue;
        }

        switch (cmd.type) {
            case CMD_SQUARE: {
                GLuint shaderToUse = cmd.shaderID ? cmd.shaderID : simple2DShader;
                if (currentShader != shaderToUse) {
                    useShader(shaderToUse);
                    currentShader = shaderToUse;
                }
                const char* texName = cmd.tex.empty() ? nullptr : cmd.tex.c_str();
                if (texName != currentTexName) {
                    glActiveTexture(GL_TEXTURE0);
                    GLuint texID = texName ? loadTextureFromFile(texName) : 0;
                    if (texID) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, texID);
                    } else {
                        ensureWhiteTex();
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, whiteTex);
                    }
                    if (currentShader && loc_tex != -1) glUniform1i(loc_tex, 0);
                    currentTexName = texName;
                }
                if (currentShader) {
                    glDisableVertexAttribArray(2);
                    glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
                }

                const float ar = cmd.rotate * float(M_PI) / -180.0f;
                const float tc[8] = {0,1, 1,1, 1,0, 0,0};
                float data[28];
                for (int i = 0; i < 4; ++i) {
                    float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                    rotatePoint(px, py, 0, 0, ar);
                    float vx = cmd.cx + px * cmd.scale;
                    float vy = cmd.cy + py * cmd.scale;
                    data[i*7+0] = vx;
                    data[i*7+1] = vy;
                    data[i*7+2] = cmd.r;
                    data[i*7+3] = cmd.g;
                    data[i*7+4] = cmd.b;
                    data[i*7+5] = tc[i*2];
                    data[i*7+6] = tc[i*2+1];
                }
                glBindVertexArray(sq_vao);
                glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                break;
            }

            case CMD_TEXT: {
                if (currentShader != 0) {
                    stopShader();
                    currentShader = 0;
                }
                if (glIsEnabled(GL_TEXTURE_2D)) glDisable(GL_TEXTURE_2D);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(cmd.r, cmd.g, cmd.b, cmd.a);
                glRasterPos2f(cmd.x, cmd.y);
                for (const char* c = cmd.text.c_str(); *c; ++c)
                    glutBitmapCharacter(cmd.font, *c);
                currentTexName = nullptr;
                break;
            }

            case CMD_3DOBJECT: {
                GLuint shaderToUse = cmd.shaderID ? cmd.shaderID : defaultLightingShader;
                if (currentShader != shaderToUse) {
                    useShader(shaderToUse);
                    currentShader = shaderToUse;
                    lightsApplied = false;
                    shadowsApplied = false;
                }
                if (!lightsApplied || !shadowsApplied) {
                    applyAllLights();
                    applyAllShadows();
                    lightsApplied = true;
                    shadowsApplied = true;
                }

                if (currentShader && loc_receiveShadows != -1) {
                    glUniform1i(loc_receiveShadows, 1);
                }

                glActiveTexture(GL_TEXTURE0);

                GLuint desiredTex = 0;
                if (cmd.obj_tex.empty()) {
                    ensureWhiteTex();
                    desiredTex = whiteTex;
                } else {
                    desiredTex = loadTextureFromFile(cmd.obj_tex.c_str());
                    if (!desiredTex) {
                        ensureWhiteTex();
                        desiredTex = whiteTex;
                    }
                }

                if (desiredTex != boundTexUnit0) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, desiredTex);
                    boundTexUnit0 = desiredTex;
                    if (currentShader && loc_tex != -1) glUniform1i(loc_tex, 0);
                }

                currentTexName = cmd.obj_tex.empty() ? nullptr : cmd.obj_tex.c_str();

                size_t numVerts = cmd.obj_vertices.size() / 3;
                if (numVerts == 0 || cmd.obj_indices.empty()) break;

                const bool hasTex = (!cmd.obj_tex.empty() && !cmd.obj_texcoords.empty());
                std::vector<float> uv;
                const float* uvPtr;
                if (hasTex) {
                    uvPtr = cmd.obj_texcoords.data();
                } else {
                    uv.assign(numVerts * 2, 0.0f);
                    uvPtr = uv.data();
                }

                size_t posBytes = cmd.obj_vertices.size() * sizeof(float);
                ensureBuffer(vbo_pos, cap_pos, posBytes, GL_ARRAY_BUFFER);
                glBufferSubData(GL_ARRAY_BUFFER, 0, posBytes, cmd.obj_vertices.data());

                bool hasNormals = !cmd.obj_normals.empty();
                if (hasNormals) {
                    size_t normBytes = cmd.obj_normals.size() * sizeof(float);
                    ensureBuffer(vbo_norm, cap_norm, normBytes, GL_ARRAY_BUFFER);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, normBytes, cmd.obj_normals.data());
                }

                size_t uvBytes = numVerts * 2 * sizeof(float);
                ensureBuffer(vbo_uv, cap_uv, uvBytes, GL_ARRAY_BUFFER);
                glBufferSubData(GL_ARRAY_BUFFER, 0, uvBytes, uvPtr);

                size_t idxBytes = cmd.obj_indices.size() * sizeof(int);
                ensureBuffer(ibo3d, cap_idx, idxBytes, GL_ELEMENT_ARRAY_BUFFER);
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idxBytes, cmd.obj_indices.data());

                glBindVertexArray(vao3D);

                glBindBuffer(GL_ARRAY_BUFFER, vbo_pos);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

                if (hasNormals) {
                    glBindBuffer(GL_ARRAY_BUFFER, vbo_norm);
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
                } else {
                    glDisableVertexAttribArray(2);
                    glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
                }

                glDisableVertexAttribArray(3);
                glVertexAttrib3f(3, cmd.obj_r, cmd.obj_g, cmd.obj_b);

                glBindBuffer(GL_ARRAY_BUFFER, vbo_uv);
                glEnableVertexAttribArray(8);
                glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo3d);

                glPushMatrix();
                glTranslatef(cmd.obj_cx, cmd.obj_cy, cmd.obj_cz);
                glDrawElements(GL_TRIANGLES, (GLsizei)cmd.obj_indices.size(), GL_UNSIGNED_INT, nullptr);
                glPopMatrix();

                glBindVertexArray(0);
                break;
            }

            case CMD_PSEUDO3D: {
                GLuint shaderToUse = cmd.shaderID ? cmd.shaderID : defaultLightingShader;
                if (currentShader != shaderToUse) {
                    useShader(shaderToUse);
                    currentShader = shaderToUse;
                    lightsApplied = false;
                    shadowsApplied = false;
                }
                if (!lightsApplied || !shadowsApplied) {
                    applyAllLights();
                    applyAllShadows();
                    lightsApplied = true;
                    shadowsApplied = true;
                }

                const pseudo_3d_entity* ent = cmd.entity;
                if (!sphereInFrustum(ent->getX(), ent->getY(), ent->getZ(), ent->getRadius()))
                    break;

                const float dx = cmd.cam_x - ent->getX();
                const float dy = cmd.cam_y - ent->getY();
                const float dz = cmd.cam_z - ent->getZ();
                const int tidx = ent->getTextureIndex(dx, dy, dz);
                const char* texName = (tidx >= 0 && tidx < (int)ent->getTextures().size()) ?
                                       ent->getTextures()[tidx].c_str() : nullptr;

                if (texName != currentTexName) {
                    glActiveTexture(GL_TEXTURE0);
                    GLuint texID = texName ? loadTextureFromFile(texName) : 0;
                    if (texID) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, texID);
                    } else {
                        ensureWhiteTex();
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, whiteTex);
                    }
                    if (currentShader && loc_tex != -1) glUniform1i(loc_tex, 0);
                    currentTexName = texName;
                    boundTexUnit0 = 0;
                }

                if (currentShader && loc_receiveShadows != -1)
                    glUniform1i(loc_receiveShadows, 0);

                const float dist = sqrtf(dx*dx + dy*dy + dz*dz);
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

                const float ga = ent->getGAngle() * float(M_PI) / 180.0f;
                const float va = ent->getVAngle() * float(M_PI) / 180.0f;

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
                float total_roll = billboard_roll + ent->getRAngle();

                const bool mirror = (tidx == 0);
                const std::vector<float>& verts = ent->getVertices();

                const float ar2 = (mirror ? -180.0f : 0.0f) * float(M_PI) / -180.0f;
                const float tc2[8] = {0,1, 1,1, 1,0, 0,0};
                float data2[28];
                for (int i = 0; i < 4; ++i) {
                    float px = verts[i*2], py = verts[i*2+1];
                    rotatePoint(px, py, 0, 0, ar2);
                    data2[i*7 + 0] = px;
                    data2[i*7 + 1] = py;
                    data2[i*7 + 2] = 1.0f;
                    data2[i*7 + 3] = 1.0f;
                    data2[i*7 + 4] = 1.0f;
                    data2[i*7 + 5] = tc2[i*2];
                    data2[i*7 + 6] = tc2[i*2+1];
                }

                glPushMatrix();
                glTranslatef(ent->getX(), ent->getY(), ent->getZ());
                glMultMatrixf(mat);
                glRotatef(total_roll + 180.0f, 0, 0, 1);

                glBindVertexArray(sq_vao);
                glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data2), data2);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                glPopMatrix();

                if (currentShader && loc_receiveShadows != -1)
                    glUniform1i(loc_receiveShadows, 1);
                glActiveTexture(GL_TEXTURE0);
                break;
            }

            default: break;
        }
    }

    if (currentDimension == 0) {
        currentIs2D = false;
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_CULL_FACE);

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        changeSize3D(window_w, window_h);

        if (lighting_global) {
            useShader(defaultLightingShader);
            if (fog.enabled) {
                if (loc_fogColor != -1) glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
                if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
                if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);
            }
            applyAllLights();
        } else {
            useShader(simple2DShader);
        }
    }

    drawQueue.clear();
}
//              текстуры
// функция для загрузки текстуры
GLuint loadTextureFromFile(const char* filename) {
    {
        lock_guard<mutex> lock(textureCacheMutex);
        auto it = textureCache.find(filename);
        if (it != textureCache.end()) return it->second;
    }

    int w, h, channels;
    unsigned char* img = SOIL_load_image(filename, &w, &h, &channels, SOIL_LOAD_AUTO);
    if (!img) {
        cerr << "Cannot load texture: " << filename << " (" << SOIL_last_result() << ")" << endl;
        lock_guard<mutex> lock(textureCacheMutex);
        return textureCache[filename] = 0;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    boundTextureID = id;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

    GLint internalFormat = format;
    if (channels == 4 && glewIsSupported("GL_EXT_texture_compression_s3tc")) {
        internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    } else if (channels == 3 && glewIsSupported("GL_EXT_texture_compression_s3tc")) {
        internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, img);

    glGenerateMipmap(GL_TEXTURE_2D);

    SOIL_free_image_data(img);

    lock_guard<mutex> lock(textureCacheMutex);
    return textureCache[filename] = id;
}
// загружаем много текстур параллельно
void preloadTextures(const vector<string>& filenames) {
    struct RawTex { string name; unsigned char* data; int w, h, channels; };
    vector<RawTex> loaded(filenames.size());

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < (int)filenames.size(); ++i) {
        {
            lock_guard<mutex> lock(textureCacheMutex);
            if (textureCache.count(filenames[i])) {
                loaded[i] = {filenames[i], nullptr, 0, 0, 0};
                continue;
            }
        }
        int w, h, channels;
        unsigned char* img = SOIL_load_image(filenames[i].c_str(), &w, &h, &channels, SOIL_LOAD_AUTO);
        loaded[i] = {filenames[i], img, w, h, channels};
    }

    for (auto& t : loaded) {
        if (!t.data) continue;
        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        boundTextureID = id;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLenum format = (t.channels == 4) ? GL_RGBA : GL_RGB;
        GLint internalFormat = format;
        if (t.channels == 4 && glewIsSupported("GL_EXT_texture_compression_s3tc"))
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        else if (t.channels == 3 && glewIsSupported("GL_EXT_texture_compression_s3tc"))
            internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, t.w, t.h, 0, format, GL_UNSIGNED_BYTE, t.data);
        glGenerateMipmap(GL_TEXTURE_2D);

        SOIL_free_image_data(t.data);

        lock_guard<mutex> lock(textureCacheMutex);
        textureCache[t.name] = id;
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

// квадрат
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex) {
    if (!g_useDrawQueue) {
        static GLuint vao = 0, vbo = 0, ibo = 0;
        static bool init = false;
        if (!init) {
            init = true;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glGenBuffers(1, &ibo);

            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, 4 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));

            GLuint indices[6] = {0, 1, 2, 0, 2, 3};
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
            glBindVertexArray(0);
            ensureWhiteTex();
        }

        const float ar = rotate * float(M_PI) / -180.0f;
        const float tc[8] = {0,1, 1,1, 1,0, 0,0};

        float data[28];
        for (int i = 0; i < 4; ++i) {
            float px = vertices[i*2], py = vertices[i*2+1];
            rotatePoint(px, py, 0, 0, ar);
            float vx = x + px * local_size;
            float vy = y + py * local_size;
            data[i*7 + 0] = vx;
            data[i*7 + 1] = vy;
            data[i*7 + 2] = (float)r;
            data[i*7 + 3] = (float)g;
            data[i*7 + 4] = (float)b;
            data[i*7 + 5] = tc[i*2];
            data[i*7 + 6] = tc[i*2+1];
        }

        glActiveTexture(GL_TEXTURE0);
        if (tex) {
            GLuint texID = loadTextureFromFile(tex);
            if (texID) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, texID);
            } else {
                ensureWhiteTex();
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, whiteTex);
            }
        } else {
            ensureWhiteTex();
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
        }

        if (currentShaderProg && loc_tex != -1) glUniform1i(loc_tex, 0);
        if (currentShaderProg) {
            glDisableVertexAttribArray(2);
            glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        if (tex) {
            glDisable(GL_TEXTURE_2D);
        } else {
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return;
    }

    DrawCommand cmd;
    cmd.type = CMD_SQUARE;
    cmd.scale = local_size;
    cmd.cx = x;
    cmd.cy = y;
    cmd.r = (float)r;
    cmd.g = (float)g;
    cmd.b = (float)b;
    cmd.rotate = rotate;
    cmd.vertCount = 4;
    for (int i = 0; i < 8; i++) cmd.verts[i] = vertices[i];
    if (tex) cmd.tex = tex;
    cmd.shaderID = simple2DShader;
    drawQueue.push_back(cmd);
}
// рисовка текста
void draw_text(const char* text, float x, float y, void* font, float r, float g, float b, float a) {
    DrawCommand cmd;
    cmd.type = CMD_TEXT;
    cmd.text = text;
    cmd.x = x;  cmd.y = y;
    cmd.font = font;
    cmd.r = r;  cmd.g = g;  cmd.b = b;  cmd.a = a;
    cmd.shaderID = simple2DShader;
    drawQueue.push_back(cmd);
}

//              класс для рисовки псевдо 3д существ
pseudo_3d_entity::pseudo_3d_entity(float x, float y, float z,
                                   float g_angle, float v_angle, float r_angle,
                                   const std::vector<std::string>& textures, int v_angles,
                                   const std::vector<float>& vertices)
    : x(x), y(y), z(z),
      g_angle(g_angle), v_angle(v_angle), r_angle(r_angle),
      textureFiles(textures), v_angles(v_angles),
      vertices_(vertices) {
    computeRadius();
    textureIDs.resize(textureFiles.size());
    for (size_t i = 0; i < textureFiles.size(); ++i)
        textureIDs[i] = loadTextureFromFile(textureFiles[i].c_str());
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

    if (textureFiles.empty()) {
        cachedTexIdx = -1;
        return -1;
    }

    const int total = int(textureFiles.size());
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

    if (!g_useDrawQueue) {
        const float dx = cam_x - x;
        const float dy = cam_y - y;
        const float dz = cam_z - z;
        const float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        const int tidx = getTextureIndex(dx, dy, dz);

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
        const char* tex = (tidx >= 0 && tidx < (int)textureFiles.size()) ? textureFiles[tidx].c_str() : nullptr;

        if (currentShaderProg && loc_receiveShadows != -1)
            glUniform1i(loc_receiveShadows, 0);

        glPushMatrix();
        glTranslatef(x, y, z);
        glMultMatrixf(mat);
        glRotatef(total_roll + 180.0f, 0, 0, 1);
        square(1.0f, 0, 0, 1, 1, 1, mirror ? -180.0f : 0.0f, vertices_.data(), tex);
        glPopMatrix();

        if (currentShaderProg && loc_receiveShadows != -1)
            glUniform1i(loc_receiveShadows, 1);

        return;
    }

    DrawCommand cmd;
    cmd.type = CMD_PSEUDO3D;
    cmd.entity = this;
    cmd.cam_x = cam_x; cmd.cam_y = cam_y; cmd.cam_z = cam_z;
    cmd.shaderID = currentShaderProg;
    drawQueue.push_back(cmd);
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
    if (idx < 0 || idx >= (int)textureIDs.size()) return 0;
    return textureIDs[idx];
}

GLuint pseudo_3d_entity::getShadowTexture(float lx, float ly, float lz) const {
    return getTextureFromDirection(lx, ly, lz);
}

std::vector<pseudo_3d_entity*> shadowCasters;

void applyAllShadows() {
    GLuint prog = currentShaderProg;
    if (!prog) return;

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
    if (loc_numShadowCasters != -1) glUniform1i(loc_numShadowCasters, totalCasters);

    if (totalCasters == 0) {
        for (int i = 0; i < MAX_SHADOW_CASTERS; ++i)
            if (loc_shadowDarkness[i] != -1) glUniform1f(loc_shadowDarkness[i], 0.0f);
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

        if (loc_shadowMatrix[i] != -1) glUniformMatrix4fv(loc_shadowMatrix[i], 1, GL_FALSE, shadowMat);
        if (loc_shadowDarkness[i] != -1) glUniform1f(loc_shadowDarkness[i], 0.8f);

        GLfloat lightPos[3] = { lightCamX, lightCamY, lightCamZ };
        if (loc_shadowLightPos[i] != -1) glUniform3fv(loc_shadowLightPos[i], 1, lightPos);

        float lightDirView[3];
        for (int k = 0; k < 3; ++k) {
            lightDirView[k] = cameraView[0+k] * light->dir[0] +
                              cameraView[4+k] * light->dir[1] +
                              cameraView[8+k] * light->dir[2];
        }
        if (loc_shadowLightDir[i] != -1) glUniform3fv(loc_shadowLightDir[i], 1, lightDirView);
        if (loc_shadowLightCutoff[i] != -1) glUniform1f(loc_shadowLightCutoff[i], cosf(light->cutoff * M_PI / 180.0f));
        if (loc_shadowLightObjDist[i] != -1) glUniform1f(loc_shadowLightObjDist[i], objDist);

        glActiveTexture(GL_TEXTURE1 + i);
        bindTexture(texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (loc_shadowMap[i] != -1) glUniform1i(loc_shadowMap[i], 1 + i);
    }

    for (int i = totalCasters; i < MAX_SHADOW_CASTERS; ++i)
        if (loc_shadowDarkness[i] != -1) glUniform1f(loc_shadowDarkness[i], 0.0f);

    glActiveTexture(GL_TEXTURE0);
}

static void sound_end_callback(void* pUserData, ma_sound* pSound) {
    ma_sound_uninit(pSound);
    delete pSound;
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
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    window_w = width;
    window_h = height;
    if (currentIs2D) {
        changeSize2D(width, height);
    } else {
        changeSize3D(width, height);
    }
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
    initSimple2DShader();
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    changeSize2D(w, h);
}
//                                                                                      КОЛХОЗ!!! ПОТОМ СРОЧНО ИСПРАВИТЬ!!!
float global_pitch,global_yaw;
// настройка камеры
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll){
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

    bool is_inverted = (norm_pitch > 90.0f && norm_pitch < 270.0f);

    if (is_inverted != camera.was_inverted) {
        yaw += 180.0f; 
    }
    camera.was_inverted = is_inverted;

    if (is_inverted) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;                    
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

    if (roll != 0.0f) {
        float rad = roll * M_PI / 180.0f;
        glm::vec3 fwd(camera.dir_x, camera.dir_y, camera.dir_z);
        glm::vec3 up0(up_x, up_y, up_z);
        glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), rad, fwd));
        glm::vec3 newUp = rot * up0;
        up_x = newUp.x; up_y = newUp.y; up_z = newUp.z;
    }

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
    camera.pitch = pitch;
    camera.yaw   = yaw;
    camera.roll  = roll;
}
// перемещение камеры
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw,float roll){
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

    bool is_inverted = (norm_pitch > 90.0f && norm_pitch < 270.0f);

    if (is_inverted != camera.was_inverted) {
        yaw += 180.0f; 
    }
    camera.was_inverted = is_inverted;

    if (is_inverted) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;                    
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

    if (roll != 0.0f) {
        float rad = roll * M_PI / 180.0f;
        glm::vec3 fwd(camera.dir_x, camera.dir_y, camera.dir_z);
        glm::vec3 up0(up_x, up_y, up_z);
        glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), rad, fwd));
        glm::vec3 newUp = rot * up0;
        up_x = newUp.x; up_y = newUp.y; up_z = newUp.z;
    }

    // обновляем матрицу
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x,eye_y,eye_z,camera.ctr_x,camera.ctr_y,camera.ctr_z, up_x, up_y, up_z);

    global_pitch = pitch;
    global_yaw = yaw;
    camera.pitch = pitch;
    camera.yaw   = yaw;
    camera.roll  = roll;
}

//              3д(может быть потом ещё что-то будет)
// рисуем 3д объект, указывая вершины треугольников
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals) {
    // вычисляем радиус ограничивающей сферы
    float maxDist = 0.0f;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        float dx = vertices[i] - cx;
        float dy = vertices[i+1] - cy;
        float dz = vertices[i+2] - cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > maxDist) maxDist = d2;
    }
    float radius = sqrtf(maxDist);

    if (!g_useDrawQueue) {
        // --- немедленный рендеринг (для порталов) ---
        if (vao3D == 0) {
            glGenVertexArrays(1, &vao3D);
        }
        size_t numVerts = vertices.size() / 3;
        if (numVerts == 0 || indices.empty()) return;

        const bool hasTex = (tex != nullptr && !texcoords.empty());
        std::vector<float> uv;
        const float* uvPtr;
        if (hasTex) {
            uvPtr = texcoords.data();
        } else {
            uv.assign(numVerts * 2, 0.0f);
            uvPtr = uv.data();
        }

        size_t posBytes = vertices.size() * sizeof(float);
        ensureBuffer(vbo_pos, cap_pos, posBytes, GL_ARRAY_BUFFER);
        glBufferSubData(GL_ARRAY_BUFFER, 0, posBytes, vertices.data());

        bool hasNormals = !normals.empty();
        if (hasNormals) {
            size_t normBytes = normals.size() * sizeof(float);
            ensureBuffer(vbo_norm, cap_norm, normBytes, GL_ARRAY_BUFFER);
            glBufferSubData(GL_ARRAY_BUFFER, 0, normBytes, normals.data());
        }

        size_t uvBytes = numVerts * 2 * sizeof(float);
        ensureBuffer(vbo_uv, cap_uv, uvBytes, GL_ARRAY_BUFFER);
        glBufferSubData(GL_ARRAY_BUFFER, 0, uvBytes, uvPtr);

        size_t idxBytes = indices.size() * sizeof(int);
        ensureBuffer(ibo, cap_idx, idxBytes, GL_ELEMENT_ARRAY_BUFFER);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idxBytes, indices.data());

        glBindVertexArray(vao3D);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_pos);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

        if (hasNormals) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo_norm);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        } else {
            glDisableVertexAttribArray(2);
            glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
        }

        glDisableVertexAttribArray(3);
        glVertexAttrib3f(3, (float)r, (float)g, (float)b);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_uv);
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

        ensureWhiteTex();
        if (tex) {
            GLuint texID = loadTextureFromFile(tex);
            if (texID) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, texID);
            } else {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, whiteTex);
            }
        } else {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
        }

        if (currentShaderProg && loc_tex != -1) glUniform1i(loc_tex, 0);

        glPushMatrix();
        glTranslatef(cx, cy, cz);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
        glPopMatrix();

        if (tex) {
            glDisable(GL_TEXTURE_2D);
        } else {
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glBindVertexArray(0);
        return;
    }

    DrawCommand cmd;
    cmd.type = CMD_3DOBJECT;
    cmd.obj_cx = cx; cmd.obj_cy = cy; cmd.obj_cz = cz;
    cmd.obj_r = (float)r; cmd.obj_g = (float)g; cmd.obj_b = (float)b;
    if (tex) cmd.obj_tex = tex;
    cmd.obj_vertices = vertices;
    cmd.obj_indices  = indices;
    cmd.obj_texcoords = texcoords;
    cmd.obj_normals   = normals;
    cmd.radius = radius;
    cmd.shaderID = currentShaderProg;
    drawQueue.push_back(cmd);
}
// свет

void enable_light() {
    if (!lighting_global) {
        initDefaultShader();
        useShader(currentShaderProg);
        lighting_global = true;
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        set_ambient_light(0.05f, 0.05f, 0.05f);
        if (fog.enabled) {
            if (loc_fogColor != -1) glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
            if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
            if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);
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

Light::Light() {}

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

void applyAllLights() {
    static float lastCamPos[3] = {0,0,0};
    static float lastCamDir[3] = {0,0,0};
    static int lastActiveCount = -1;
    GLuint prog = currentShaderProg;
    if (prog == 0) return;

    std::vector<Light*> candidates;
    for (Light* light : activeLights) {
        if (light->isEnabled()) {
            candidates.push_back(light);
        }
    }

    if (candidates.empty()) {
        if (loc_numLights != -1) glUniform1i(loc_numLights, 0);
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
    if (loc_numLights != -1) glUniform1i(loc_numLights, count);

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

        if (loc_lightEnabled[i] != -1) glUniform1i(loc_lightEnabled[i], 1);
        if (loc_lightPosition[i] != -1) glUniform3fv(loc_lightPosition[i], 1, viewPos);
        if (loc_lightDirection[i] != -1) glUniform3fv(loc_lightDirection[i], 1, viewDir);

        float diff[3] = { light->color[0] * light->intensity,
                          light->color[1] * light->intensity,
                          light->color[2] * light->intensity };
        if (loc_lightDiffuse[i] != -1) glUniform3fv(loc_lightDiffuse[i], 1, diff);
        if (loc_lightCutoff[i] != -1) glUniform1f(loc_lightCutoff[i], cosf(light->cutoff * M_PI / 180.0f));
        if (loc_lightAttenuation[i] != -1) glUniform3f(loc_lightAttenuation[i], light->constAtt, light->linearAtt, light->quadAtt);
    }
}

void set_ambient_light(float r, float g, float b) {
    global_ambient[0] = r;
    global_ambient[1] = g;
    global_ambient[2] = b;
    if (loc_ambientLight != -1) glUniform3f(loc_ambientLight, r, g, b);
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
        if (loc_fogColor != -1) glUniform3f(loc_fogColor, r, g, b);
        if (loc_fogStart != -1) glUniform1f(loc_fogStart, start);
        if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, end);
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
            if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
            if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);
        }
    }
}

void set_fog_color(float r, float g, float b) {
    fog.color[0] = r; fog.color[1] = g; fog.color[2] = b;
    if (fog.enabled) {
        if (currentShaderProg) {
            if (loc_fogColor != -1) glUniform3f(loc_fogColor, r, g, b);
        }
    }
}

void set_fog_range(float start, float end) {
    fog.start = start;
    fog.end = end;
    if (fog.enabled) {
        if (currentShaderProg) {
            if (loc_fogStart != -1) glUniform1f(loc_fogStart, start);
            if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, end);
        }
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
    static float last_gpu_usage = -1.0f;
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
        {
            FILE* gf = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits", "r");
            if (gf) {
                float current_gpu = -1.0f;
                if (fscanf(gf, "%f", &current_gpu) == 1)
                    last_gpu_usage = current_gpu;
                pclose(gf);
            }
        }
    }
    // вывод статистики в левом верхнем углу
    char buf[256];
    snprintf(buf,sizeof(buf),"FPS: %.0f  RAM: %ld MB  CPU: %.1f%%  GPU: ", fps, ram_kb / 1024, cpu_pct);
    if (last_gpu_usage >= 0.0f) {
        char gpu_str[32];
        snprintf(gpu_str, sizeof(gpu_str), "%.1f%%", last_gpu_usage);
        strcat(buf, gpu_str);
    } else {
        strcat(buf, "N/A");
    }
    draw_text(buf,10.0f,float(win_h)-20.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    draw_text(buf,10.0f,float(win_h)-20.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"X: %.10f  Y: %.10f  Z: %.10f",camera.eye_x,camera.eye_y,camera.eye_z);
    draw_text(buf,10.0f,float(win_h)-32.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"CPU: %s  RAM: %s  GPU: %s",cpu_name.c_str(),ram_v.c_str(),gpu_name.c_str());
    draw_text(buf,10.0f,float(win_h)-44.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
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
//порталы
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
    allPortals.push_back(this);
}

Portal::~Portal() {
    destroyFBOs();
    auto it = std::find(allPortals.begin(), allPortals.end(), this);
    if (it != allPortals.end()) allPortals.erase(it);
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

glm::mat4 Portal::getPortalTransform(float fx, float fy, float fz,
                                      float tx, float ty, float tz) const {
    bool srcIsA = (fx == ax && fy == ay && fz == az);

    float srcYaw, srcPitch, srcRoll, dstYaw, dstPitch, dstRoll;
    float srcX, srcY, srcZ, dstX, dstY, dstZ;

    if (srcIsA) {
        srcYaw = yawA; srcPitch = pitchA; srcRoll = rollA;
        dstYaw = yawB; dstPitch = pitchB; dstRoll = rollB;
        srcX = ax; srcY = ay; srcZ = az;
        dstX = bx; dstY = by; dstZ = bz;
    } else {
        srcYaw = yawB; srcPitch = pitchB; srcRoll = rollB;
        dstYaw = yawA; dstPitch = pitchA; dstRoll = rollA;
        srcX = bx; srcY = by; srcZ = bz;
        dstX = ax; dstY = ay; dstZ = az;
    }

    int n = (int)vertices.size() / 3;
    glm::vec3 centerSrc(0,0,0), centerDst(0,0,0);
    for (int i = 0; i < n; i++) {
        centerSrc += glm::vec3(srcX + vertices[i*3], srcY + vertices[i*3+1], srcZ + vertices[i*3+2]);
        centerDst += glm::vec3(dstX + vertices[i*3], dstY + vertices[i*3+1], dstZ + vertices[i*3+2]);
    }
    centerSrc /= (float)n;
    centerDst /= (float)n;

    auto makeBasis = [](float yaw, float pitch, float roll) {
        glm::mat4 rot4 = glm::mat4(1.0f);
        rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::mat3(rot4);
    };

    glm::mat3 rotSrc = makeBasis(srcYaw, srcPitch, srcRoll);
    glm::mat3 rotDst = makeBasis(dstYaw, dstPitch, dstRoll);

    glm::mat4 fromSrc(1.0f);
    fromSrc[0] = glm::vec4(rotSrc[0], 0.0f);
    fromSrc[1] = glm::vec4(rotSrc[1], 0.0f);
    fromSrc[2] = glm::vec4(rotSrc[2], 0.0f);
    fromSrc[3] = glm::vec4(centerSrc, 1.0f);

    glm::mat4 toDst(1.0f);
    toDst[0] = glm::vec4(rotDst[0], 0.0f);
    toDst[1] = glm::vec4(rotDst[1], 0.0f);
    toDst[2] = glm::vec4(rotDst[2], 0.0f);
    toDst[3] = glm::vec4(centerDst, 1.0f);

    return toDst * glm::inverse(fromSrc);
}

static void portalSetUniforms(GLuint prog, GLuint tex, bool depthOnly) {
    glUseProgram(prog);
    if (loc_portalMode != -1) glUniform1i(loc_portalMode, 1);
    if (loc_portalDepthOnly != -1) glUniform1i(loc_portalDepthOnly, depthOnly ? 1 : 0);
    if (loc_portalTex != -1) glUniform1i(loc_portalTex, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(GL_TEXTURE0);
}

static void portalClearUniforms(GLuint prog) {
    if (loc_portalMode != -1) glUniform1i(loc_portalMode, 0);
    if (loc_portalDepthOnly != -1) glUniform1i(loc_portalDepthOnly, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void Portal::drawPortalSurface(float px, float py, float pz, GLuint tex, bool sideB) {
    int n = (int)vertices.size() / 3;
    if (n < 3) return;

    GLuint prog = currentShaderProg ? currentShaderProg : defaultLightingShader;

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
                                  int depth, bool drawingA){
    if (!sceneDraw) return;

    FBO& fbo = drawingA ? fboA : fboB;
    resizeFBOs(window_w > 0 ? window_w : 800, window_h > 0 ? window_h : 600);

    glm::mat4 portalMat = getPortalTransform(src_x, src_y, src_z, dst_x, dst_y, dst_z);

    glm::vec3 camPos = glm::vec3(portalMat * glm::vec4(camera.eye_x, camera.eye_y, camera.eye_z, 1.0f));
    glm::vec3 newDir = glm::normalize(glm::mat3(portalMat) * glm::vec3(camera.dir_x, camera.dir_y, camera.dir_z));
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
    float savedPitch = camera.pitch, savedYaw = camera.yaw;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glViewport(0, 0, fbo.w, fbo.h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera.eye_x = camPos.x; camera.eye_y = camPos.y; camera.eye_z = camPos.z;
    camera.dir_x = newDir.x; camera.dir_y = newDir.y; camera.dir_z = newDir.z;
    camera.ctr_x = camPos.x + newDir.x;
    camera.ctr_y = camPos.y + newDir.y;
    camera.ctr_z = camPos.z + newDir.z;
    camera.pitch = new_pitch;
    camera.yaw   = new_yaw;

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    gluLookAt(camPos.x, camPos.y, camPos.z,
              camera.ctr_x, camera.ctr_y, camera.ctr_z,
              camera.up_x, camera.up_y, camera.up_z);

    glEnable(GL_CLIP_PLANE0);
    glClipPlane(GL_CLIP_PLANE0, clipPlane);

    bool prevQueue = g_useDrawQueue;
    g_useDrawQueue = false;

    GLuint shaderToUse = currentShaderProg ? currentShaderProg : defaultLightingShader;
    useShader(shaderToUse);
    applyAllLights();
    applyAllShadows();

    sceneDraw();

    stopShader();

    g_useDrawQueue = prevQueue;

    glDisable(GL_CLIP_PLANE0);
    glPopMatrix();

    camera.eye_x = savedEyeX; camera.eye_y = savedEyeY; camera.eye_z = savedEyeZ;
    camera.ctr_x = savedCtrX; camera.ctr_y = savedCtrY; camera.ctr_z = savedCtrZ;
    camera.dir_x = savedDirX; camera.dir_y = savedDirY; camera.dir_z = savedDirZ;
    camera.up_x = savedUpX; camera.up_y = savedUpY; camera.up_z = savedUpZ;
    camera.pitch = savedPitch; camera.yaw = savedYaw;

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

void Portal::checkTeleport() {
    glm::vec3 camPos(camera.eye_x, camera.eye_y, camera.eye_z);

    auto computeWorldMatrix = [&](float px, float py, float pz,
                                   float yaw, float pitch, float roll) -> glm::mat4 {
        glm::mat4 rot = glm::mat4(1.0f);
        rot = glm::rotate(rot, glm::radians(yaw),   glm::vec3(0,1,0));
        rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1,0,0));
        rot = glm::rotate(rot, glm::radians(roll),  glm::vec3(0,0,1));
        return glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz)) * rot;
    };

    auto checkPortalSide = [&](SideState& state,
                                float sx, float sy, float sz,
                                float syaw, float spitch, float sroll,
                                float dx, float dy, float dz,
                                float dyaw, float dpitch, float droll,
                                glm::vec3 localNormal) -> bool
    {
        glm::mat4 worldSrc = computeWorldMatrix(sx, sy, sz, syaw, spitch, sroll);
        glm::vec3 normal = glm::mat3(worldSrc) * localNormal;

        glm::vec3 center(0.0f);
        int n = vertices.size() / 3;
        for (int i = 0; i < n; ++i) {
            glm::vec4 local(vertices[i*3], vertices[i*3+1], vertices[i*3+2], 1.0f);
            center += glm::vec3(worldSrc * local);
        }
        center /= float(n);

        float dist = glm::dot(normal, camPos - center);

        if (!state.prevValid) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            state.prevValid = true;
            return false;
        }

        float prevDist = state.prevSignedDist;

        if (prevDist * dist >= 0.0f) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            return false;
        }

        float t = prevDist / (prevDist - dist);
        glm::vec3 intersection = state.prevCamPos + t * (camPos - state.prevCamPos);

        glm::mat4 invWorld = glm::inverse(worldSrc);
        glm::vec3 localPt = glm::vec3(invWorld * glm::vec4(intersection, 1.0f));
        if (!pointInPortalPolygon(glm::vec2(localPt.x, localPt.y))) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            return false;
        }

        glm::mat4 transform = getPortalTransform(sx, sy, sz, dx, dy, dz);
        glm::vec4 newPos4 = transform * glm::vec4(intersection, 1.0f);
        glm::vec3 newPos(newPos4.x, newPos4.y, newPos4.z);

        glm::mat3 rotMat = glm::mat3(transform);
        glm::vec3 oldForward(camera.dir_x, camera.dir_y, camera.dir_z);
        glm::vec3 oldUp(camera.up_x, camera.up_y, camera.up_z);
        glm::vec3 newForward = glm::normalize(rotMat * oldForward);
        glm::vec3 newUp      = rotMat * oldUp;

        float newPitch = glm::degrees(asinf(newForward.y));
        float newYaw   = glm::degrees(atan2f(newForward.x, newForward.z));

        const float epsilon = 0.001f;
        if (newUp.y < -epsilon) {
            newPitch = 180.0f - newPitch;
            newYaw   = newYaw + 180.0f;
            newYaw = fmod(newYaw, 360.0f)+180;
            if (newYaw < 0.0f) newYaw += 360.0f;
        }

        float np = fmod(newPitch, 360.0f);
        if (np < 0) np += 360.0f;
        glm::vec3 defUp = (np > 90.0f && np < 270.0f)
                            ? glm::vec3(0.0f, -1.0f, 0.0f)
                            : glm::vec3(0.0f,  1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(newUp, newForward));
        glm::vec3 correctedUp = glm::cross(newForward, right);
        float cosRoll = glm::dot(defUp, correctedUp);
        float sinRoll = glm::dot(glm::cross(defUp, correctedUp), newForward);
        float newRoll = glm::degrees(atan2f(sinRoll, cosRoll));

        glm::mat4 worldDst = computeWorldMatrix(dx, dy, dz, dyaw, dpitch, droll);
        glm::vec3 pushNormal = glm::mat3(worldDst) * (-localNormal);
        const float push = 0.1f;  
        if (prevDist > 0.0f) {
            newPos += pushNormal * push;
        } else {
            newPos -= pushNormal * push;
        }

        move_camera(newPos.x, newPos.y, newPos.z, newPitch, newYaw, newRoll);

        state.prevValid = false;
        SideState& otherState = (&state == &sideA) ? sideB : sideA;
        otherState.prevValid = false;
        return true;
    };

    if (checkPortalSide(sideA, ax, ay, az, yawA, pitchA, rollA,
                        bx, by, bz, yawB, pitchB, rollB,
                        glm::vec3(0.0f, 0.0f, -1.0f)))
        return;

    checkPortalSide(sideB, bx, by, bz, yawB, pitchB, rollB,
                    ax, ay, az, yawA, pitchA, rollA,
                    glm::vec3(0.0f, 0.0f, 1.0f));
}