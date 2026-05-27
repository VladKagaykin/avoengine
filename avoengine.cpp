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
// определить названия компонентов
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <string>

static std::string getCPUName_Win() {
    // Использование CPUID для получения строки процессора
    char brand[49] = {0};
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0x80000000);
    if (cpuInfo[0] >= 0x80000004) {
        for (unsigned int i = 0; i < 3; ++i) {
            __cpuid(cpuInfo, 0x80000002 + i);
            memcpy(brand + i * 16, cpuInfo, sizeof(cpuInfo));
        }
        return std::string(brand);
    }
    return "Unknown CPU";
}

static std::string getRAMTotal_Win() {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    return std::to_string(statex.ullTotalPhys / (1024 * 1024)) + " MB";
}

static std::string getGPUName_OpenGL() {
    // Должен быть активный контекст OpenGL
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    return renderer ? std::string(renderer) : "Unknown GPU";
}
#else
#include <fstream>
#include <sstream>
#include <cstring>

static std::string getCPUName_Linux() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            size_t pos = line.find(": ");
            if (pos != std::string::npos)
                return line.substr(pos + 2);
        }
    }
    return "Unknown CPU";
}

static std::string getRAMTotal_Linux() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            size_t pos = line.find(":");
            std::string val = line.substr(pos + 1);
            // удаляем " kB" в конце
            size_t kbpos = val.find("kB");
            if (kbpos != std::string::npos)
                val = val.substr(0, kbpos);
            // убираем пробелы
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            long kb = std::stol(val);
            return std::to_string(kb / 1024) + " MB";
        }
    }
    return "Unknown RAM";
}

static std::string getGPUName_OpenGL() {
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    return renderer ? std::string(renderer) : "Unknown GPU";
}
#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>           
#pragma comment(lib, "pdh.lib")
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#include <cstring>
#endif

#ifdef _WIN32
static double getProcessCPUUsage_Win() {
    static ULARGE_INTEGER lastCPU, lastSysCPU;
    static int numProcessors = 0;
    static bool initialized = false;

    HANDLE self = GetCurrentProcess();
    FILETIME ftime, fsys, fuser;
    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    ULARGE_INTEGER nowCPU, nowSysCPU;
    nowCPU.LowPart = fuser.dwLowDateTime; nowCPU.HighPart = fuser.dwHighDateTime;
    nowSysCPU.LowPart = fsys.dwLowDateTime; nowSysCPU.HighPart = fsys.dwHighDateTime;

    if (!initialized) {
        lastCPU = nowCPU; lastSysCPU = nowSysCPU;
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
        initialized = true;
        return 0.0;
    }

    ULONGLONG totalCPU = (nowCPU.QuadPart - lastCPU.QuadPart) +
                         (nowSysCPU.QuadPart - lastSysCPU.QuadPart);
    lastCPU = nowCPU; lastSysCPU = nowSysCPU;

    FILETIME ftime2;
    GetSystemTimeAsFileTime(&ftime2);
    ULARGE_INTEGER nowSysTime;
    nowSysTime.LowPart = ftime2.dwLowDateTime;
    nowSysTime.HighPart = ftime2.dwHighDateTime;
    static ULARGE_INTEGER lastSysTime = nowSysTime;
    double elapsed = (nowSysTime.QuadPart - lastSysTime.QuadPart) / 10000000.0;
    lastSysTime = nowSysTime;

    if (elapsed <= 0.0) return 0.0;
    return (totalCPU / 10000.0) / (elapsed * numProcessors) * 100.0;
}

static long getProcessRAMUsage_Win() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024 * 1024); // МБ
    return 0;
}

static float getGPUUsage_Win() {
    return -1.0f;
}
#else
static double getProcessCPUUsage_Linux(long &prev_cpu_total) {
    long utime, stime;
    FILE* stat = fopen("/proc/self/stat", "r");
    if (!stat) return 0.0;
    fscanf(stat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &utime, &stime);
    fclose(stat);

    long cur_cpu = utime + stime;
    static long numCores = sysconf(_SC_NPROCESSORS_ONLN);   

    double pct = (cur_cpu - prev_cpu_total) * 100.0 / (double)sysconf(_SC_CLK_TCK) / 1.0 / numCores;
    prev_cpu_total = cur_cpu;
    return pct;
}

static long getProcessRAMUsage_Linux() {
    FILE* status = fopen("/proc/self/status", "r");
    if (!status) return 0;
    char line[128];
    long vmRSS = 0;
    while (fgets(line, sizeof(line), status))
        if (sscanf(line, "VmRSS: %ld", &vmRSS) == 1) break;
    fclose(status);
    return vmRSS / 1024; 
}

#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>

static float getGPUUsage_Linux() {
    for (int i = 0; i < 8; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", i);
        FILE* f = fopen(path, "r");
        if (f) {
            int val = -1;
            int ret = fscanf(f, "%d", &val);
            fclose(f);
            if (ret == 1 && val >= 0) return (float)val;
        }
    }

    FILE* intel_fp = popen("intel_gpu_top -J -s 250 -o /dev/stdout 2>/dev/null", "r");
    if (intel_fp) {
        char buf[4096];
        std::string json;
        while (fgets(buf, sizeof(buf), intel_fp))
            json += buf;
        pclose(intel_fp);

        size_t pos = json.find("\"Render/3D");
        if (pos != std::string::npos) {
            pos = json.find("\"busy\":", pos);
            if (pos != std::string::npos) {
                pos += 7;
                char* end;
                float usage = strtof(json.c_str() + pos, &end);
                if (usage >= 0.0f && usage <= 100.0f)
                    return usage;
            }
        }
    }

    FILE* amd_fp = popen("radeontop -d - -l 1 2>/dev/null", "r");
    if (amd_fp) {
        char line[256];
        float total_usage = -1.0f;
        while (fgets(line, sizeof(line), amd_fp)) {
            if (strstr(line, "Graphics pipe")) {
                char* pct = strstr(line, "%");
                if (pct) {
                    *pct = '\0';
                    char* num = strrchr(line, ' ');
                    if (num) {
                        total_usage = strtof(num + 1, nullptr);
                        break;
                    }
                }
            }
        }
        pclose(amd_fp);
        if (total_usage >= 0.0f) return total_usage;
    }

    FILE* nv_fp = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (nv_fp) {
        float usage = -1.0f;
        if (fscanf(nv_fp, "%f", &usage) == 1) {
            pclose(nv_fp);
            return usage;
        }
        pclose(nv_fp);
    }

    return -1.0f; 
}
#endif

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

static GLuint accumulationFBO = 0;
static GLuint accumulationTex = 0;
static GLuint accumulationDepth = 0;
static int accumulationFrameCount = 0;
static float prevCamX = 0, prevCamY = 0, prevCamZ = 0;
static float prevCamPitch = 0, prevCamYaw = 0;
static bool accumulationInited = false;

void createAccumulationBuffers(int w, int h) {
    if (accumulationInited) {
        glDeleteFramebuffers(1, &accumulationFBO);
        glDeleteTextures(1, &accumulationTex);
        glDeleteTextures(1, &accumulationDepth);
    }
    glGenFramebuffers(1, &accumulationFBO);
    glGenTextures(1, &accumulationTex);
    glGenTextures(1, &accumulationDepth);

    glBindTexture(GL_TEXTURE_2D, accumulationTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, accumulationDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, accumulationFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumulationTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, accumulationDepth, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    accumulationInited = true;
    accumulationFrameCount = 0;
}

void resetAccumulation() {
    accumulationFrameCount = 0;
    if (accumulationInited) {
        glBindFramebuffer(GL_FRAMEBUFFER, accumulationFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

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
varying vec2 vUV;

uniform bool raycast;
uniform bool displayMode;

void main() {
    vN = normalize(gl_NormalMatrix * aNormal);
    vec4 mvPos = gl_ModelViewMatrix * aVertex;
    vP = mvPos.xyz;
    vColor = aColor;
    vTexCoord = aTexCoord;
    vWorldPos = aVertex.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * aVertex;
    vClipPos = gl_Position;
    if (raycast || displayMode)
        vUV = (aVertex.xy + 1.0) * 0.5;
    else
        vUV = vec2(0.0);
}
)";

static const char* defaultFragmentShader = R"(
#version 120
#define MAX_LIGHTS 16
#define MAX_STEPS 64
#define SURF_DIST 0.001
#define MAX_DIST 1000.0
#define MAX_TEXTURES 2         
#define MAX_PORTALS 8
#define MAX_PORTAL_VERTS 16
#define MAX_BOUNCES 40

struct Light {
    bool enabled;
    vec3 position;
    vec3 direction;
    vec3 diffuse;
    float cutoff;
    vec3 attenuation;
};

varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec4 vClipPos;
varying vec2 vUV;

uniform sampler2D tex;
uniform Light lights[MAX_LIGHTS];
uniform int numLights;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

uniform bool portalMode;
uniform bool portalDepthOnly;
uniform sampler2D portalTex;

uniform bool raycast;
uniform vec3 camPos;
uniform mat4 invViewProj;
uniform sampler2D panoramaTex;
uniform bool hasPanorama;

uniform sampler2D triTexPos;
uniform sampler2D triTexNorm;
uniform sampler2D triTexColor;
uniform sampler2D triTexUV;
uniform sampler2D triTexIndices;
uniform sampler2D textures[MAX_TEXTURES];   
uniform int triCount;
uniform int triTexWidth;
uniform int triTexHeight;

uniform bool displayMode;
uniform sampler2D accumulationTex;
uniform float frameCount;

uniform int portalCount;
uniform vec3 portalPos[MAX_PORTALS];
uniform vec3 portalNormal[MAX_PORTALS];
uniform float portalD[MAX_PORTALS];
uniform mat4 portalInvWorld[MAX_PORTALS];
uniform int portalVertCount[MAX_PORTALS];
uniform vec2 portalVerts[MAX_PORTALS * MAX_PORTAL_VERTS];
uniform mat4 portalTeleport[MAX_PORTALS];

float rayTriangleIntersect(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out vec3 faceNormal, out float u, out float v) {
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 h = cross(rd, e2);
    float a = dot(e1, h);
    if (abs(a) < 0.00001) return -1.0;
    float f = 1.0 / a;
    vec3 s = ro - v0;
    u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return -1.0;
    vec3 q = cross(s, e1);
    v = f * dot(rd, q);
    if (v < 0.0 || u + v > 1.0) return -1.0;
    float t = f * dot(e2, q);
    if (t > 0.001) {
        faceNormal = cross(e1, e2);
        return t;
    }
    return -1.0;
}

float shadowRay(int lightIdx, vec3 hitPos, vec3 N) {
    const float SHADOW_BIAS = 0.001;
    const float ALPHA_THRESHOLD = 0.5;
    const int MAX_SHADOW_BOUNCES = 4;

    Light L = lights[lightIdx];
    vec3 lightPos = L.position;
    vec3 lightDir = L.direction;
    bool isSpot = dot(lightDir, lightDir) > 0.000001;

    vec3 ro = hitPos + N * SHADOW_BIAS;
    vec3 rd;
    float maxDist;

    vec3 toLight = lightPos - hitPos;
    float distToLight = length(toLight);
    
    if (isSpot) {
        vec3 dirToLight = toLight / distToLight;
        float cosAngle = dot(-dirToLight, normalize(lightDir));
        if (cosAngle < L.cutoff) return 0.0;
    }
    
    rd = toLight / distToLight;
    maxDist = distToLight;

    float invTriW = 1.0 / float(triTexWidth);
    float invTriH = 1.0 / float(triTexHeight);
    float triW = float(triTexWidth);

    for (int bounce = 0; bounce < MAX_SHADOW_BOUNCES; bounce++) {
        float closestOpaque = maxDist;
        bool hitPortal = false;
        int portalIdx = -1;

        for (int i = 0; i < triCount; i++) {
            float base = float(i) * 3.0;
            float u0 = mod(base, triW) * invTriW;
            float v0coord = floor(base * invTriW) * invTriH;
            vec4 idxData0 = texture2D(triTexIndices, vec2(u0, v0coord));
            float i0 = idxData0.x;
            if (i0 < 0.0) continue;
            float tid_f = idxData0.z;

            float u1 = mod(base+1.0, triW) * invTriW;
            float v1coord = floor((base+1.0) * invTriW) * invTriH;
            float i1 = texture2D(triTexIndices, vec2(u1, v1coord)).x;
            float u2 = mod(base+2.0, triW) * invTriW;
            float v2coord = floor((base+2.0) * invTriW) * invTriH;
            float i2 = texture2D(triTexIndices, vec2(u2, v2coord)).x;

            float p0x = mod(i0, triW) * invTriW, p0y = floor(i0 * invTriW) * invTriH;
            float p1x = mod(i1, triW) * invTriW, p1y = floor(i1 * invTriW) * invTriH;
            float p2x = mod(i2, triW) * invTriW, p2y = floor(i2 * invTriW) * invTriH;

            vec3 v0 = texture2D(triTexPos, vec2(p0x, p0y)).rgb;
            vec3 v1 = texture2D(triTexPos, vec2(p1x, p1y)).rgb;
            vec3 v2 = texture2D(triTexPos, vec2(p2x, p2y)).rgb;

            vec3 faceNormal;
            float u, v;
            float t = rayTriangleIntersect(ro, rd, v0, v1, v2, faceNormal, u, v);
            if (t > SHADOW_BIAS && t < closestOpaque) {
                int texID = int(floor(tid_f));
                bool opaque = true;
                if (texID >= 0 && texID < MAX_TEXTURES) {
                    vec2 uv0 = texture2D(triTexUV, vec2(p0x, p0y)).rg;
                    vec2 uv1 = texture2D(triTexUV, vec2(p1x, p1y)).rg;
                    vec2 uv2 = texture2D(triTexUV, vec2(p2x, p2y)).rg;
                    vec2 uvCoord = (1.0-u-v)*uv0 + u*uv1 + v*uv2;
                    vec4 texCol = vec4(1.0);
                    if (texID == 0) texCol = texture2D(textures[0], uvCoord);
                    else if (texID == 1) texCol = texture2D(textures[1], uvCoord);
                    if (texCol.a < ALPHA_THRESHOLD) opaque = false;
                }
                if (opaque) {
                    closestOpaque = t;
                    hitPortal = false;
                }
            }
        }

        for (int p = 0; p < portalCount; p++) {
            vec3 Nportal = portalNormal[p];
            float denom = dot(rd, Nportal);
            if (abs(denom) < 0.0001) continue;
            float t = -(dot(ro, Nportal) + portalD[p]) / denom;
            if (t > SHADOW_BIAS && t < closestOpaque) {
                vec3 candidatePos = ro + rd * t;
                vec2 localPt = (portalInvWorld[p] * vec4(candidatePos, 1.0)).xy;
                int vc = portalVertCount[p];
                bool inside = false;
                for (int i = 0, j = vc-1; i < vc; j = i++) {
                    vec2 vi = portalVerts[p * MAX_PORTAL_VERTS + i];
                    vec2 vj = portalVerts[p * MAX_PORTAL_VERTS + j];
                    if (((vi.y > localPt.y) != (vj.y > localPt.y)) &&
                        (localPt.x < (vj.x-vi.x)*(localPt.y-vi.y)/(vj.y-vi.y)+vi.x))
                        inside = !inside;
                }
                if (inside) {
                    closestOpaque = t;
                    hitPortal = true;
                    portalIdx = p;
                }
            }
        }

        if (!hitPortal && closestOpaque == maxDist)
            return 1.0;

        if (!hitPortal)
            return 0.0;

        vec3 hitPoint = ro + rd * closestOpaque;
        ro = vec3(portalTeleport[portalIdx] * vec4(hitPoint, 1.0));
        rd = normalize(mat3(portalTeleport[portalIdx]) * rd);

        toLight = lightPos - ro;
        maxDist = length(toLight);
        rd = toLight / maxDist;
    }
    return 1.0;
}

void main() {
    if (displayMode) {
        gl_FragColor = texture2D(accumulationTex, vUV);
        return;
    }
    if (portalMode) {
        if (portalDepthOnly) { gl_FragColor = vec4(0.0); return; }
        vec2 uv = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;
        gl_FragColor = texture2D(portalTex, uv);
        return;
    }

    float invFogRange = 1.0 / (fogEnd - fogStart);
    const float PI = 3.14159265;
    const float TWO_PI = 6.2831853;

    if (raycast) {
        vec3 ro = camPos;
        vec4 clipPos = vec4(vUV * 2.0 - 1.0, -1.0, 1.0);
        vec4 worldPos = invViewProj * clipPos;
        worldPos /= worldPos.w;
        vec3 rd = normalize(worldPos.xyz - ro);
        float totalDist = 0.0;

        float invTriW = 1.0 / float(triTexWidth);
        float invTriH = 1.0 / float(triTexHeight);
        float triW = float(triTexWidth);

        for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
            float closest = MAX_DIST;
            vec3 hitPos, hitNormal, hitCol;
            int hitTexID = -1;
            bool isPortalHit = false;
            int portalHitIdx = -1;

            for (int i = 0; i < triCount; i++) {
                float base = float(i) * 3.0;
                float u0 = mod(base, triW) * invTriW;
                float v0coord = floor(base * invTriW) * invTriH;
                vec4 idxData0 = texture2D(triTexIndices, vec2(u0, v0coord));
                float i0 = idxData0.x;
                if (i0 < 0.0) continue;
                float billFlag = idxData0.y;
                float tid_f = idxData0.z;

                float u1 = mod(base+1.0, triW) * invTriW;
                float v1coord = floor((base+1.0) * invTriW) * invTriH;
                float i1 = texture2D(triTexIndices, vec2(u1, v1coord)).x;
                float u2 = mod(base+2.0, triW) * invTriW;
                float v2coord = floor((base+2.0) * invTriW) * invTriH;
                float i2 = texture2D(triTexIndices, vec2(u2, v2coord)).x;

                float p0x = mod(i0, triW) * invTriW, p0y = floor(i0 * invTriW) * invTriH;
                float p1x = mod(i1, triW) * invTriW, p1y = floor(i1 * invTriW) * invTriH;
                float p2x = mod(i2, triW) * invTriW, p2y = floor(i2 * invTriW) * invTriH;

                vec3 v0 = texture2D(triTexPos, vec2(p0x, p0y)).rgb;
                vec3 v1 = texture2D(triTexPos, vec2(p1x, p1y)).rgb;
                vec3 v2 = texture2D(triTexPos, vec2(p2x, p2y)).rgb;

                vec3 faceNormal;
                float u, v;
                float t = rayTriangleIntersect(ro, rd, v0, v1, v2, faceNormal, u, v);
                if (t > 0.0 && t < closest) {
                    int texID = int(floor(tid_f));
                    bool opaque = true;
                    vec3 col = texture2D(triTexColor, vec2(p0x, p0y)).rgb;
                    if (texID >= 0 && texID < MAX_TEXTURES) {
                        vec2 uv0 = texture2D(triTexUV, vec2(p0x, p0y)).rg;
                        vec2 uv1 = texture2D(triTexUV, vec2(p1x, p1y)).rg;
                        vec2 uv2 = texture2D(triTexUV, vec2(p2x, p2y)).rg;
                        vec2 uvCoord = (1.0-u-v)*uv0 + u*uv1 + v*uv2;
                        vec4 texCol = vec4(1.0);
                        if (texID == 0) texCol = texture2D(textures[0], uvCoord);
                        else if (texID == 1) texCol = texture2D(textures[1], uvCoord);
                        if (texCol.a < 0.5) opaque = false;
                        col = texCol.rgb * col;
                    }
                    if (opaque) {
                        closest = t;
                        hitPos = ro + rd * t;
                        if (billFlag < 0.5) {
                            hitNormal = -rd;
                        } else {
                            vec3 n0 = texture2D(triTexNorm, vec2(p0x, p0y)).rgb;
                            vec3 n1 = texture2D(triTexNorm, vec2(p1x, p1y)).rgb;
                            vec3 n2 = texture2D(triTexNorm, vec2(p2x, p2y)).rgb;
                            vec3 interpN = (1.0-u-v)*n0 + u*n1 + v*n2;
                            float len2 = dot(interpN, interpN);
                            if (len2 < 0.0000001) {
                                hitNormal = normalize(faceNormal);
                            } else {
                                hitNormal = interpN * inversesqrt(len2);
                            }
                        }
                        hitCol = col;
                        hitTexID = texID;
                        isPortalHit = false;
                    }
                }
            }

            for (int p = 0; p < portalCount; p++) {
                vec3 N = portalNormal[p];
                float denom = dot(rd, N);
                if (abs(denom) < 0.0001) continue;
                float t = -(dot(ro, N) + portalD[p]) / denom;
                if (t > 0.001 && t < closest) {
                    vec3 candidatePos = ro + rd * t;
                    vec2 localPt = (portalInvWorld[p] * vec4(candidatePos, 1.0)).xy;
                    int vc = portalVertCount[p];
                    bool inside = false;
                    for (int i = 0, j = vc-1; i < vc; j = i++) {
                        vec2 vi = portalVerts[p * MAX_PORTAL_VERTS + i];
                        vec2 vj = portalVerts[p * MAX_PORTAL_VERTS + j];
                        if (((vi.y > localPt.y) != (vj.y > localPt.y)) &&
                            (localPt.x < (vj.x-vi.x)*(localPt.y-vi.y)/(vj.y-vi.y)+vi.x))
                            inside = !inside;
                    }
                    if (inside) {
                        closest = t;
                        hitPos = candidatePos;
                        isPortalHit = true;
                        portalHitIdx = p;
                    }
                }
            }

            if (closest == MAX_DIST) {
                if (hasPanorama) {
                    float panU = 0.5 + atan(rd.z, rd.x) / TWO_PI;
                    float panV = 0.5 + asin(rd.y) / PI;
                    gl_FragColor = texture2D(panoramaTex, vec2(panU, panV));
                } else {
                    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                }
                return;
            }

            if (isPortalHit) {
                totalDist += closest;
                ro = vec3(portalTeleport[portalHitIdx] * vec4(hitPos, 1.0));
                rd = normalize(mat3(portalTeleport[portalHitIdx]) * rd);
            } else {
                float fogCoord = totalDist + closest;
                vec3 N = normalize(hitNormal);
                vec3 col = ambientLight;

                for (int j = 0; j < numLights; j++) {
                    if (!lights[j].enabled) continue;
                    if (lights[j].attenuation.x <= 0.0) continue;
                    vec3 L; float atten;
                    if (dot(lights[j].direction, lights[j].direction) > 0.000001) {
                        vec3 toPoint = hitPos - lights[j].position;
                        float d2 = dot(toPoint, toPoint);
                        if (d2 < 0.000001) continue;
                        float invDist = inversesqrt(d2);
                        L = -toPoint * invDist;
                        float d = 1.0 / invDist;
                        atten = 1.0 / (lights[j].attenuation.x + lights[j].attenuation.y*d + lights[j].attenuation.z*d2);
                        if (dot(-L, lights[j].direction) < lights[j].cutoff) continue;
                    } else {
                        vec3 toLight = lights[j].position - hitPos;
                        float d2 = dot(toLight, toLight);
                        if (d2 < 0.000001) continue;
                        float invDist = inversesqrt(d2);
                        L = toLight * invDist;
                        float d = 1.0 / invDist;
                        atten = 1.0 / (lights[j].attenuation.x + lights[j].attenuation.y*d + lights[j].attenuation.z*d2);
                    }
                    float diff = max(dot(N, L), 0.0);
                    if (diff > 0.0) {
                        float shadow = shadowRay(j, hitPos, N);
                        col += lights[j].diffuse * diff * atten * shadow;
                    }
                }

                col *= hitCol;
                float fogFactor = clamp((fogEnd - fogCoord) * invFogRange, 0.0, 1.0);
                col = mix(fogColor, col, fogFactor);
                gl_FragColor = vec4(col, 1.0);
                return;
            }
        }
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 texColor = texture2D(tex, vTexCoord);
    if (texColor.a < 0.01) discard;
    vec3 N = normalize(vN);
    vec3 totalLight = ambientLight;
    int nL = (numLights < MAX_LIGHTS) ? numLights : MAX_LIGHTS;
    for (int i = 0; i < nL; i++) {
        if (!lights[i].enabled || lights[i].attenuation.x <= 0.0) continue;
        vec3 Lv = lights[i].position - vP;
        float d2 = dot(Lv, Lv);
        if (d2 < 0.000001) continue;
        float invDist = inversesqrt(d2);
        float dist = 1.0 / invDist;
        vec3 L = Lv * invDist;
        if (dot(-L, normalize(lights[i].direction)) < lights[i].cutoff) continue;
        float att = 1.0 / (lights[i].attenuation.x + lights[i].attenuation.y*dist + lights[i].attenuation.z*d2);
        float diff = max(dot(N, L), 0.0);
        totalLight += lights[i].diffuse * diff * att;
    }
    vec3 finalColor = texColor.rgb * vColor.rgb * totalLight;
    float fogCoord = length(vP);
    float fogFactor = clamp((fogEnd - fogCoord) * invFogRange, 0.0, 1.0);
    finalColor = mix(fogColor, finalColor, fogFactor);
    gl_FragColor = vec4(finalColor, texColor.a * vColor.a);
}
)";

static GLint loc_displayMode = -1;
static GLint loc_accumulationTex = -1;
static GLint loc_frameCount = -1;
static GLuint triTexUV = 0;

const int MAX_SHADOW_CASTERS = 8;

// кэш uniform-локаций для основного шейдера
static GLint loc_tex = -1;
static GLint loc_numLights = -1;
static GLint loc_ambientLight = -1;
static GLint loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;
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
        loc_portalMode = glGetUniformLocation(defaultLightingShader, "portalMode");
        loc_portalDepthOnly = glGetUniformLocation(defaultLightingShader, "portalDepthOnly");
        loc_portalTex = glGetUniformLocation(defaultLightingShader, "portalTex");
        loc_displayMode = glGetUniformLocation(defaultLightingShader, "displayMode");
        loc_accumulationTex = glGetUniformLocation(defaultLightingShader, "accumulationTex");
        loc_frameCount = glGetUniformLocation(defaultLightingShader, "frameCount");

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
static GLint loc_tex_2d = -1;

void initSimple2DShader() {
    if (simple2DShader) return;
    simple2DShader = createShaderProgram(simple2DVertexShader, simple2DFragmentShader);
    loc_tex_2d = glGetUniformLocation(simple2DShader, "tex");
}
static GLuint lineVAO = 0, lineVBO = 0;
static bool lineInit = false;

static void initLineVAO() {
    if (lineInit) return;
    lineInit = true;
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, 2 * 9 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(7 * sizeof(float)));
    glBindVertexArray(0);
}

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

//              очень ужасный рэй кастинг
enum DrawCommandType : int {
    CMD_SQUARE,
    CMD_TEXT,
    CMD_3DOBJECT,
    CMD_PSEUDO3D,
    CMD_LINE_2D,
    CMD_LINE_3D,
    CMD_PANORAMA,
    CMD_PORTAL
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
    float obj_alpha;
    std::string obj_tex;
    std::vector<float> obj_vertices;
    std::vector<int> obj_indices;
    std::vector<float> obj_texcoords;
    std::vector<float> obj_normals;
    float radius = 0.0f;
    float obj_yaw = 0.0f;
    float obj_pitch = 0.0f;
    float obj_roll = 0.0f;

    const pseudo_3d_entity* entity;
    float cam_x, cam_y, cam_z;

    GLuint shaderID = 0;

    Portal* portal = nullptr;
};

static GLuint skyboxVAO = 0, skyboxVBO = 0, skyboxIBO = 0;
static int skyboxIndexCount = 0;

static std::vector<DrawCommand> drawQueue;
static std::mutex drawQueueMutex;

static bool currentIs2D = false;

static GLuint triTexPos = 0;
static GLuint triTexNorm = 0;
static GLuint triTexColor = 0;
static GLuint triTexIndices = 0;
static int triCount = 0;
static int triTexWidth = 0;
static int triTexHeight = 0;

void ensureTriTextures(int triNeeded) {
    int w = 1, h = 1;
    while (w * h < triNeeded) {
        if (w <= h) w *= 2;
        else h *= 2;
    }
    w = std::min(w, 2048);
    h = std::min(h, 2048);
    if (triTexPos && w == triTexWidth && h == triTexHeight) return;

    if (triTexPos) {
        glDeleteTextures(1, &triTexPos);
        glDeleteTextures(1, &triTexNorm);
        glDeleteTextures(1, &triTexColor);
        glDeleteTextures(1, &triTexIndices);
    }

    triTexWidth = w;
    triTexHeight = h;

    auto createFloatTex = [](GLuint& tex, int w, int h, int components) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, components == 3 ? GL_RGB32F : GL_RGBA32F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    };

    createFloatTex(triTexPos, w, h, 3);
    createFloatTex(triTexNorm, w, h, 3);
    createFloatTex(triTexColor, w, h, 3);
    createFloatTex(triTexIndices, w, h, 3);
}

void flushDrawQueue() {
    applyAllLights();
    if (drawQueue.empty()) return;

    std::vector<DrawCommand> commands2D;
    std::vector<DrawCommand> commands3D;
    std::vector<DrawCommand> panoramaCommands;
    bool hasPortalCommand = false;

    for (auto& cmd : drawQueue) {
        switch (cmd.type) {
            case CMD_PORTAL:
                hasPortalCommand = true;
                break;
            case CMD_SQUARE:
            case CMD_TEXT:
            case CMD_LINE_2D:
                commands2D.push_back(cmd);
                break;
            case CMD_PANORAMA:
                panoramaCommands.push_back(cmd);
                break;
            default:
                commands3D.push_back(cmd);
                break;
        }
    }
    drawQueue.clear();

    if (!commands3D.empty() || !panoramaCommands.empty()) {
        if (currentShaderProg == 0)
            currentShaderProg = defaultLightingShader;

        if (!accumulationInited)
            createAccumulationBuffers(window_w, window_h);

        std::unordered_map<GLuint, int> textureSlotMap;
        std::vector<GLuint> activeTextures;
        activeTextures.reserve(8);

        auto getTextureSlot = [&](GLuint texID) -> int {
            if (texID == 0) return -1;
            auto it = textureSlotMap.find(texID);
            if (it != textureSlotMap.end()) return it->second;
            if (activeTextures.size() >= 8) return -1;
            int slot = (int)activeTextures.size();
            textureSlotMap[texID] = slot;
            activeTextures.push_back(texID);
            return slot;
        };

        int totalTriangles = 0;
        for (auto& cmd : commands3D) {
            if (cmd.type == CMD_3DOBJECT) {
                totalTriangles += cmd.obj_indices.size() / 3;
            } else if (cmd.type == CMD_PSEUDO3D) {
                totalTriangles += 2;
            }
        }
        ensureTriTextures(totalTriangles * 3);

        if (totalTriangles > 0) {
            int totalPixels = triTexWidth * triTexHeight;
            std::vector<float> posData(totalPixels * 3, 0.0f);
            std::vector<float> normData(totalPixels * 3, 0.0f);
            std::vector<float> colData(totalPixels * 4, 0.0f);
            std::vector<float> uvData(totalPixels * 2, 0.0f);
            std::vector<float> idxData(totalPixels * 4, -1.0f);

            int texIdx = 0;
            for (auto& cmd : commands3D) {
                switch (cmd.type) {
                    case CMD_3DOBJECT: {
                        if (cmd.obj_indices.size() < 3) break;
                        const auto& verts = cmd.obj_vertices;
                        const auto& norms = cmd.obj_normals;
                        const auto& uvs   = cmd.obj_texcoords;
                        const auto& idxs  = cmd.obj_indices;
                        float cx = cmd.obj_cx, cy = cmd.obj_cy, cz = cmd.obj_cz;
                        GLuint texID = 0;
                        if (!cmd.obj_tex.empty()) texID = loadTextureFromFile(cmd.obj_tex.c_str());
                        int slot = getTextureSlot(texID);

                        float yaw = cmd.obj_yaw * M_PI / 180.0f;
                        float pitch = cmd.obj_pitch * M_PI / 180.0f;
                        float roll = cmd.obj_roll * M_PI / 180.0f;
                        glm::mat4 rotMat = glm::mat4(1.0f);
                        rotMat = glm::rotate(rotMat, yaw, glm::vec3(0,1,0));
                        rotMat = glm::rotate(rotMat, pitch, glm::vec3(1,0,0));
                        rotMat = glm::rotate(rotMat, roll, glm::vec3(0,0,1));

                        for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
                            if (texIdx >= totalTriangles) break;
                            int i0 = idxs[i]*3, i1 = idxs[i+1]*3, i2 = idxs[i+2]*3;
                            for (int j = 0; j < 3; j++) {
                                int vidx = (j==0?i0:j==1?i1:i2);
                                glm::vec4 local(verts[vidx], verts[vidx+1], verts[vidx+2], 1.0f);
                                glm::vec4 rotated = rotMat * local;
                                int base = texIdx * 9 + j * 3;
                                posData[base+0] = rotated.x + cx;
                                posData[base+1] = rotated.y + cy;
                                posData[base+2] = rotated.z + cz;
                                if (vidx+2 < (int)norms.size()) {
                                    glm::vec4 normLocal(norms[vidx], norms[vidx+1], norms[vidx+2], 0.0f);
                                    glm::vec4 normRotated = rotMat * normLocal;
                                    normData[base+0] = normRotated.x;
                                    normData[base+1] = normRotated.y;
                                    normData[base+2] = normRotated.z;
                                }
                                int colBase = texIdx * 12 + j * 4;
                                colData[colBase+0] = cmd.obj_r;
                                colData[colBase+1] = cmd.obj_g;
                                colData[colBase+2] = cmd.obj_b;
                                colData[colBase+3] = cmd.obj_alpha;
                                int uvBase = texIdx * 6 + j * 2;
                                if (vidx/3*2+1 < (int)uvs.size()) {
                                    uvData[uvBase+0] = uvs[vidx/3*2];
                                    uvData[uvBase+1] = uvs[vidx/3*2+1];
                                }
                                int pixel = texIdx * 3 + j;
                                idxData[pixel*4 + 0] = (float)pixel;
                                idxData[pixel*4 + 1] = 1.0;
                                idxData[pixel*4 + 2] = (float)slot;
                                idxData[pixel*4 + 3] = 0.0;
                            }
                            texIdx++;
                        }
                        break;
                    }
                    case CMD_PSEUDO3D: {
                        if (!cmd.entity) break;
                        const pseudo_3d_entity* ent = cmd.entity;
                        float cx = ent->getX(), cy = ent->getY(), cz = ent->getZ();
                        float camX = camera.eye_x, camY = camera.eye_y, camZ = camera.eye_z;
                        float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                        if (dist < 0.0001f) break;

                        float fx = dx / dist, fy = dy / dist, fz = dz / dist;
                        float wx = 0, wy = 1, wz = 0;
                        if (fabsf(fy) > 0.999f) { wx = 0; wy = 0; wz = 1; }

                        float rx = wy * fz - wz * fy;
                        float ry = wz * fx - wx * fz;
                        float rz = wx * fy - wy * fx;
                        float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
                        if (rlen > 1e-4f) { rx /= rlen; ry /= rlen; rz /= rlen; }

                        float ux = fy * rz - fz * ry;
                        float uy = fz * rx - fx * rz;
                        float uz = fx * ry - fy * rx;

                        float ga = ent->g_angle * M_PI / 180.0f;
                        float va = ent->v_angle * M_PI / 180.0f;
                        float ra = ent->r_angle * M_PI / 180.0f;

                        float eu_x = -sinf(ga) * sinf(va);
                        float eu_y = -cosf(va);
                        float eu_z = -cosf(ga) * sinf(va);

                        float fwx = cosf(va) * sinf(ga);
                        float fwy = -sinf(va);
                        float fwz = cosf(va) * cosf(ga);
                        float len_fw = sqrtf(fwx*fwx + fwy*fwy + fwz*fwz);
                        if (len_fw > 1e-6f) { fwx /= len_fw; fwy /= len_fw; fwz /= len_fw; }

                        float cos_ra = cosf(ra), sin_ra = sinf(ra);
                        float rot_eu_x = eu_x * cos_ra + (fwy*eu_z - fwz*eu_y) * sin_ra + fwx*(fwx*eu_x + fwy*eu_y + fwz*eu_z)*(1-cos_ra);
                        float rot_eu_y = eu_y * cos_ra + (fwz*eu_x - fwx*eu_z) * sin_ra + fwy*(fwx*eu_x + fwy*eu_y + fwz*eu_z)*(1-cos_ra);
                        float rot_eu_z = eu_z * cos_ra + (fwx*eu_y - fwy*eu_x) * sin_ra + fwz*(fwx*eu_x + fwy*eu_y + fwz*eu_z)*(1-cos_ra);
                        eu_x = rot_eu_x; eu_y = rot_eu_y; eu_z = rot_eu_z;

                        float dot_eu = eu_x * fx + eu_y * fy + eu_z * fz;
                        float pu_x = eu_x - dot_eu * fx;
                        float pu_y = eu_y - dot_eu * fy;
                        float pu_z = eu_z - dot_eu * fz;
                        float plen = sqrtf(pu_x*pu_x + pu_y*pu_y + pu_z*pu_z);

                        float roll_raw;
                        if (plen < 0.001f) {
                            roll_raw = 0.0f;
                        } else {
                            roll_raw = atan2f(-(pu_x*rx + pu_y*ry + pu_z*rz),
                                            pu_x*ux + pu_y*uy + pu_z*uz) * 180.0f / M_PI;
                        }

                        int texIdxLocal = ent->getTextureIndex(fx, fy, fz);
                        bool mirror = (texIdxLocal == 0);
                        float net_angle = roll_raw + 180.0f + (mirror ? -180.0f : 0.0f);
                        float roll_rad = net_angle * M_PI / 180.0f;

                        const auto& verts = ent->getVertices();
                        if (verts.size() < 8) break;

                        auto rotateLocal = [roll_rad](float lx, float ly) {
                            float c = cosf(roll_rad), s = sinf(roll_rad);
                            return std::make_pair(lx * c - ly * s, lx * s + ly * c);
                        };

                        float v0x = verts[0], v0y = verts[1];
                        float v1x = verts[2], v1y = verts[3];
                        float v2x = verts[4], v2y = verts[5];
                        float v3x = verts[6], v3y = verts[7];

                        auto [rv0x, rv0y] = rotateLocal(v0x, v0y);
                        auto [rv1x, rv1y] = rotateLocal(v1x, v1y);
                        auto [rv2x, rv2y] = rotateLocal(v2x, v2y);
                        auto [rv3x, rv3y] = rotateLocal(v3x, v3y);

                        glm::vec3 p0(cx + rx*rv0x + ux*rv0y, cy + ry*rv0x + uy*rv0y, cz + rz*rv0x + uz*rv0y);
                        glm::vec3 p1(cx + rx*rv1x + ux*rv1y, cy + ry*rv1x + uy*rv1y, cz + rz*rv1x + uz*rv1y);
                        glm::vec3 p2(cx + rx*rv2x + ux*rv2y, cy + ry*rv2x + uy*rv2y, cz + rz*rv2x + uz*rv2y);
                        glm::vec3 p3(cx + rx*rv3x + ux*rv3y, cy + ry*rv3x + uy*rv3y, cz + rz*rv3x + uz*rv3y);

                        GLuint texID = ent->getTextureID(texIdxLocal);
                        int slot = getTextureSlot(texID);
                        glm::vec3 normal(fx, fy, fz);

                        glm::vec3 triVerts[6] = { p0, p2, p1, p0, p3, p2 };
                        glm::vec2 uvsArr[6] = {
                            glm::vec2(0,1), glm::vec2(1,0), glm::vec2(1,1),
                            glm::vec2(0,1), glm::vec2(0,0), glm::vec2(1,0)
                        };

                        for (int tri = 0; tri < 2; tri++) {
                            if (texIdx >= totalTriangles) break;
                            for (int j = 0; j < 3; j++) {
                                int idx = tri*3 + j;
                                int base = texIdx * 9 + j * 3;
                                posData[base+0] = triVerts[idx].x;
                                posData[base+1] = triVerts[idx].y;
                                posData[base+2] = triVerts[idx].z;
                                normData[base+0] = normal.x;
                                normData[base+1] = normal.y;
                                normData[base+2] = normal.z;
                                int colBase = texIdx * 12 + j * 4;
                                colData[colBase+0] = 1.0f;
                                colData[colBase+1] = 1.0f;
                                colData[colBase+2] = 1.0f;
                                colData[colBase+3] = 1.0f;
                                int uvBase = texIdx * 6 + j * 2;
                                uvData[uvBase+0] = uvsArr[idx].x;
                                uvData[uvBase+1] = uvsArr[idx].y;
                                int pixel = texIdx * 3 + j;
                                idxData[pixel*4 + 0] = (float)pixel;
                                idxData[pixel*4 + 1] = 0.0;
                                idxData[pixel*4 + 2] = (float)slot;
                                idxData[pixel*4 + 3] = 0.0;
                            }
                            texIdx++;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            triCount = totalTriangles;

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, triTexPos);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, triTexWidth, triTexHeight, GL_RGB, GL_FLOAT, posData.data());

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, triTexNorm);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, triTexWidth, triTexHeight, GL_RGB, GL_FLOAT, normData.data());

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, triTexColor);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, triTexWidth, triTexHeight, GL_RGBA, GL_FLOAT, colData.data());

            if (triTexUV == 0) {
                glGenTextures(1, &triTexUV);
                glBindTexture(GL_TEXTURE_2D, triTexUV);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, triTexWidth, triTexHeight, 0, GL_RG, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, triTexUV);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, triTexWidth, triTexHeight, GL_RG, GL_FLOAT, uvData.data());

            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, triTexIndices);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, triTexWidth, triTexHeight, GL_RGBA, GL_FLOAT, idxData.data());
        }

        glBindFramebuffer(GL_FRAMEBUFFER, accumulationFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        useShader(currentShaderProg);

        static GLint loc_raycast = -1, loc_displayMode = -1, loc_invViewProj = -1, loc_camPos = -1;
        static GLint loc_ambient = -1, loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;
        static GLint loc_numLights = -1, loc_panoramaTex = -1, loc_hasPanorama = -1;
        static GLint loc_triCount = -1, loc_triTexWidth = -1, loc_triTexHeight = -1;
        static GLint loc_triTexPos = -1, loc_triTexNorm = -1, loc_triTexColor = -1, loc_triTexIndices = -1, loc_triTexUV = -1;
        static GLint loc_portalCount = -1, loc_portalPos = -1, loc_portalNormal = -1, loc_portalD = -1;
        static GLint loc_portalInvWorld = -1, loc_portalVertCount = -1, loc_portalVerts = -1, loc_portalTeleport = -1;
        static GLint loc_lightEnabled[MAX_LIGHTS] = {0}, loc_lightPos[MAX_LIGHTS] = {0}, loc_lightDir[MAX_LIGHTS] = {0};
        static GLint loc_lightDiffuse[MAX_LIGHTS] = {0}, loc_lightCutoff[MAX_LIGHTS] = {0}, loc_lightAtten[MAX_LIGHTS] = {0};
        static GLint loc_texSlot[8] = {0};
        static bool uniformsCached = false;
        if (!uniformsCached) {
            loc_raycast = glGetUniformLocation(currentShaderProg, "raycast");
            loc_displayMode = glGetUniformLocation(currentShaderProg, "displayMode");
            loc_invViewProj = glGetUniformLocation(currentShaderProg, "invViewProj");
            loc_camPos = glGetUniformLocation(currentShaderProg, "camPos");
            loc_ambient = glGetUniformLocation(currentShaderProg, "ambientLight");
            loc_fogColor = glGetUniformLocation(currentShaderProg, "fogColor");
            loc_fogStart = glGetUniformLocation(currentShaderProg, "fogStart");
            loc_fogEnd = glGetUniformLocation(currentShaderProg, "fogEnd");
            loc_numLights = glGetUniformLocation(currentShaderProg, "numLights");
            loc_panoramaTex = glGetUniformLocation(currentShaderProg, "panoramaTex");
            loc_hasPanorama = glGetUniformLocation(currentShaderProg, "hasPanorama");
            loc_triCount = glGetUniformLocation(currentShaderProg, "triCount");
            loc_triTexWidth = glGetUniformLocation(currentShaderProg, "triTexWidth");
            loc_triTexHeight = glGetUniformLocation(currentShaderProg, "triTexHeight");
            loc_triTexPos = glGetUniformLocation(currentShaderProg, "triTexPos");
            loc_triTexNorm = glGetUniformLocation(currentShaderProg, "triTexNorm");
            loc_triTexColor = glGetUniformLocation(currentShaderProg, "triTexColor");
            loc_triTexIndices = glGetUniformLocation(currentShaderProg, "triTexIndices");
            loc_triTexUV = glGetUniformLocation(currentShaderProg, "triTexUV");
            loc_portalCount = glGetUniformLocation(currentShaderProg, "portalCount");
            loc_portalPos = glGetUniformLocation(currentShaderProg, "portalPos");
            loc_portalNormal = glGetUniformLocation(currentShaderProg, "portalNormal");
            loc_portalD = glGetUniformLocation(currentShaderProg, "portalD");
            loc_portalInvWorld = glGetUniformLocation(currentShaderProg, "portalInvWorld");
            loc_portalVertCount = glGetUniformLocation(currentShaderProg, "portalVertCount");
            loc_portalVerts = glGetUniformLocation(currentShaderProg, "portalVerts");
            loc_portalTeleport = glGetUniformLocation(currentShaderProg, "portalTeleport");
            for (int i = 0; i < MAX_LIGHTS; i++) {
                char buf[64];
                snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
                loc_lightEnabled[i] = glGetUniformLocation(currentShaderProg, buf);
                snprintf(buf, sizeof(buf), "lights[%d].position", i);
                loc_lightPos[i] = glGetUniformLocation(currentShaderProg, buf);
                snprintf(buf, sizeof(buf), "lights[%d].direction", i);
                loc_lightDir[i] = glGetUniformLocation(currentShaderProg, buf);
                snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
                loc_lightDiffuse[i] = glGetUniformLocation(currentShaderProg, buf);
                snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
                loc_lightCutoff[i] = glGetUniformLocation(currentShaderProg, buf);
                snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
                loc_lightAtten[i] = glGetUniformLocation(currentShaderProg, buf);
            }
            for (int i = 0; i < 8; i++) {
                char name[32];
                snprintf(name, sizeof(name), "textures[%d]", i);
                loc_texSlot[i] = glGetUniformLocation(currentShaderProg, name);
            }
            uniformsCached = true;
        }

        if (loc_displayMode != -1) glUniform1i(loc_displayMode, 0);
        if (loc_raycast != -1) glUniform1i(loc_raycast, 1);

        float proj[16], view[16];
        glGetFloatv(GL_PROJECTION_MATRIX, proj);
        glGetFloatv(GL_MODELVIEW_MATRIX, view);
        glm::mat4 projMat, viewMat;
        memcpy(&projMat[0][0], proj, 16*sizeof(float));
        memcpy(&viewMat[0][0], view, 16*sizeof(float));
        glm::mat4 viewProj = projMat * viewMat;
        glm::mat4 invVP = glm::inverse(viewProj);
        float invViewProj[16];
        memcpy(invViewProj, &invVP[0][0], 16*sizeof(float));

        if (loc_invViewProj != -1) glUniformMatrix4fv(loc_invViewProj, 1, GL_FALSE, invViewProj);
        if (loc_camPos != -1) glUniform3f(loc_camPos, camera.eye_x, camera.eye_y, camera.eye_z);
        if (loc_ambient != -1) glUniform3fv(loc_ambient, 1, global_ambient);
        if (loc_fogColor != -1) glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
        if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
        if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);

        int nLights = std::min((int)activeLights.size(), MAX_LIGHTS);
        if (loc_numLights != -1) glUniform1i(loc_numLights, nLights);
        for (int i = 0; i < nLights; i++) {
            Light* l = activeLights[i];
            if (loc_lightEnabled[i] != -1) glUniform1i(loc_lightEnabled[i], 1);
            if (loc_lightPos[i] != -1) glUniform3f(loc_lightPos[i], l->pos[0], l->pos[1], l->pos[2]);
            if (loc_lightDir[i] != -1) glUniform3f(loc_lightDir[i], l->dir[0], l->dir[1], l->dir[2]);
            if (loc_lightDiffuse[i] != -1) glUniform3f(loc_lightDiffuse[i], l->color[0]*l->intensity, l->color[1]*l->intensity, l->color[2]*l->intensity);
            if (loc_lightCutoff[i] != -1) glUniform1f(loc_lightCutoff[i], cosf(l->cutoff * M_PI / 180.0f));
            if (loc_lightAtten[i] != -1) glUniform3f(loc_lightAtten[i], l->constAtt, l->linearAtt, l->quadAtt);
        }

        glUniform1i(loc_triCount, triCount);
        glUniform1i(loc_triTexWidth, triTexWidth);
        glUniform1i(loc_triTexHeight, triTexHeight);
        glUniform1i(loc_triTexPos, 2);
        glUniform1i(loc_triTexNorm, 3);
        glUniform1i(loc_triTexColor, 4);
        glUniform1i(loc_triTexIndices, 5);
        glUniform1i(loc_triTexUV, 6);

        for (int i = 0; i < (int)activeTextures.size(); i++) {
            if (loc_texSlot[i] != -1) {
                glActiveTexture(GL_TEXTURE7 + i);
                glBindTexture(GL_TEXTURE_2D, activeTextures[i]);
                glUniform1i(loc_texSlot[i], 7 + i);
            }
        }
        for (int i = activeTextures.size(); i < 8; i++) {
            if (loc_texSlot[i] != -1) glUniform1i(loc_texSlot[i], 0);
        }

        if (!panoramaCommands.empty() && sphere_sky.texture != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
            if (loc_panoramaTex != -1) glUniform1i(loc_panoramaTex, 1);
            if (loc_hasPanorama != -1) glUniform1i(loc_hasPanorama, 1);
        } else {
            if (loc_hasPanorama != -1) glUniform1i(loc_hasPanorama, 0);
        }

        const int MAX_PORTALS = 8;
        const int MAX_PORTAL_VERTS = 16;
        int portalCount = 0;
        float portalPos[MAX_PORTALS * 3] = {0};
        float portalNormal[MAX_PORTALS * 3] = {0};
        float portalD[MAX_PORTALS] = {0};
        float portalInvWorld[MAX_PORTALS * 16] = {0};
        int   portalVertCount[MAX_PORTALS] = {0};
        float portalVerts[MAX_PORTALS * MAX_PORTAL_VERTS * 2] = {0};
        float portalTeleport[MAX_PORTALS * 16] = {0};

        if (hasPortalCommand) {
            for (Portal* p : allPortals) {
                if (portalCount >= MAX_PORTALS) break;

                auto addPortalSide = [&](float px, float py, float pz,
                                         float yaw, float pitch, float roll,
                                         float nx, float ny, float nz,
                                         const glm::mat4& teleport) {
                    portalPos[portalCount*3 + 0] = px;
                    portalPos[portalCount*3 + 1] = py;
                    portalPos[portalCount*3 + 2] = pz;

                    portalNormal[portalCount*3 + 0] = nx;
                    portalNormal[portalCount*3 + 1] = ny;
                    portalNormal[portalCount*3 + 2] = nz;

                    portalD[portalCount] = -(nx*px + ny*py + nz*pz);

                    glm::mat4 rot = glm::mat4(1.0f);
                    rot = glm::rotate(rot, glm::radians(yaw),   glm::vec3(0,1,0));
                    rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1,0,0));
                    rot = glm::rotate(rot, glm::radians(roll),  glm::vec3(0,0,1));
                    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(px,py,pz)) * rot;
                    glm::mat4 invWorld = glm::inverse(world);
                    for (int c = 0; c < 4; ++c)
                        for (int r = 0; r < 4; ++r)
                            portalInvWorld[portalCount*16 + c*4 + r] = invWorld[c][r];

                    int vCount = (int)p->vertices.size() / 3;
                    portalVertCount[portalCount] = vCount;
                    for (int i = 0; i < vCount && i < MAX_PORTAL_VERTS; ++i) {
                        portalVerts[portalCount * MAX_PORTAL_VERTS * 2 + i*2 + 0] = p->vertices[i*3 + 0];
                        portalVerts[portalCount * MAX_PORTAL_VERTS * 2 + i*2 + 1] = p->vertices[i*3 + 1];
                    }

                    for (int c = 0; c < 4; ++c)
                        for (int r = 0; r < 4; ++r)
                            portalTeleport[portalCount*16 + c*4 + r] = teleport[c][r];

                    portalCount++;
                };

                glm::vec3 nA = p->portalNormal(p->ax, p->ay, p->az, false);
                glm::mat4 teleAtoB = p->getPortalTransform(p->ax, p->ay, p->az, p->bx, p->by, p->bz);
                addPortalSide(p->ax, p->ay, p->az,
                              p->yawA, p->pitchA, p->rollA,
                              nA.x, nA.y, nA.z,
                              teleAtoB);

                if (portalCount < MAX_PORTALS) {
                    glm::vec3 nB = p->portalNormal(p->bx, p->by, p->bz, true);
                    glm::mat4 teleBtoA = p->getPortalTransform(p->bx, p->by, p->bz, p->ax, p->ay, p->az);
                    addPortalSide(p->bx, p->by, p->bz,
                                  p->yawB, p->pitchB, p->rollB,
                                  nB.x, nB.y, nB.z,
                                  teleBtoA);
                }
            }
        }

        glUniform1i(loc_portalCount, portalCount);
        if (portalCount > 0) {
            glUniform3fv(loc_portalPos, portalCount, portalPos);
            glUniform3fv(loc_portalNormal, portalCount, portalNormal);
            glUniform1fv(loc_portalD, portalCount, portalD);
            glUniformMatrix4fv(loc_portalInvWorld, portalCount, GL_FALSE, portalInvWorld);
            glUniform1iv(loc_portalVertCount, portalCount, portalVertCount);
            glUniform2fv(loc_portalVerts, MAX_PORTALS * MAX_PORTAL_VERTS, portalVerts);
            glUniformMatrix4fv(loc_portalTeleport, portalCount, GL_FALSE, portalTeleport);
        }

        glActiveTexture(GL_TEXTURE0);

        static GLuint fullScreenVAO = 0, fullScreenVBO = 0;
        if (fullScreenVAO == 0) {
            float quad[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
            glGenVertexArrays(1, &fullScreenVAO);
            glGenBuffers(1, &fullScreenVBO);
            glBindVertexArray(fullScreenVAO);
            glBindBuffer(GL_ARRAY_BUFFER, fullScreenVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glBindVertexArray(0);
        }

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(fullScreenVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glDisable(GL_BLEND);
        useShader(currentShaderProg);
        if (loc_displayMode != -1) glUniform1i(loc_displayMode, 1);
        if (loc_raycast != -1) glUniform1i(loc_raycast, 0);
        GLint loc_accumulationTex = glGetUniformLocation(currentShaderProg, "accumulationTex");
        glUniform1i(loc_accumulationTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumulationTex);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(fullScreenVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();

        glBindTexture(GL_TEXTURE_2D, 0);
        glEnable(GL_BLEND);
    }

    if (!commands2D.empty()) {
        static GLuint sq_vao = 0, sq_vbo = 0, sq_ibo = 0;
        static bool sq_init = false;
        if (!sq_init) {
            sq_init = true;
            glGenVertexArrays(1, &sq_vao);
            glGenBuffers(1, &sq_vbo);
            glGenBuffers(1, &sq_ibo);
            glBindVertexArray(sq_vao);
            glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
            glBufferData(GL_ARRAY_BUFFER, 4 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
            GLuint indices[6] = {0, 1, 2, 0, 2, 3};
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sq_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
            glBindVertexArray(0);
            ensureWhiteTex();
        }

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        glOrtho(0, window_w, 0, window_h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glDisable(GL_CULL_FACE);
        glColor4f(1,1,1,1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLuint shader2D = simple2DShader;
        useShader(shader2D);
        glUniform1i(loc_tex_2d, 0);

        for (auto& cmd : commands2D) {
            switch (cmd.type) {
                case CMD_SQUARE: {
                    const char* texName = cmd.tex.empty() ? nullptr : cmd.tex.c_str();
                    glActiveTexture(GL_TEXTURE0);
                    if (texName) {
                        GLuint id = loadTextureFromFile(texName);
                        if (id) glBindTexture(GL_TEXTURE_2D, id);
                        else { ensureWhiteTex(); glBindTexture(GL_TEXTURE_2D, whiteTex); }
                    } else { ensureWhiteTex(); glBindTexture(GL_TEXTURE_2D, whiteTex); }
                    float ar = cmd.rotate * M_PI / -180.0f;
                    float tc[8] = {0,1, 1,1, 1,0, 0,0};
                    float data[32];
                    for (int i = 0; i < 4; ++i) {
                        float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                        rotatePoint(px, py, 0, 0, ar);
                        float vx = cmd.cx + px * cmd.scale;
                        float vy = cmd.cy + py * cmd.scale;
                        data[i*8+0] = vx; data[i*8+1] = vy;
                        data[i*8+2] = cmd.r; data[i*8+3] = cmd.g; data[i*8+4] = cmd.b; data[i*8+5] = cmd.a;
                        data[i*8+6] = tc[i*2]; data[i*8+7] = tc[i*2+1];
                    }
                    glBindVertexArray(sq_vao);
                    glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                    break;
                }
                case CMD_TEXT: {
                    GLuint prevShader = currentShaderProg;
                    if (currentShaderProg) stopShader();
                    glDisable(GL_TEXTURE_2D);
                    glColor4f(cmd.r, cmd.g, cmd.b, cmd.a);
                    glWindowPos2f(cmd.x, cmd.y);
                    for (const char* c = cmd.text.c_str(); *c; ++c)
                        glutBitmapCharacter(cmd.font, *c);
                    glEnable(GL_TEXTURE_2D);
                    if (prevShader) useShader(prevShader);
                    break;
                }
                case CMD_LINE_2D: {
                    float data[2*9] = {0};
                    data[0] = cmd.verts[0]; data[1] = cmd.verts[1]; data[2] = 0;
                    data[3] = cmd.r; data[4] = cmd.g; data[5] = cmd.b; data[6] = cmd.a;
                    data[7] = 0; data[8] = 0;
                    data[9] = cmd.verts[2]; data[10] = cmd.verts[3]; data[11] = 0;
                    data[12] = cmd.r; data[13] = cmd.g; data[14] = cmd.b; data[15] = cmd.a;
                    data[16] = 0; data[17] = 0;
                    glBindVertexArray(lineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, whiteTex);
                    glLineWidth(cmd.scale);
                    glDrawArrays(GL_LINES, 0, 2);
                    glLineWidth(1.0f);
                    glBindVertexArray(0);
                    break;
                }
                default:
                    break;
            }
        }

        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_CULL_FACE);
    }
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
// отрезок
void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2, float r, float g, float b, float a, float thickness) {
    DrawCommand cmd;
    cmd.type = CMD_LINE_2D;
    cmd.verts[0] = x + x1; cmd.verts[1] = y + y1;
    cmd.verts[2] = x + x2; cmd.verts[3] = y + y2;
    cmd.r = r; cmd.g = g; cmd.b = b; cmd.a = a;
    cmd.scale = thickness;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
}
// квадрат
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex, float alpha){
    DrawCommand cmd;
    cmd.type = CMD_SQUARE;
    cmd.scale = local_size;
    cmd.cx = x;
    cmd.cy = y;
    cmd.r = (float)r;
    cmd.g = (float)g;
    cmd.b = (float)b;
    cmd.a = alpha;
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

GLuint pseudo_3d_entity::getTextureFromDirection(float lx, float ly, float lz) const {
    int idx = getTextureIndex(lx, ly, lz);
    if (idx < 0 || idx >= (int)textureIDs.size()) return 0;
    return textureIDs[idx];
}

GLuint pseudo_3d_entity::getShadowTexture(float lx, float ly, float lz) const {
    return getTextureFromDirection(lx, ly, lz);
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
    if (accumulationInited)
        createAccumulationBuffers(width, height);
    if (currentIs2D)
        changeSize2D(width, height);
    else
        changeSize3D(width, height);
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

    #ifdef _WIN32
        cpu_name = getCPUName_Win();
        ram_v = getRAMTotal_Win();
    #else
        cpu_name = getCPUName_Linux();
        ram_v = getRAMTotal_Linux();
    #endif
        gpu_name = getGPUName_OpenGL();

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
    initLineVAO();
    createAccumulationBuffers(w, h);
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

    camera.up_x = up_x;
    camera.up_y = up_y;
    camera.up_z = up_z;

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

    camera.up_x = up_x;
    camera.up_y = up_y;
    camera.up_z = up_z;

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
// отрезок
void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float a, float thickness,
                  int segments, float alpha) {
    if (segments < 3) segments = 3; 

    float wx1 = x + x1, wy1 = y + y1, wz1 = z + z1;
    float wx2 = x + x2, wy2 = y + y2, wz2 = z + z2;
    float dx = wx2 - wx1, dy = wy2 - wy1, dz = wz2 - wz1;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    float ndx = dx / len, ndy = dy / len, ndz = dz / len;

    float upx = 0, upy = 0, upz = 1;
    if (fabs(ndx) < 0.001f && fabs(ndz) < 0.001f) {
        upx = 1; upy = 0; upz = 0;
    }

    float rx = upy*ndz - upz*ndy;
    float ry = upz*ndx - upx*ndz;
    float rz = upx*ndy - upy*ndx;
    float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
    rx /= rlen; ry /= rlen; rz /= rlen;

    float ux = ndy*rz - ndz*ry;
    float uy = ndz*rx - ndx*rz;
    float uz = ndx*ry - ndy*rx;

    float half = thickness * 0.5f;

    int totalVerts = 2 * segments + 2;
    std::vector<float> vertices(totalVerts * 3);
    std::vector<float> normals(totalVerts * 3);
    std::vector<int> indices;

    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        float c = cosf(angle) * half;
        float s = sinf(angle) * half;

        vertices[i*3 + 0] = wx1 + rx * c + ux * s;
        vertices[i*3 + 1] = wy1 + ry * c + uy * s;
        vertices[i*3 + 2] = wz1 + rz * c + uz * s;

        float nx = rx * cosf(angle) + ux * sinf(angle);
        float ny = ry * cosf(angle) + uy * sinf(angle);
        float nz = rz * cosf(angle) + uz * sinf(angle);
        normals[i*3 + 0] = nx;
        normals[i*3 + 1] = ny;
        normals[i*3 + 2] = nz;
    }

    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        float c = cosf(angle) * half;
        float s = sinf(angle) * half;

        vertices[(i + segments)*3 + 0] = wx2 + rx * c + ux * s;
        vertices[(i + segments)*3 + 1] = wy2 + ry * c + uy * s;
        vertices[(i + segments)*3 + 2] = wz2 + rz * c + uz * s;

        float nx = rx * cosf(angle) + ux * sinf(angle);
        float ny = ry * cosf(angle) + uy * sinf(angle);
        float nz = rz * cosf(angle) + uz * sinf(angle);
        normals[(i + segments)*3 + 0] = nx;
        normals[(i + segments)*3 + 1] = ny;
        normals[(i + segments)*3 + 2] = nz;
    }

    int idxStartApex = 2 * segments;
    vertices[idxStartApex*3 + 0] = wx1 - ndx * half;
    vertices[idxStartApex*3 + 1] = wy1 - ndy * half;
    vertices[idxStartApex*3 + 2] = wz1 - ndz * half;
    normals[idxStartApex*3 + 0] = -ndx;
    normals[idxStartApex*3 + 1] = -ndy;
    normals[idxStartApex*3 + 2] = -ndz;

    int idxEndApex = 2 * segments + 1;
    vertices[idxEndApex*3 + 0] = wx2 + ndx * half;
    vertices[idxEndApex*3 + 1] = wy2 + ndy * half;
    vertices[idxEndApex*3 + 2] = wz2 + ndz * half;
    normals[idxEndApex*3 + 0] = ndx;
    normals[idxEndApex*3 + 1] = ndy;
    normals[idxEndApex*3 + 2] = ndz;

    for (int i = 0; i < segments; ++i) {
        int i0 = i;
        int i1 = (i + 1) % segments;
        int j0 = i + segments;
        int j1 = i1 + segments;

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(j0);

        indices.push_back(i1);
        indices.push_back(j1);
        indices.push_back(j0);
    }

    for (int i = 0; i < segments; ++i) {
        int i1 = (i + 1) % segments;
        indices.push_back(idxStartApex);
        indices.push_back(i1);
        indices.push_back(i);
    }

    for (int i = 0; i < segments; ++i) {
        int i1 = (i + 1) % segments;
        indices.push_back(idxEndApex);
        indices.push_back(i + segments);
        indices.push_back(i1 + segments);
    }

    std::vector<float> texcoords(totalVerts * 2, 0.0f);

    draw3DObject(0, 0, 0, r, g, b, nullptr, vertices, indices, texcoords, normals, 0.0f, 0.0f, 0.0f, alpha);
}
// рисуем 3д объект, указывая вершины треугольников
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw, float pitch , float roll,
                  float alpha){
    float maxDist = 0.0f;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        float dx = vertices[i] - cx;
        float dy = vertices[i+1] - cy;
        float dz = vertices[i+2] - cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > maxDist) maxDist = d2;
    }
    float radius = sqrtf(maxDist);
    DrawCommand cmd;
    cmd.type = CMD_3DOBJECT;
    cmd.obj_cx = cx; cmd.obj_cy = cy; cmd.obj_cz = cz;
    cmd.obj_r = (float)r; cmd.obj_g = (float)g; cmd.obj_b = (float)b;
    cmd.obj_alpha = alpha;
    cmd.obj_yaw = yaw; cmd.obj_pitch = pitch; cmd.obj_roll = roll;
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
        if (currentShaderProg == 0)
            currentShaderProg = defaultLightingShader;
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
void draw_performance_hud(int win_w, int win_h) {
    static int frame_cnt = 0;
    static double fps = 0.0;
    static auto prev_time = std::chrono::steady_clock::now();
    static double cpu_usage = 0.0;
    static long ram_usage_mb = 0;
    static float gpu_usage = -1.0f; // -1 = N/A

    ++frame_cnt;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - prev_time).count();

    if (elapsed >= 1.0) {
        fps = frame_cnt / elapsed;
        frame_cnt = 0;

#ifdef _WIN32
        cpu_usage = getProcessCPUUsage_Win();
        ram_usage_mb = getProcessRAMUsage_Win();
        gpu_usage = getGPUUsage_Win();
#else
        static long prev_cpu_total = 0;
        cpu_usage = getProcessCPUUsage_Linux(prev_cpu_total);
        ram_usage_mb = getProcessRAMUsage_Linux();
        gpu_usage = getGPUUsage_Linux();
#endif
        prev_time = now;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "FPS: %.0f  RAM: %ld MB  CPU: %.1f%%  GPU: ",
             fps, ram_usage_mb, cpu_usage);
    if (gpu_usage >= 0.0f) {
        char gpu_str[32];
        snprintf(gpu_str, sizeof(gpu_str), "%.1f%%", gpu_usage);
        strcat(buf, gpu_str);
    } else {
        strcat(buf, "N/A");
    }
    draw_text(buf, 10.0f, float(win_h) - 20.0f, GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);

    snprintf(buf, sizeof(buf), "X: %.10f  Y: %.10f  Z: %.10f",
             camera.eye_x, camera.eye_y, camera.eye_z);
    draw_text(buf, 10.0f, float(win_h) - 32.0f, GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);
    snprintf(buf, sizeof(buf), "CPU: %s  RAM: %s  GPU: %s",
             cpu_name.c_str(), ram_v.c_str(), gpu_name.c_str());
    draw_text(buf, 10.0f, float(win_h) - 44.0f, GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);
}
// панорама
sphere_panorama sphere_sky;
 
void set_panorama(const char* path) {
    if (sphere_sky.enabled) remove_panorama();

    sphere_sky.texture = SOIL_load_OGL_texture(
        path,
        SOIL_LOAD_AUTO,
        SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT
    );

    if (sphere_sky.texture == 0) return;

    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    float radius = 180.0f;
    int stacks = 32, slices = 32;

    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<float> colors;
    std::vector<unsigned int> indices;

    for (int i = 0; i <= stacks; ++i) {
        float lat0 = M_PI * (-0.5f + (float)i / stacks);
        float z0 = sin(lat0), zr0 = cos(lat0);
        for (int j = 0; j <= slices; ++j) {
            float lng = 2.0f * M_PI * (float)j / slices;
            float x = cos(lng), y = sin(lng);
            vertices.push_back(x * zr0 * radius);
            vertices.push_back(y * zr0 * radius);
            vertices.push_back(z0 * radius);
            texcoords.push_back((float)j / slices);
            texcoords.push_back((float)i / stacks);
            colors.push_back(1.0f); colors.push_back(1.0f); colors.push_back(1.0f); colors.push_back(1.0f);
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            unsigned int first = i * (slices + 1) + j;
            unsigned int second = first + slices + 1;
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    skyboxIndexCount = (int)indices.size();

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glGenBuffers(1, &skyboxIBO);

    glBindVertexArray(skyboxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    GLuint texVBO;
    glGenBuffers(1, &texVBO);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO);
    glBufferData(GL_ARRAY_BUFFER, texcoords.size() * sizeof(float), texcoords.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    GLuint colVBO;
    glGenBuffers(1, &colVBO);
    glBindBuffer(GL_ARRAY_BUFFER, colVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glDeleteBuffers(1, &texVBO);
    glDeleteBuffers(1, &colVBO);

    sphere_sky.enabled = true;
    sphere_sky.path = path;
}
void remove_panorama() {
    if (!sphere_sky.enabled) return;
    glDeleteTextures(1, &sphere_sky.texture);
    if (skyboxVAO) { glDeleteVertexArrays(1, &skyboxVAO); skyboxVAO = 0; }
    if (skyboxVBO) { glDeleteBuffers(1, &skyboxVBO); skyboxVBO = 0; }
    if (skyboxIBO) { glDeleteBuffers(1, &skyboxIBO); skyboxIBO = 0; }
    skyboxIndexCount = 0;
    sphere_sky.enabled = false;
}
void draw_panorama(float camX, float camY, float camZ) {
    if (!sphere_sky.enabled || sphere_sky.texture == 0 || skyboxVAO == 0) return;
    DrawCommand cmd;
    cmd.type = CMD_PANORAMA;
    cmd.obj_cx = camX; cmd.obj_cy = camY; cmd.obj_cz = camZ;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
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
    allPortals.push_back(this);
}

Portal::~Portal() {
    auto it = std::find(allPortals.begin(), allPortals.end(), this);
    if (it != allPortals.end()) allPortals.erase(it);
}

bool Portal::pointInPortalPolygon(const glm::vec2& point) const {
    int n = vertices.size() / 3;
    if (n < 3) return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = vertices[i * 3];
        float yi = vertices[i * 3 + 1];
        float xj = vertices[j * 3];
        float yj = vertices[j * 3 + 1];
        if (((yi > point.y) != (yj > point.y)) &&
            (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

glm::vec3 Portal::portalNormal(float px, float py, float pz, bool sideB) const {
    glm::vec3 localNormal(0.0f, 0.0f, -1.0f);
    if (sideB) localNormal = glm::vec3(0.0f, 0.0f, 1.0f); 

    float yaw   = sideB ? yawB   : yawA;
    float pitch = sideB ? pitchB : pitchA;
    float roll  = sideB ? rollB  : rollA;

    glm::mat4 rot4 = glm::mat4(1.0f);
    rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat3 rot = glm::mat3(rot4);

    return glm::normalize(rot * localNormal);
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

void Portal::draw() {
    checkTeleport();

    DrawCommand cmd;
    cmd.type = CMD_PORTAL;
    cmd.portal = this;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
}

bool Portal::teleportRay(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                         glm::vec3& newOrigin, glm::vec3& newDir) const {
    for (int side = 0; side < 2; ++side) {
        float px = (side == 0) ? ax : bx;
        float py = (side == 0) ? ay : by;
        float pz = (side == 0) ? az : bz;
        float yaw   = (side == 0) ? yawA : yawB;
        float pitch = (side == 0) ? pitchA : pitchB;
        float roll  = (side == 0) ? rollA : rollB;

        glm::vec3 N = portalNormal(px, py, pz, side == 1);
        float denom = glm::dot(dir, N);
        if (fabs(denom) < 0.0001f) continue;  
        if (denom > 0.0f) continue;           

        float t = glm::dot(N, glm::vec3(px, py, pz) - origin) / denom;
        if (t < 0.0f || t > maxDist) continue;

        glm::vec3 hitPoint = origin + dir * t;

        glm::mat4 worldSrc = glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0,1,0))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1,0,0))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(roll), glm::vec3(0,0,1));
        glm::vec3 localPt = glm::vec3(glm::inverse(worldSrc) * glm::vec4(hitPoint, 1.0f));

        if (!pointInPortalPolygon(glm::vec2(localPt.x, localPt.y)))
            continue;

        float dx = (side == 0) ? bx : ax;
        float dy = (side == 0) ? by : ay;
        float dz = (side == 0) ? bz : az;
        glm::mat4 transform = getPortalTransform(px, py, pz, dx, dy, dz);

        newOrigin = glm::vec3(transform * glm::vec4(hitPoint, 1.0f));
        glm::mat3 rotMat = glm::mat3(transform);
        newDir = glm::normalize(rotMat * dir);

        return true;
    }
    return false;
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