#include "portals_rc.h"
#include "pseudo3dentity.h"
#include "light.h"
#include "ambient.h"
#include "audio_not_mini.h"
#include "2d_primitives.h"
#include "3d_primitives.h"
#include "shaders.h"
#include "textures.h"
#include "warp.h"
#include "baking_scene.h"

// #define MINIAUDIO_IMPLEMENTATION
// #include "src/miniaudio.h"

// указываем что пользуемся только указанным api для воспроизведения звука(шиндовс или линукс)
#ifdef _WIN32
  #define MA_ENABLE_WASAPI
#else
  #define MA_ENABLE_ALSA
#endif
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
// импортируем сам miniaudio
// #include "src/miniaudio.h"

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
// основная библиотека opengl
#include <GLFW/glfw3.h>
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
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    return renderer ? std::string(renderer) : "Unknown GPU";
}
#else
#include <fstream>
#include <sstream>
#include <cstring>

std::string getCPUName_Linux() {
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

std::string getRAMTotal_Linux() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            size_t pos = line.find(":");
            std::string val = line.substr(pos + 1);
            size_t kbpos = val.find("kB");
            if (kbpos != std::string::npos)
                val = val.substr(0, kbpos);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            long kb = std::stol(val);
            return std::to_string(kb / 1024) + " MB";
        }
    }
    return "Unknown RAM";
}

std::string getGPUName_OpenGL() {
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
std::vector<float> getProcessCPUUsage_Win() {
    static PDH_HQUERY cpuQuery = NULL;
    static std::vector<PDH_HCOUNTER> counters;
    static bool initialized = false;
    std::vector<float> usages;

    if (!initialized) {
        if (PdhOpenQuery(NULL, 0, &cpuQuery) != ERROR_SUCCESS) return usages;
        char path[256];
        for (int i = 0; ; i++) {
            sprintf(path, "\\Processor(%d)\\%% Processor Time", i);
            PDH_HCOUNTER counter;
            if (PdhAddCounter(cpuQuery, path, 0, &counter) == ERROR_SUCCESS) {
                counters.push_back(counter);
            } else {
                break;
            }
        }
        if (!counters.empty()) {
            PdhCollectQueryData(cpuQuery);
        }
        initialized = true;
    }

    if (!counters.empty()) {
        PdhCollectQueryData(cpuQuery);
        DWORD dwType;
        PDH_FMT_COUNTERVALUE Value;
        for (size_t i = 0; i < counters.size(); ++i) {
            if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, &dwType, &Value) == ERROR_SUCCESS) {
                usages.push_back((float)Value.doubleValue / 10.0f);
            }
        }
    }
    return usages;
}

long getProcessRAMUsage_Win() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024 * 1024);
    return 0;
}

float getGPUUsage_Win() {
    static PDH_HQUERY gpuQuery = NULL;
    static std::vector<PDH_HCOUNTER> gpuCounters;
    static bool initialized = false;

    if (!initialized) {
        if (PdhOpenQuery(NULL, 0, &gpuQuery) != ERROR_SUCCESS) return -1.0f;
        char path[256];
        for (int i = 0; ; i++) {
            sprintf(path, "\\GPU Engine(%d engtype_3D)\\Utilization Percentage", i);
            PDH_HCOUNTER counter;
            if (PdhAddCounter(gpuQuery, path, 0, &counter) == ERROR_SUCCESS) {
                gpuCounters.push_back(counter);
            } else {
                break;
            }
        }
        if (!gpuCounters.empty()) {
            PdhCollectQueryData(gpuQuery);
        }
        initialized = true;
    }

    if (gpuCounters.empty()) return -1.0f;

    PdhCollectQueryData(gpuQuery);
    DWORD dwType;
    PDH_FMT_COUNTERVALUE Value;
    float total = 0.0f;
    int valid = 0;
    for (size_t i = 0; i < gpuCounters.size(); ++i) {
        if (PdhGetFormattedCounterValue(gpuCounters[i], PDH_FMT_DOUBLE, &dwType, &Value) == ERROR_SUCCESS) {
            total += (float)Value.doubleValue;
            valid++;
        }
    }
    return valid > 0 ? total / valid : -1.0f;
}
#else
std::vector<float> getProcessCPUUsage_Linux() {
    static std::vector<unsigned long long> prevUser, prevNice, prevSystem, prevIdle, prevIowait, prevIrq, prevSoftirq, prevSteal;
    static bool initialized = false;
    std::vector<float> usages;

    std::ifstream stat("/proc/stat");
    if (!stat) return usages;
    std::string line;
    std::vector<unsigned long long> curUser, curNice, curSystem, curIdle, curIowait, curIrq, curSoftirq, curSteal;
    while (std::getline(stat, line)) {
        if (line.compare(0, 3, "cpu") == 0) {
            if (line[3] >= '0' && line[3] <= '9') {
                std::istringstream iss(line);
                std::string cpu;
                unsigned long long user, nice, system, idle, iowait, irq, softirq, steal = 0;
                iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
                if (iss >> steal) {}
                curUser.push_back(user);
                curNice.push_back(nice);
                curSystem.push_back(system);
                curIdle.push_back(idle);
                curIowait.push_back(iowait);
                curIrq.push_back(irq);
                curSoftirq.push_back(softirq);
                curSteal.push_back(steal);
            }
        }
    }

    if (!initialized) {
        prevUser = curUser;
        prevNice = curNice;
        prevSystem = curSystem;
        prevIdle = curIdle;
        prevIowait = curIowait;
        prevIrq = curIrq;
        prevSoftirq = curSoftirq;
        prevSteal = curSteal;
        initialized = true;
        return usages;
    }

    size_t cores = std::min(prevUser.size(), curUser.size());
    for (size_t i = 0; i < cores; ++i) {
        unsigned long long prevIdleTotal = prevIdle[i] + prevIowait[i];
        unsigned long long curIdleTotal = curIdle[i] + curIowait[i];
        unsigned long long prevTotal = prevUser[i] + prevNice[i] + prevSystem[i] + prevIdleTotal + prevIrq[i] + prevSoftirq[i] + prevSteal[i];
        unsigned long long curTotal = curUser[i] + curNice[i] + curSystem[i] + curIdleTotal + curIrq[i] + curSoftirq[i] + curSteal[i];
        unsigned long long totalDiff = curTotal - prevTotal;
        unsigned long long idleDiff = curIdleTotal - prevIdleTotal;
        if (totalDiff > 0)
            usages.push_back((1.0f - (float)idleDiff / totalDiff) * 10.0f);
        else
            usages.push_back(0.0f);
    }

    prevUser = curUser;
    prevNice = curNice;
    prevSystem = curSystem;
    prevIdle = curIdle;
    prevIowait = curIowait;
    prevIrq = curIrq;
    prevSoftirq = curSoftirq;
    prevSteal = curSteal;
    return usages;
}

long getProcessRAMUsage_Linux() {
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

float getGPUUsage_Linux() {
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
// удобная запись в переменные через printf и прочий ужас
#include <cstdio>
// взаимодействия с консолью
#include <iostream>
// таблица номер-значение, поможет для текстур
#include <unordered_map>
// массивы
#include <vector>
// простофильный текст
#include <string>

//              объявления

settings Engine_settings;

// использование пространства имён std 😲
using namespace std;
// переменные для хранения в них размеров окна и экрана
int window_w = 0, window_h = 0, screen_w = 0, screen_h = 0;
static GLFWwindow* g_window = nullptr;
// железо
string cpu_name;
string ram_v;
string gpu_name;

// инициализация камеры
CameraParams camera;

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
varying vec2 vUV;

uniform mat4 u_projection;
uniform mat4 u_modelView;
uniform bool raycast;
uniform bool displayMode;

void main() {
    vec4 mvPos = u_modelView * aVertex;
    vN = normalize(mat3(u_modelView) * aNormal);
    vP = mvPos.xyz;
    vColor = aColor;
    vTexCoord = aTexCoord;
    vWorldPos = aVertex.xyz;
    gl_Position = u_projection * mvPos;
    vClipPos = gl_Position;
    if (raycast || displayMode)
        vUV = (aVertex.xy + 1.0) * 0.5;
    else
        vUV = vec2(0.0);
}
)";

static const char* defaultFragmentShader = R"(
#version 120
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
uniform Light lights[16];
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
uniform sampler2D textures[2];
uniform int triCount;
uniform int triTexWidth;
uniform int triTexHeight;
uniform bool displayMode;
uniform sampler2D accumulationTex;
uniform float frameCount;
uniform int portalCount;
uniform vec3 portalPos[8];
uniform vec3 portalNormal[8];
uniform float portalD[8];
uniform mat4 portalInvWorld[8];
uniform int portalVertCount[8];
uniform vec2 portalVerts[8 * 16];
uniform mat4 portalTeleport[8];
uniform sampler2D bvhTex;
uniform int bvhNodeCount;
uniform int bvhTexWidth;
uniform int bvhTexHeight;
uniform bool warpPlaneEnabled;
uniform vec3 warpPlaneOrigin;
uniform vec3 warpPlaneAxisU;
uniform vec3 warpPlaneAxisV;
uniform sampler2D warpPlaneDisplacementTex;
uniform float maxDist;
uniform float shadowBias;
uniform float camWarpStrength;
uniform float shadowWarpStrength;
uniform float camStepSize;
uniform float shadowStepSize;
uniform int maxBounces;
uniform int maxShadowBounces;
uniform int raySamples;
const float PI = 3.14159265;
const float TWO_PI = 6.2831853;
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float rayTriangleIntersect(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2,
                           out vec3 faceNormal, out float u, out float v) {
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
bool intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax,
                   out float tmin, out float tmax) {
    vec3 invR = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invR;
    vec3 t1 = (bmax - ro) * invR;
    vec3 tmin3 = min(t0, t1);
    vec3 tmax3 = max(t0, t1);
    tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
    tmax = min(min(tmax3.x, tmax3.y), tmax3.z);
    return tmax >= max(0.0, tmin);
}
void fetchBVHNode(int nodeIdx, out vec3 bmin, out int left, out vec3 bmax, out int right,
                  out int firstTri, out int triCountNode, out int escape) {
    float texW = float(bvhTexWidth);
    float texH = float(bvhTexHeight);
    float v = (float(nodeIdx) + 0.5) / texH;
    vec4 d0 = texture2D(bvhTex, vec2((0.5) / texW, v));
    vec4 d1 = texture2D(bvhTex, vec2((1.5) / texW, v));
    vec4 d2 = texture2D(bvhTex, vec2((2.5) / texW, v));
    bmin = d0.xyz;
    left = int(d0.w);
    bmax = d1.xyz;
    right = int(d1.w);
    firstTri = int(d2.x);
    triCountNode = int(d2.y);
    escape = int(d2.z);
}
bool traceSegment(vec3 ro, vec3 rd, float maxT,
                  out float hitT, out vec3 hitPos, out vec3 hitNormal,
                  out vec3 hitCol, out float hitAlpha, out int hitTexID,
                  out bool isPortalHit, out int portalIdx) {
    float closest = maxT;
    hitT = maxT;
    isPortalHit = false;
    portalIdx = -1;
    float invTriW = 1.0 / float(triTexWidth);
    float invTriH = 1.0 / float(triTexHeight);
    float triW = float(triTexWidth);
    int nodeIdx = 0;
    while (nodeIdx >= 0 && nodeIdx < bvhNodeCount) {
        vec3 bmin, bmax;
        int left, right, firstTri, triCountNode, escape;
        fetchBVHNode(nodeIdx, bmin, left, bmax, right, firstTri, triCountNode, escape);
        float tminAABB, tmaxAABB;
        if (!intersectAABB(ro, rd, bmin, bmax, tminAABB, tmaxAABB) || tminAABB > maxT) {
            nodeIdx = escape;
            continue;
        }
        if (left < 0) {
            for (int i = firstTri; i < firstTri + triCountNode; i++) {
                float base = float(i) * 3.0;
                float u0 = mod(base, triW) * invTriW, v0coord = floor(base * invTriW) * invTriH;
                vec4 idxData0 = texture2D(triTexIndices, vec2(u0, v0coord));
                float i0 = idxData0.x;
                if (i0 < 0.0) continue;
                float billFlag = idxData0.y;
                float tid_f = idxData0.z;
                float u1 = mod(base+1.0, triW) * invTriW, v1coord = floor((base+1.0) * invTriW) * invTriH;
                float i1 = texture2D(triTexIndices, vec2(u1, v1coord)).x;
                float u2 = mod(base+2.0, triW) * invTriW, v2coord = floor((base+2.0) * invTriW) * invTriH;
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
                    vec4 objColor = texture2D(triTexColor, vec2(p0x, p0y));
                    vec3 col = objColor.rgb;
                    float alpha = objColor.a;
                    if (texID >= 0 && texID < 2) {
                        vec2 uv0 = texture2D(triTexUV, vec2(p0x, p0y)).rg;
                        vec2 uv1 = texture2D(triTexUV, vec2(p1x, p1y)).rg;
                        vec2 uv2 = texture2D(triTexUV, vec2(p2x, p2y)).rg;
                        vec2 uvCoord = (1.0-u-v)*uv0 + u*uv1 + v*uv2;
                        vec4 texCol;
                        if (texID == 0) texCol = texture2D(textures[0], uvCoord);
                        else texCol = texture2D(textures[1], uvCoord);
                        alpha *= texCol.a;
                        col = texCol.rgb * col;
                    }
                    if (alpha < 0.005) continue;
                    closest = t;
                    hitPos = ro + rd * t;
                    if (billFlag < 0.5) {
                        hitNormal = -rd;
                    } else {
                        vec3 n0 = texture2D(triTexNorm, vec2(p0x, p0y)).rgb;
                        vec3 n1 = texture2D(triTexNorm, vec2(p1x, p1y)).rgb;
                        vec3 n2 = texture2D(triTexNorm, vec2(p2x, p2y)).rgb;
                        vec3 interpN = (1.0-u-v)*n0 + u*n1 + v*n2;
                        vec3 rawNormal;
                        if (dot(interpN, interpN) < 0.0000001) {
                            rawNormal = normalize(faceNormal);
                        } else {
                            rawNormal = normalize(interpN);
                        }
                        hitNormal = (dot(rawNormal, rd) > 0.0) ? -rawNormal : rawNormal;
                    }
                    hitCol = col;
                    hitAlpha = alpha;
                    hitTexID = texID;
                    isPortalHit = false;
                }
            }
            nodeIdx = escape;
        } else {
            nodeIdx = left;
        }
    }
    for (int p = 0; p < portalCount; p++) {
        vec3 Np = portalNormal[p];
        float denom = dot(rd, Np);
        if (abs(denom) < 0.0001) continue;
        float t = -(dot(ro, Np) + portalD[p]) / denom;
        if (t > 0.001 && t < closest) {
            vec3 candidatePos = ro + rd * t;
            vec2 localPt = (portalInvWorld[p] * vec4(candidatePos, 1.0)).xy;
            int vc = portalVertCount[p];
            bool inside = false;
            for (int i = 0, j = vc-1; i < vc; j = i++) {
                vec2 vi = portalVerts[p * 16 + i];
                vec2 vj = portalVerts[p * 16 + j];
                if (((vi.y > localPt.y) != (vj.y > localPt.y)) &&
                    (localPt.x < (vj.x-vi.x)*(localPt.y-vi.y)/(vj.y-vi.y)+vi.x))
                    inside = !inside;
            }
            if (inside) {
                closest = t;
                hitPos = candidatePos;
                isPortalHit = true;
                portalIdx = p;
            }
        }
    }
    hitT = closest;
    return closest < maxT;
}
float shadowRay(int lightIdx, vec3 hitPos, vec3 N, mat4 portalTransform, int samples) {
    if (samples <= 0) samples = 1;
    float totalShadow = 0.0;
    for (int s = 0; s < samples; s++) {
        Light L = lights[lightIdx];
        vec3 lightPos = (portalTransform * vec4(L.position, 1.0)).xyz;
        vec3 lightDir = L.direction;
        bool isSpot = dot(lightDir, lightDir) > 0.000001;
        if (isSpot) {
            lightDir = normalize(mat3(portalTransform) * L.direction);
        }
        vec3 jitter = (samples > 1)
            ? vec3(hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(0.0, 1.0)) - 0.5,
                   hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(1.0, 0.0)) - 0.5,
                   hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(1.0, 1.0)) - 0.5) * 0.01
            : vec3(0.0);
        vec3 ro = hitPos + jitter + N * shadowBias;
        vec3 toLight = lightPos - ro;
        float distToLight = length(toLight);
        if (isSpot) {
            vec3 dirToLight = toLight / distToLight;
            float cosAngle = dot(-dirToLight, lightDir);
            if (cosAngle < L.cutoff) { totalShadow += 0.0; continue; }
        }
        vec3 rd = normalize(toLight);
        float remaining = distToLight;
        mat4 currentTransform = portalTransform;
        int portalBounces = 0;
        float shadowContrib = 1.0;
        for (int i = 0; i < int(ceil(length(lights[lightIdx].diffuse) * 100.0 / shadowStepSize)); i++) {
            if (remaining <= 0.0) break;
            float step = min(shadowStepSize, remaining);
            if (warpPlaneEnabled) {
                vec3 localPos = ro - warpPlaneOrigin;
                float wu = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
                float wv = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
                if (wu >= 0.0 && wu <= 1.0 && wv >= 0.0 && wv <= 1.0) {
                    vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(wu, wv)).rgb;
                    rd = normalize(rd + disp * shadowWarpStrength);
                }
            }
            float tHit;
            vec3 segPos, segNorm, segCol;
            float hitAlpha;
            int segTex;
            bool isPortalHit;
            int portalIdx;
            bool segHit = traceSegment(ro, rd, step, tHit, segPos, segNorm, segCol, hitAlpha, segTex, isPortalHit, portalIdx);
            if (segHit) {
                if (isPortalHit) {
                    if (portalBounces >= maxShadowBounces) { shadowContrib = 0.0; break; }
                    portalBounces++;
                    ro = ro + rd * tHit;
                    remaining -= tHit;
                    currentTransform = portalTeleport[portalIdx] * currentTransform;
                    ro = vec3(portalTeleport[portalIdx] * vec4(ro, 1.0));
                    rd = normalize(mat3(portalTeleport[portalIdx]) * rd);
                    ro += rd * 0.001;
                    lightPos = (currentTransform * vec4(L.position, 1.0)).xyz;
                    if (isSpot) lightDir = normalize(mat3(currentTransform) * L.direction);
                    toLight = lightPos - ro;
                    distToLight = length(toLight);
                    rd = normalize(toLight);
                    remaining = distToLight;
                    if (isSpot && dot(-rd, lightDir) < L.cutoff) { shadowContrib = 0.0; break; }
                    continue;
                } else {
                    if (tHit > shadowBias) {
                        shadowContrib *= (1.0 - hitAlpha);
                        ro = ro + rd * tHit + rd * 0.001;
                        remaining -= tHit;
                        continue;
                    }
                }
            }
            ro = ro + rd * step;
            remaining -= step;
        }
        totalShadow += shadowContrib;
    }
    return totalShadow / float(samples);
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
    if (raycast) {
        int samples = raySamples > 1 ? raySamples : 1;
        vec3 accumulatedColor = vec3(0.0);
        for (int s = 0; s < samples; s++) {
            vec2 jitter = (samples > 1)
                ? vec2(hash(gl_FragCoord.xy + vec2(float(s), 0.0)),
                       hash(gl_FragCoord.xy + vec2(0.0, float(s)))) - 0.5
                : vec2(0.0);
            vec2 sampleUV = vUV + jitter / vec2(float(1024), float(768));
            vec4 clipPos = vec4(sampleUV * 2.0 - 1.0, -1.0, 1.0);
            vec4 worldPos = invViewProj * clipPos;
            worldPos /= worldPos.w;
            vec3 ro = camPos;
            vec3 rd = normalize(worldPos.xyz - ro);
            float totalDist = 0.0;
            mat4 cumulativePortalTransform = mat4(1.0);
            int bounce = 0;
            bool continueRay = true;
            float transparency = 1.0;
            while (continueRay && bounce < maxBounces) {
                float travelledThisBounce = 0.0;
                bool hit = false;
                vec3 hitPos, hitNormal, hitCol;
                float hitAlpha;
                int hitTexID;
                bool isPortalHit;
                int portalHitIdx;
                for (int step = 0; step < int(ceil(maxDist / camStepSize)); step++) {
                    if (travelledThisBounce >= maxDist) break;
                    float stepSize = min(camStepSize, maxDist - travelledThisBounce);
                    if (warpPlaneEnabled) {
                        vec3 localPos = ro - warpPlaneOrigin;
                        float u = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
                        float v = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
                        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
                            vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(u, v)).rgb;
                            rd = normalize(rd + disp * camWarpStrength);
                        }
                    }
                    float tHit;
                    bool segHit = traceSegment(ro, rd, stepSize, tHit, hitPos, hitNormal, hitCol, hitAlpha, hitTexID, isPortalHit, portalHitIdx);
                    if (segHit) {
                        totalDist += travelledThisBounce + tHit;
                        hit = true;
                        break;
                    }
                    ro = ro + rd * stepSize;
                    travelledThisBounce += stepSize;
                }
                if (!hit) {
                    if (hasPanorama) {
                        float panU = 0.5 + atan(rd.z, rd.x) / TWO_PI;
                        float panV = 0.5 + asin(rd.y) / PI;
                        vec3 panColor = texture2D(panoramaTex, vec2(panU, panV)).rgb;
                        accumulatedColor = mix(accumulatedColor, panColor, transparency);
                    }
                    continueRay = false;
                    break;
                }
                if (isPortalHit) {
                    cumulativePortalTransform = portalTeleport[portalHitIdx] * cumulativePortalTransform;
                    ro = vec3(portalTeleport[portalHitIdx] * vec4(hitPos, 1.0));
                    rd = normalize(mat3(portalTeleport[portalHitIdx]) * rd);
                    totalDist = 0.0;
                    bounce++;
                    continue;
                } else {
                    float fogCoord = totalDist;
                    vec3 N = normalize(hitNormal);
                    vec3 litCol = ambientLight;
                    for (int j = 0; j < numLights; j++) {
                        if (!lights[j].enabled) continue;
                        if (lights[j].attenuation.x <= 0.0) continue;
                        vec3 effectiveLightPos = (cumulativePortalTransform * vec4(lights[j].position, 1.0)).xyz;
                        if (warpPlaneEnabled) {
                            vec3 localPos = hitPos - warpPlaneOrigin;
                            float wu = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
                            float wv = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
                            if (wu >= 0.0 && wu <= 1.0 && wv >= 0.0 && wv <= 1.0) {
                                vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(wu, wv)).rgb;
                                effectiveLightPos += disp * shadowWarpStrength * 2.0;
                            }
                        }
                        vec3 Lpos = effectiveLightPos;
                        vec3 Ldir = lights[j].direction;
                        bool isSpot = dot(Ldir, Ldir) > 0.000001;
                        if (isSpot) Ldir = normalize(mat3(cumulativePortalTransform) * Ldir);
                        vec3 delta = Lpos - hitPos;
                        float d2 = dot(delta, delta);
                        if (d2 < 0.000001) continue;
                        float invDist = inversesqrt(d2);
                        float d = 1.0 / invDist;
                        float atten = 1.0 / (lights[j].attenuation.x + lights[j].attenuation.y*d + lights[j].attenuation.z*d2);
                        if (isSpot) {
                            vec3 dirToHit = -delta * invDist;
                            if (dot(dirToHit, Ldir) < lights[j].cutoff) continue;
                        }
                        vec3 L = delta * invDist;
                        float diff = max(dot(N, L), 0.0);
                        if (diff > 0.0) {
                            float shadow = shadowRay(j, hitPos, N, cumulativePortalTransform, samples);
                            litCol += lights[j].diffuse * diff * atten * shadow;
                        }
                    }
                    litCol *= hitCol;
                    float fogFactor = clamp((fogEnd - fogCoord) * invFogRange, 0.0, 1.0);
                    litCol = mix(fogColor, litCol, fogFactor);

                    accumulatedColor = mix(accumulatedColor, litCol, hitAlpha * transparency);
                    transparency *= (1.0 - hitAlpha);

                    if (transparency < 0.001) {
                        continueRay = false;
                        break;
                    }

                    ro = hitPos + rd * 0.001;
                    totalDist = fogCoord;
                    bounce++;
                    continue;
                }
                bounce++;
            }
        }
        gl_FragColor = vec4(accumulatedColor / float(samples), 1.0);
        return;
    }
    vec4 texColor = texture2D(tex, vTexCoord);
    if (texColor.a < 0.01) discard;
    vec3 N = normalize(vN);
    vec3 totalLight = ambientLight;
    int nL = numLights;
    if (nL > 16) nL = 16;
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

// кэш uniform-локаций для основного шейдера
static GLint loc_tex = -1;
static GLint loc_portalMode = -1, loc_portalDepthOnly = -1, loc_portalTex = -1;

static glm::mat4 g_projectionMatrix(1.0f);
static glm::mat4 g_modelViewMatrix(1.0f);
static GLint loc_u_projection_default = -1;
static GLint loc_u_modelView_default = -1;

static void updateMatrixUniforms() {
    if (loc_u_projection_default != -1) glUniformMatrix4fv(loc_u_projection_default, 1, GL_FALSE, &g_projectionMatrix[0][0]);
    if (loc_u_modelView_default != -1) glUniformMatrix4fv(loc_u_modelView_default, 1, GL_FALSE, &g_modelViewMatrix[0][0]);
    if (loc_u_projection_2d != -1) glUniformMatrix4fv(loc_u_projection_2d, 1, GL_FALSE, &g_projectionMatrix[0][0]);
    if (loc_u_modelView_2d != -1) glUniformMatrix4fv(loc_u_modelView_2d, 1, GL_FALSE, &g_modelViewMatrix[0][0]);
}

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

        loc_u_projection_default = glGetUniformLocation(defaultLightingShader, "u_projection");
        loc_u_modelView_default = glGetUniformLocation(defaultLightingShader, "u_modelView");

        int maxLights = Engine_settings.MAX_LIGHTS;
        loc_lightEnabled.resize(maxLights, -1);
        loc_lightPosition.resize(maxLights, -1);
        loc_lightDirection.resize(maxLights, -1);
        loc_lightDiffuse.resize(maxLights, -1);
        loc_lightCutoff.resize(maxLights, -1);
        loc_lightAttenuation.resize(maxLights, -1);

        char buf[64];
        for (int i = 0; i < maxLights; ++i) {
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

//              очень ужасный рэй кастинг

std::vector<DrawCommand> drawQueue;
std::mutex drawQueueMutex;

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
        GLenum internalFormat = (components == 3) ? GL_RGB32F : GL_RGBA32F;
        GLenum format = (components == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    };

    createFloatTex(triTexPos, w, h, 3);
    createFloatTex(triTexNorm, w, h, 3);
    createFloatTex(triTexColor, w, h, 4);   
    createFloatTex(triTexIndices, w, h, 3);
}

struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    AABB() : min(FLT_MAX), max(-FLT_MAX) {}
    AABB(const glm::vec3& p) : min(p), max(p) {}
    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const AABB& box) {
        min = glm::min(min, box.min);
        max = glm::max(max, box.max);
    }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return max - min; }
    float radius() const { return glm::length(extents()) * 0.5f; }
};

struct Triangle {
    float v0[3], v1[3], v2[3];
    float centroid[3];
};

struct BVHNode {
    float bmin[3], bmax[3];
    int left, right;
    int firstTri, triCount;
    int escape;
};

static std::vector<BVHNode> bvhNodes;

int buildBVHRecursive(std::vector<BVHNode>& nodes,
                       const std::vector<Triangle>& triangles,
                       std::vector<int>& triIndices, int start, int end, int depth) {
    BVHNode node;
    node.bmin[0] = node.bmin[1] = node.bmin[2] = std::numeric_limits<float>::max();
    node.bmax[0] = node.bmax[1] = node.bmax[2] = -std::numeric_limits<float>::max();
    for (int i = start; i < end; ++i) {
        const Triangle& t = triangles[triIndices[i]];
        for (int k = 0; k < 3; ++k) {
            node.bmin[k] = std::min(node.bmin[k], std::min({t.v0[k], t.v1[k], t.v2[k]}));
            node.bmax[k] = std::max(node.bmax[k], std::max({t.v0[k], t.v1[k], t.v2[k]}));
        }
    }
    node.left = node.right = -1;
    node.firstTri = start;
    node.triCount = end - start;
    int idx = (int)nodes.size();
    nodes.push_back(node);

    if (node.triCount <= 4 || depth > 20) return idx;

    int axis = 0;
    float ext = node.bmax[0] - node.bmin[0];
    if (node.bmax[1] - node.bmin[1] > ext) { axis = 1; ext = node.bmax[1] - node.bmin[1]; }
    if (node.bmax[2] - node.bmin[2] > ext) { axis = 2; }

    int mid = start + (end - start) / 2;
    std::nth_element(triIndices.begin() + start, triIndices.begin() + mid, triIndices.begin() + end,
        [&triangles, axis](int a, int b) {
            return triangles[a].centroid[axis] < triangles[b].centroid[axis];
        });

    int leftIdx = buildBVHRecursive(nodes, triangles, triIndices, start, mid, depth + 1);
    int rightIdx = buildBVHRecursive(nodes, triangles, triIndices, mid, end, depth + 1);

    nodes[idx].left = leftIdx;
    nodes[idx].right = rightIdx;
    nodes[idx].firstTri = 0;
    nodes[idx].triCount = 0;
    return idx;
}

static GLuint bvhNodeTex = 0, bvhNode2Tex = 0, bvhTriRefTex = 0;
static int bvhTexWidth = 0, bvhTexHeight = 0;
static GLuint bvhBoxTex = 0, bvhTriTex = 0;
static int bvhNodeCount = 0;
static int bvhBoxWidth = 0;  
static int bvhTriWidth = 0;

void computeEscapeIndicesFixed(std::vector<BVHNode>& nodes, int nodeIdx, int parentEscape = -1) {
    BVHNode& n = nodes[nodeIdx];
    if (n.left == -1) {
        n.escape = parentEscape;
        return;
    }
    computeEscapeIndicesFixed(nodes, n.left, n.right);
    computeEscapeIndicesFixed(nodes, n.right, parentEscape);
    n.escape = parentEscape;
}

std::vector<float> packBVH(const std::vector<BVHNode>& nodes) {
    std::vector<float> data(nodes.size() * 4 * 4);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const BVHNode& n = nodes[i];
        float* base = &data[i * 16];
        base[0] = n.bmin[0]; base[1] = n.bmin[1]; base[2] = n.bmin[2]; base[3] = (float)n.left;
        base[4] = n.bmax[0]; base[5] = n.bmax[1]; base[6] = n.bmax[2]; base[7] = (float)n.right;
        base[8] = (float)n.firstTri; base[9] = (float)n.triCount; base[10] = (float)n.escape; base[11] = 0.0f;
        base[12] = base[13] = base[14] = base[15] = 0.0f;
    }
    return data;
}

GLuint createBVHTexture(const std::vector<float>& packedData, int nodeCount) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, nodeCount, 0, GL_RGBA, GL_FLOAT, packedData.data());
    return tex;
}

void flushDrawQueue() {
    applyAllLights();
    static std::vector<float> cachedPosData;
    static std::vector<float> cachedNormData;
    static std::vector<float> cachedColData;
    static std::vector<float> cachedUvData;
    static std::vector<float> cachedIdxData;
    static GLuint cachedBvhTex = 0;
    static int cachedTriCount = 0;
    static int cachedTriTexWidth = 0, cachedTriTexHeight = 0;
    static bool bvhValid = false;
    static std::vector<GLuint> cachedTextureIDs;
    static std::vector<int> cachedTextureSlots;
    static GLuint triTexUV = 0;

    if (is_scene_changed && current_scene) {
        std::vector<DrawCommand> tempQueue;
        drawQueue.swap(tempQueue);
        current_scene();
        std::vector<DrawCommand> staticCommands;
        staticCommands.swap(drawQueue);
        drawQueue.swap(tempQueue);
        int totalTriangles = 0;
        for (auto& cmd : staticCommands) {
            if (cmd.type == CMD_3DOBJECT) totalTriangles += cmd.obj_indices.size() / 3;
            else if (cmd.type == CMD_PSEUDO3D) totalTriangles += 2;
        }
        if (totalTriangles > 0) {
            ensureTriTextures(totalTriangles * 3);
            int totalPixels = triTexWidth * triTexHeight;
            std::vector<float> posData(totalPixels * 3, 0.0f);
            std::vector<float> normData(totalPixels * 3, 0.0f);
            std::vector<float> colData(totalPixels * 4, 0.0f);
            std::vector<float> uvData(totalPixels * 2, 0.0f);
            std::vector<float> idxData(totalPixels * 4, -1.0f);
            int texIdx = 0;
            std::unordered_map<GLuint, int> textureSlotMap;
            std::vector<GLuint> activeTextures;
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
            for (auto& cmd : staticCommands) {
                if (cmd.type == CMD_3DOBJECT) {
                    if (cmd.obj_indices.size() < 3) continue;
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
                } else if (cmd.type == CMD_PSEUDO3D && cmd.entity) {
                    continue;
                }
            }
            cachedTriCount = totalTriangles;
            std::vector<Triangle> triangles(cachedTriCount);
            for (int i = 0; i < cachedTriCount; i++) {
                float* base = &posData[i * 9];
                Triangle t;
                memcpy(t.v0, base + 0, 3 * sizeof(float));
                memcpy(t.v1, base + 3, 3 * sizeof(float));
                memcpy(t.v2, base + 6, 3 * sizeof(float));
                for (int k = 0; k < 3; k++)
                    t.centroid[k] = (t.v0[k] + t.v1[k] + t.v2[k]) / 3.0f;
                triangles[i] = t;
            }
            std::vector<int> triIndices(cachedTriCount);
            for (int i = 0; i < cachedTriCount; i++) triIndices[i] = i;
            bvhNodes.clear();
            buildBVHRecursive(bvhNodes, triangles, triIndices, 0, cachedTriCount, 0);
            computeEscapeIndicesFixed(bvhNodes, 0, -1);
            std::vector<float> posDataNew(totalPixels * 3, 0.0f);
            std::vector<float> normDataNew(totalPixels * 3, 0.0f);
            std::vector<float> colDataNew(totalPixels * 4, 0.0f);
            std::vector<float> uvDataNew(totalPixels * 2, 0.0f);
            std::vector<float> idxDataNew(totalPixels * 4, -1.0f);
            for (int newIdx = 0; newIdx < cachedTriCount; ++newIdx) {
                int oldIdx = triIndices[newIdx];
                memcpy(&posDataNew[newIdx * 9], &posData[oldIdx * 9], 9 * sizeof(float));
                memcpy(&normDataNew[newIdx * 9], &normData[oldIdx * 9], 9 * sizeof(float));
                memcpy(&colDataNew[newIdx * 12], &colData[oldIdx * 12], 12 * sizeof(float));
                memcpy(&uvDataNew[newIdx * 6], &uvData[oldIdx * 6], 6 * sizeof(float));
                memcpy(&idxDataNew[newIdx * 12], &idxData[oldIdx * 12], 12 * sizeof(float));
            }
            for (int i = 0; i < cachedTriCount; ++i)
                for (int j = 0; j < 3; ++j)
                    idxDataNew[(i * 3 + j) * 4 + 0] = (float)(i * 3 + j);
            cachedPosData = std::move(posDataNew);
            cachedNormData = std::move(normDataNew);
            cachedColData = std::move(colDataNew);
            cachedUvData = std::move(uvDataNew);
            cachedIdxData = std::move(idxDataNew);
            cachedTriTexWidth = triTexWidth;
            cachedTriTexHeight = triTexHeight;
            if (cachedBvhTex) glDeleteTextures(1, &cachedBvhTex);
            std::vector<float> packed = packBVH(bvhNodes);
            cachedBvhTex = createBVHTexture(packed, bvhNodes.size());
            cachedTextureIDs = activeTextures;
            cachedTextureSlots.clear();
            for (size_t i = 0; i < activeTextures.size(); ++i) cachedTextureSlots.push_back(i);
            bvhValid = true;
        } else {
            bvhValid = false;
        }
        is_scene_changed = 0;
    }
    if (!current_scene) {
        bvhValid = false;
    }
    std::vector<DrawCommand> commands2D;
    std::vector<DrawCommand> commands3D;
    std::vector<DrawCommand> panoramaCommands;
    std::vector<Portal*> portalCommands;
    bool hasPortalCommand = false;
    for (auto& cmd : drawQueue) {
        switch (cmd.type) {
            case CMD_PORTAL: hasPortalCommand = true; portalCommands.push_back(cmd.portal); break;
            case CMD_SQUARE: case CMD_TEXT: case CMD_LINE_2D: commands2D.push_back(cmd); break;
            case CMD_PANORAMA: panoramaCommands.push_back(cmd); break;
            default: commands3D.push_back(cmd); break;
        }
    }
    drawQueue.clear();

    float mult = Engine_settings.RAY_MULTIPLY;
    int renderW, renderH;
    int raySamples;
    if (mult >= 1.0f) {
        renderW = window_w;
        renderH = window_h;
        raySamples = (int)mult;
    } else {
        float factor = sqrtf(mult);
        renderW = std::max(1, (int)(window_w * factor));
        renderH = std::max(1, (int)(window_h * factor));
        raySamples = 1;
    }

    static GLuint fbo = 0, fboTex = 0;
    static int lastRenderW = 0, lastRenderH = 0;
    if (fbo == 0 || renderW != lastRenderW || renderH != lastRenderH) {
        if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &fboTex); fbo = 0; fboTex = 0; }
        if (renderW != window_w || renderH != window_h) {
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &fboTex);
            glBindTexture(GL_TEXTURE_2D, fboTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderW, renderH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &fboTex); fbo = 0; fboTex = 0;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            lastRenderW = renderW;
            lastRenderH = renderH;
        } else {
            fbo = 0; fboTex = 0;
            lastRenderW = renderW;
            lastRenderH = renderH;
        }
    }

    static GLint loc_raycast = -1, loc_invViewProj = -1, loc_camPos = -1;
    static GLint loc_ambient = -1, loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;
    static GLint loc_numLights = -1, loc_panoramaTex = -1, loc_hasPanorama = -1;
    static GLint loc_triCount = -1, loc_triTexWidth = -1, loc_triTexHeight = -1;
    static GLint loc_triTexPos = -1, loc_triTexNorm = -1, loc_triTexColor = -1, loc_triTexIndices = -1, loc_triTexUV = -1;
    static GLint loc_portalCount = -1, loc_portalPos = -1, loc_portalNormal = -1, loc_portalD = -1;
    static GLint loc_portalInvWorld = -1, loc_portalVertCount = -1, loc_portalVerts = -1, loc_portalTeleport = -1;
    static std::vector<GLint> locLightEnabled;
    static std::vector<GLint> locLightPosition;
    static std::vector<GLint> locLightDirection;
    static std::vector<GLint> locLightDiffuse;
    static std::vector<GLint> locLightCutoff;
    static std::vector<GLint> locLightAttenuation;
    static std::vector<GLint> locTexSlot;
    static GLint loc_bvhTex = -2, loc_bvhNodeCount = -2, loc_bvhTexWidth = -2, loc_bvhTexHeight = -2;
    static GLint loc_warpEnabled = -1, loc_warpOrigin = -1, loc_warpAxisU = -1, loc_warpAxisV = -1, loc_warpDisplacementTex = -1;
    static GLint loc_maxDist = -1, loc_shadowBias = -1, loc_camWarpStrength = -1;
    static GLint loc_shadowWarpStrength = -1, loc_camStepSize = -1, loc_shadowStepSize = -1;
    static GLint loc_maxBounces = -1, loc_maxShadowBounces = -1;
    static GLint loc_raySamples = -1;
    static bool uniformsCached = false;
    if (!uniformsCached && currentShaderProg) {
        loc_raycast = glGetUniformLocation(currentShaderProg, "raycast");
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
        int maxLights = Engine_settings.MAX_LIGHTS;
        locLightEnabled.resize(maxLights, -1);
        locLightPosition.resize(maxLights, -1);
        locLightDirection.resize(maxLights, -1);
        locLightDiffuse.resize(maxLights, -1);
        locLightCutoff.resize(maxLights, -1);
        locLightAttenuation.resize(maxLights, -1);
        for (int i = 0; i < maxLights; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
            locLightEnabled[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].position", i);
            locLightPosition[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].direction", i);
            locLightDirection[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
            locLightDiffuse[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
            locLightCutoff[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
            locLightAttenuation[i] = glGetUniformLocation(currentShaderProg, buf);
        }
        locTexSlot.resize(8, -1);
        for (int i = 0; i < 8; i++) {
            char name[32]; snprintf(name, sizeof(name), "textures[%d]", i); locTexSlot[i] = glGetUniformLocation(currentShaderProg, name);
        }
        loc_bvhTex = glGetUniformLocation(currentShaderProg, "bvhTex");
        loc_bvhNodeCount = glGetUniformLocation(currentShaderProg, "bvhNodeCount");
        loc_bvhTexWidth = glGetUniformLocation(currentShaderProg, "bvhTexWidth");
        loc_bvhTexHeight = glGetUniformLocation(currentShaderProg, "bvhTexHeight");
        loc_warpEnabled = glGetUniformLocation(currentShaderProg, "warpPlaneEnabled");
        loc_warpOrigin = glGetUniformLocation(currentShaderProg, "warpPlaneOrigin");
        loc_warpAxisU = glGetUniformLocation(currentShaderProg, "warpPlaneAxisU");
        loc_warpAxisV = glGetUniformLocation(currentShaderProg, "warpPlaneAxisV");
        loc_warpDisplacementTex = glGetUniformLocation(currentShaderProg, "warpPlaneDisplacementTex");
        loc_maxDist = glGetUniformLocation(currentShaderProg, "maxDist");
        loc_shadowBias = glGetUniformLocation(currentShaderProg, "shadowBias");
        loc_camWarpStrength = glGetUniformLocation(currentShaderProg, "camWarpStrength");
        loc_shadowWarpStrength = glGetUniformLocation(currentShaderProg, "shadowWarpStrength");
        loc_camStepSize = glGetUniformLocation(currentShaderProg, "camStepSize");
        loc_shadowStepSize = glGetUniformLocation(currentShaderProg, "shadowStepSize");
        loc_maxBounces = glGetUniformLocation(currentShaderProg, "maxBounces");
        loc_maxShadowBounces = glGetUniformLocation(currentShaderProg, "maxShadowBounces");
        loc_raySamples = glGetUniformLocation(currentShaderProg, "raySamples");
        uniformsCached = true;
    }

    bool useFBO = (fbo != 0 && fboTex != 0);
    if (useFBO) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, renderW, renderH);
    } else {
        glViewport(0, 0, window_w, window_h);
    }

    if (bvhValid || !commands3D.empty() || !panoramaCommands.empty()) {
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
        int totalDynamicTriangles = 0;
        for (auto& cmd : commands3D) {
            if (cmd.type == CMD_3DOBJECT) totalDynamicTriangles += cmd.obj_indices.size() / 3;
            else if (cmd.type == CMD_PSEUDO3D) totalDynamicTriangles += 2;
        }
        if (totalDynamicTriangles > 0) {
            if (bvhValid) {
                int combinedTriCount = cachedTriCount + totalDynamicTriangles;
                ensureTriTextures(combinedTriCount * 3);
                int totalPixels = triTexWidth * triTexHeight;
                std::vector<float> posData(totalPixels * 3, 0.0f);
                std::vector<float> normData(totalPixels * 3, 0.0f);
                std::vector<float> colData(totalPixels * 4, 0.0f);
                std::vector<float> uvData(totalPixels * 2, 0.0f);
                std::vector<float> idxData(totalPixels * 4, -1.0f);
                memcpy(posData.data(), cachedPosData.data(), cachedTriCount * 9 * sizeof(float));
                memcpy(normData.data(), cachedNormData.data(), cachedTriCount * 9 * sizeof(float));
                memcpy(colData.data(), cachedColData.data(), cachedTriCount * 12 * sizeof(float));
                memcpy(uvData.data(), cachedUvData.data(), cachedTriCount * 6 * sizeof(float));
                memcpy(idxData.data(), cachedIdxData.data(), cachedTriCount * 12 * sizeof(float));
                activeTextures = cachedTextureIDs;
                for (size_t i = 0; i < cachedTextureIDs.size(); ++i)
                    textureSlotMap[cachedTextureIDs[i]] = cachedTextureSlots[i];
                int texIdx = cachedTriCount;
                for (auto& cmd : commands3D) {
                    if (cmd.type == CMD_3DOBJECT) {
                        if (cmd.obj_indices.size() < 3) continue;
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
                            if (texIdx >= combinedTriCount) break;
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
                    } else if (cmd.type == CMD_PSEUDO3D && cmd.entity) {
                        const pseudo_3d_entity* ent = cmd.entity;
                        float cx = ent->getX(), cy = ent->getY(), cz = ent->getZ();
                        float camX = cmd.cam_x, camY = cmd.cam_y, camZ = cmd.cam_z;
                        float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                        if (dist < 0.0001f) continue;
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
                        float roll_raw = (plen < 0.001f) ? 0.0f :
                            atan2f(-(pu_x*rx + pu_y*ry + pu_z*rz), pu_x*ux + pu_y*uy + pu_z*uz) * 180.0f / M_PI;
                        int texIdxLocal = ent->getTextureIndex(fx, fy, fz);
                        float net_angle = roll_raw + 180.0f + (texIdxLocal == 0 ? -180.0f : 0.0f);
                        float roll_rad = net_angle * M_PI / 180.0f;
                        const auto& verts = ent->getVertices();
                        if (verts.size() < 8) continue;
                        auto rotateLocal = [roll_rad](float lx, float ly) {
                            float c = cosf(roll_rad), s = sinf(roll_rad);
                            return std::make_pair(lx * c - ly * s, lx * s + ly * c);
                        };
                        auto [rv0x, rv0y] = rotateLocal(verts[0], verts[1]);
                        auto [rv1x, rv1y] = rotateLocal(verts[2], verts[3]);
                        auto [rv2x, rv2y] = rotateLocal(verts[4], verts[5]);
                        auto [rv3x, rv3y] = rotateLocal(verts[6], verts[7]);
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
                            if (texIdx >= combinedTriCount) break;
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
                                colData[colBase+0] = 1.0f; colData[colBase+1] = 1.0f; colData[colBase+2] = 1.0f; colData[colBase+3] = 1.0f;
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
                    }
                }
                triCount = combinedTriCount;
                std::vector<Triangle> triangles(triCount);
                for (int i = 0; i < triCount; i++) {
                    float* base = &posData[i * 9];
                    Triangle t;
                    memcpy(t.v0, base + 0, 3 * sizeof(float));
                    memcpy(t.v1, base + 3, 3 * sizeof(float));
                    memcpy(t.v2, base + 6, 3 * sizeof(float));
                    for (int k = 0; k < 3; k++)
                        t.centroid[k] = (t.v0[k] + t.v1[k] + t.v2[k]) / 3.0f;
                    triangles[i] = t;
                }
                std::vector<int> triIndices(triCount);
                for (int i = 0; i < triCount; i++) triIndices[i] = i;
                bvhNodes.clear();
                buildBVHRecursive(bvhNodes, triangles, triIndices, 0, triCount, 0);
                computeEscapeIndicesFixed(bvhNodes, 0, -1);
                std::vector<float> posDataNew(totalPixels * 3, 0.0f);
                std::vector<float> normDataNew(totalPixels * 3, 0.0f);
                std::vector<float> colDataNew(totalPixels * 4, 0.0f);
                std::vector<float> uvDataNew(totalPixels * 2, 0.0f);
                std::vector<float> idxDataNew(totalPixels * 4, -1.0f);
                for (int newIdx = 0; newIdx < triCount; ++newIdx) {
                    int oldIdx = triIndices[newIdx];
                    memcpy(&posDataNew[newIdx * 9], &posData[oldIdx * 9], 9 * sizeof(float));
                    memcpy(&normDataNew[newIdx * 9], &normData[oldIdx * 9], 9 * sizeof(float));
                    memcpy(&colDataNew[newIdx * 12], &colData[oldIdx * 12], 12 * sizeof(float));
                    memcpy(&uvDataNew[newIdx * 6], &uvData[oldIdx * 6], 6 * sizeof(float));
                    memcpy(&idxDataNew[newIdx * 12], &idxData[oldIdx * 12], 12 * sizeof(float));
                }
                for (int i = 0; i < triCount; ++i)
                    for (int j = 0; j < 3; ++j)
                        idxDataNew[(i * 3 + j) * 4 + 0] = (float)(i * 3 + j);
                posData = std::move(posDataNew);
                normData = std::move(normDataNew);
                colData = std::move(colDataNew);
                uvData = std::move(uvDataNew);
                idxData = std::move(idxDataNew);

                if (cachedBvhTex) glDeleteTextures(1, &cachedBvhTex);
                std::vector<float> packed = packBVH(bvhNodes);
                cachedBvhTex = createBVHTexture(packed, bvhNodes.size());

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
            } else {
                ensureTriTextures(totalDynamicTriangles * 3);
                int totalPixels = triTexWidth * triTexHeight;
                std::vector<float> posData(totalPixels * 3, 0.0f);
                std::vector<float> normData(totalPixels * 3, 0.0f);
                std::vector<float> colData(totalPixels * 4, 0.0f);
                std::vector<float> uvData(totalPixels * 2, 0.0f);
                std::vector<float> idxData(totalPixels * 4, -1.0f);
                int texIdx = 0;
                for (auto& cmd : commands3D) {
                    if (cmd.type == CMD_3DOBJECT) {
                        if (cmd.obj_indices.size() < 3) continue;
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
                            if (texIdx >= totalDynamicTriangles) break;
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
                    } else if (cmd.type == CMD_PSEUDO3D && cmd.entity) {
                        const pseudo_3d_entity* ent = cmd.entity;
                        float cx = ent->getX(), cy = ent->getY(), cz = ent->getZ();
                        float camX = cmd.cam_x, camY = cmd.cam_y, camZ = cmd.cam_z;
                        float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                        if (dist < 0.0001f) continue;
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
                        float roll_raw = (plen < 0.001f) ? 0.0f :
                            atan2f(-(pu_x*rx + pu_y*ry + pu_z*rz), pu_x*ux + pu_y*uy + pu_z*uz) * 180.0f / M_PI;
                        int texIdxLocal = ent->getTextureIndex(fx, fy, fz);
                        float net_angle = roll_raw + 180.0f + (texIdxLocal == 0 ? -180.0f : 0.0f);
                        float roll_rad = net_angle * M_PI / 180.0f;
                        const auto& verts = ent->getVertices();
                        if (verts.size() < 8) continue;
                        auto rotateLocal = [roll_rad](float lx, float ly) {
                            float c = cosf(roll_rad), s = sinf(roll_rad);
                            return std::make_pair(lx * c - ly * s, lx * s + ly * c);
                        };
                        auto [rv0x, rv0y] = rotateLocal(verts[0], verts[1]);
                        auto [rv1x, rv1y] = rotateLocal(verts[2], verts[3]);
                        auto [rv2x, rv2y] = rotateLocal(verts[4], verts[5]);
                        auto [rv3x, rv3y] = rotateLocal(verts[6], verts[7]);
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
                            if (texIdx >= totalDynamicTriangles) break;
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
                                colData[colBase+0] = 1.0f; colData[colBase+1] = 1.0f; colData[colBase+2] = 1.0f; colData[colBase+3] = 1.0f;
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
                    }
                }
                triCount = totalDynamicTriangles;
                std::vector<Triangle> triangles(triCount);
                for (int i = 0; i < triCount; i++) {
                    float* base = &posData[i * 9];
                    Triangle t;
                    memcpy(t.v0, base + 0, 3 * sizeof(float));
                    memcpy(t.v1, base + 3, 3 * sizeof(float));
                    memcpy(t.v2, base + 6, 3 * sizeof(float));
                    for (int k = 0; k < 3; k++)
                        t.centroid[k] = (t.v0[k] + t.v1[k] + t.v2[k]) / 3.0f;
                    triangles[i] = t;
                }
                std::vector<int> triIndices(triCount);
                for (int i = 0; i < triCount; i++) triIndices[i] = i;
                bvhNodes.clear();
                buildBVHRecursive(bvhNodes, triangles, triIndices, 0, triCount, 0);
                computeEscapeIndicesFixed(bvhNodes, 0, -1);
                std::vector<float> posDataNew(totalPixels * 3, 0.0f);
                std::vector<float> normDataNew(totalPixels * 3, 0.0f);
                std::vector<float> colDataNew(totalPixels * 4, 0.0f);
                std::vector<float> uvDataNew(totalPixels * 2, 0.0f);
                std::vector<float> idxDataNew(totalPixels * 4, -1.0f);
                for (int newIdx = 0; newIdx < triCount; ++newIdx) {
                    int oldIdx = triIndices[newIdx];
                    memcpy(&posDataNew[newIdx * 9], &posData[oldIdx * 9], 9 * sizeof(float));
                    memcpy(&normDataNew[newIdx * 9], &normData[oldIdx * 9], 9 * sizeof(float));
                    memcpy(&colDataNew[newIdx * 12], &colData[oldIdx * 12], 12 * sizeof(float));
                    memcpy(&uvDataNew[newIdx * 6], &uvData[oldIdx * 6], 6 * sizeof(float));
                    memcpy(&idxDataNew[newIdx * 12], &idxData[oldIdx * 12], 12 * sizeof(float));
                }
                for (int i = 0; i < triCount; ++i)
                    for (int j = 0; j < 3; ++j)
                        idxDataNew[(i * 3 + j) * 4 + 0] = (float)(i * 3 + j);
                posData = std::move(posDataNew);
                normData = std::move(normDataNew);
                colData = std::move(colDataNew);
                uvData = std::move(uvDataNew);
                idxData = std::move(idxDataNew);
                if (cachedBvhTex) glDeleteTextures(1, &cachedBvhTex);
                std::vector<float> packed = packBVH(bvhNodes);
                cachedBvhTex = createBVHTexture(packed, bvhNodes.size());
                bvhValid = true;
                cachedTriCount = triCount;
                cachedPosData = posData;
                cachedNormData = normData;
                cachedColData = colData;
                cachedUvData = uvData;
                cachedIdxData = idxData;
                cachedTriTexWidth = triTexWidth;
                cachedTriTexHeight = triTexHeight;
                cachedTextureIDs = activeTextures;
                cachedTextureSlots.clear();
                for (size_t i = 0; i < activeTextures.size(); ++i) cachedTextureSlots.push_back(i);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, triTexPos);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, triTexWidth, triTexHeight, 0, GL_RGB, GL_FLOAT, cachedPosData.data());
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, triTexNorm);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, triTexWidth, triTexHeight, 0, GL_RGB, GL_FLOAT, cachedNormData.data());
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, triTexColor);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, triTexWidth, triTexHeight, 0, GL_RGBA, GL_FLOAT, cachedColData.data());
                if (triTexUV == 0) {
                    glGenTextures(1, &triTexUV);
                    glBindTexture(GL_TEXTURE_2D, triTexUV);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, triTexWidth, triTexHeight, 0, GL_RG, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, triTexUV);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, triTexWidth, triTexHeight, 0, GL_RG, GL_FLOAT, cachedUvData.data());
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, triTexIndices);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, triTexWidth, triTexHeight, 0, GL_RGBA, GL_FLOAT, cachedIdxData.data());
            }
        } else if (bvhValid) {
            triCount = cachedTriCount;
            activeTextures = cachedTextureIDs;
            for (size_t i = 0; i < activeTextures.size(); ++i)
                textureSlotMap[activeTextures[i]] = cachedTextureSlots[i];
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, triTexPos);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, cachedTriTexWidth, cachedTriTexHeight, 0, GL_RGB, GL_FLOAT, cachedPosData.data());
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, triTexNorm);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, cachedTriTexWidth, cachedTriTexHeight, 0, GL_RGB, GL_FLOAT, cachedNormData.data());
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, triTexColor);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cachedTriTexWidth, cachedTriTexHeight, 0, GL_RGBA, GL_FLOAT, cachedColData.data());
            if (triTexUV == 0) {
                glGenTextures(1, &triTexUV);
                glBindTexture(GL_TEXTURE_2D, triTexUV);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, cachedTriTexWidth, cachedTriTexHeight, 0, GL_RG, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, triTexUV);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cachedTriTexWidth, cachedTriTexHeight, GL_RG, GL_FLOAT, cachedUvData.data());
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, triTexIndices);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cachedTriTexWidth, cachedTriTexHeight, 0, GL_RGBA, GL_FLOAT, cachedIdxData.data());
        } else {
            triCount = 0;
        }
        glm::mat4 prevProj = g_projectionMatrix;
        glm::mat4 prevModel = g_modelViewMatrix;
        g_projectionMatrix = glm::mat4(1.0f);
        g_modelViewMatrix = glm::mat4(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        useShader(currentShaderProg);
        if (currentShaderProg) updateMatrixUniforms();
        glUniform1i(loc_raycast, 1);
        glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), (float)renderW/(float)renderH, camera.znear, camera.zfar);
        glm::mat4 viewMat = glm::lookAt(glm::vec3(camera.eye_x, camera.eye_y, camera.eye_z),
                                        glm::vec3(camera.ctr_x, camera.ctr_y, camera.ctr_z),
                                        glm::vec3(camera.up_x, camera.up_y, camera.up_z));
        glm::mat4 invVP = glm::inverse(projMat * viewMat);
        glUniformMatrix4fv(loc_invViewProj, 1, GL_FALSE, &invVP[0][0]);
        glUniform3f(loc_camPos, camera.eye_x, camera.eye_y, camera.eye_z);
        glUniform3fv(loc_ambient, 1, global_ambient);
        glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
        glUniform1f(loc_fogStart, fog.start);
        glUniform1f(loc_fogEnd, fog.end);
        int nLights = std::min((int)activeLights.size(), (int)locLightEnabled.size());
        glUniform1i(loc_numLights, nLights);
        for (int i = 0; i < nLights; i++) {
            Light* l = activeLights[i];
            glUniform1i(locLightEnabled[i], 1);
            glUniform3f(locLightPosition[i], l->pos[0], l->pos[1], l->pos[2]);
            glUniform3f(locLightDirection[i], l->dir[0], l->dir[1], l->dir[2]);
            glUniform3f(locLightDiffuse[i], l->color[0]*l->intensity, l->color[1]*l->intensity, l->color[2]*l->intensity);
            glUniform1f(locLightCutoff[i], cosf(l->cutoff * M_PI / 180.0f));
            glUniform3f(locLightAttenuation[i], l->constAtt, l->linearAtt, l->quadAtt);
        }
        glUniform1i(loc_triCount, triCount);
        glUniform1i(loc_triTexWidth, triTexWidth);
        glUniform1i(loc_triTexHeight, triTexHeight);
        glUniform1i(loc_triTexPos, 2);
        glUniform1i(loc_triTexNorm, 3);
        glUniform1i(loc_triTexColor, 4);
        glUniform1i(loc_triTexIndices, 5);
        glUniform1i(loc_triTexUV, 6);
        if (bvhValid && cachedBvhTex != 0 && bvhNodes.size() > 0) {
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, cachedBvhTex);
            glUniform1i(loc_bvhTex, 10);
            glUniform1i(loc_bvhNodeCount, (int)bvhNodes.size());
            glUniform1i(loc_bvhTexWidth, 4);
            glUniform1i(loc_bvhTexHeight, (int)bvhNodes.size());
        } else {
            glUniform1i(loc_bvhNodeCount, 0);
        }
        for (int i = 0; i < (int)activeTextures.size(); i++) {
            glActiveTexture(GL_TEXTURE7 + i);
            glBindTexture(GL_TEXTURE_2D, activeTextures[i]);
            glUniform1i(locTexSlot[i], 7 + i);
        }
        for (int i = activeTextures.size(); i < 8; i++) glUniform1i(locTexSlot[i], 0);
        if (!panoramaCommands.empty() && sphere_sky.texture != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
            glUniform1i(loc_panoramaTex, 1);
            glUniform1i(loc_hasPanorama, 1);
        } else {
            glUniform1i(loc_hasPanorama, 0);
        }
        std::vector<float> portalPos(Engine_settings.MAX_PORTALS * 3, 0.0f);
        std::vector<float> portalNormal(Engine_settings.MAX_PORTALS * 3, 0.0f);
        std::vector<float> portalD(Engine_settings.MAX_PORTALS, 0.0f);
        std::vector<float> portalInvWorld(Engine_settings.MAX_PORTALS * 16, 0.0f);
        std::vector<int>   portalVertCount(Engine_settings.MAX_PORTALS, 0);
        std::vector<float> portalVerts(Engine_settings.MAX_PORTALS * Engine_settings.MAX_PORTAL_VERTS * 2, 0.0f);
        std::vector<float> portalTeleport(Engine_settings.MAX_PORTALS * 16, 0.0f);
        int portalCount = 0;
        if (hasPortalCommand) {
            std::vector<Portal*> uniquePortals;
            for (Portal* p : portalCommands) {
                if (std::find(uniquePortals.begin(), uniquePortals.end(), p) == uniquePortals.end()) {
                    uniquePortals.push_back(p);
                }
            }
            for (Portal* p : uniquePortals) {
                if (portalCount >= Engine_settings.MAX_PORTALS) break;
                auto addPortalSide = [&](float px, float py, float pz, float yaw, float pitch, float roll,
                                         float nx, float ny, float nz, const glm::mat4& teleport) {
                    portalPos[portalCount*3+0] = px; portalPos[portalCount*3+1] = py; portalPos[portalCount*3+2] = pz;
                    portalNormal[portalCount*3+0] = nx; portalNormal[portalCount*3+1] = ny; portalNormal[portalCount*3+2] = nz;
                    portalD[portalCount] = -(nx*px + ny*py + nz*pz);
                    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0,1,0));
                    rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1,0,0));
                    rot = glm::rotate(rot, glm::radians(roll), glm::vec3(0,0,1));
                    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(px,py,pz)) * rot;
                    glm::mat4 invWorld = glm::inverse(world);
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                        portalInvWorld[portalCount*16 + c*4 + r] = invWorld[c][r];
                    int vCount = (int)p->vertices.size() / 3;
                    portalVertCount[portalCount] = vCount;
                    for (int i = 0; i < vCount && i < Engine_settings.MAX_PORTAL_VERTS; ++i) {
                        portalVerts[portalCount * Engine_settings.MAX_PORTAL_VERTS * 2 + i*2 + 0] = p->vertices[i*3 + 0];
                        portalVerts[portalCount * Engine_settings.MAX_PORTAL_VERTS * 2 + i*2 + 1] = p->vertices[i*3 + 1];
                    }
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                        portalTeleport[portalCount*16 + c*4 + r] = teleport[c][r];
                    portalCount++;
                };
                glm::vec3 nA = p->portalNormal(p->ax, p->ay, p->az, false);
                glm::mat4 teleAtoB = p->getPortalTransform(p->ax, p->ay, p->az, p->bx, p->by, p->bz);
                addPortalSide(p->ax, p->ay, p->az, p->yawA, p->pitchA, p->rollA, nA.x, nA.y, nA.z, teleAtoB);
                if (portalCount < Engine_settings.MAX_PORTALS) {
                    glm::vec3 nB = p->portalNormal(p->bx, p->by, p->bz, true);
                    glm::mat4 teleBtoA = p->getPortalTransform(p->bx, p->by, p->bz, p->ax, p->ay, p->az);
                    addPortalSide(p->bx, p->by, p->bz, p->yawB, p->pitchB, p->rollB, nB.x, nB.y, nB.z, teleBtoA);
                }
            }
        }
        glUniform1i(loc_portalCount, portalCount);
        if (portalCount > 0) {
            glUniform3fv(loc_portalPos, portalCount, portalPos.data());
            glUniform3fv(loc_portalNormal, portalCount, portalNormal.data());
            glUniform1fv(loc_portalD, portalCount, portalD.data());
            glUniformMatrix4fv(loc_portalInvWorld, portalCount, GL_FALSE, portalInvWorld.data());
            glUniform1iv(loc_portalVertCount, portalCount, portalVertCount.data());
            glUniform2fv(loc_portalVerts, Engine_settings.MAX_PORTALS * Engine_settings.MAX_PORTAL_VERTS, portalVerts.data());
            glUniformMatrix4fv(loc_portalTeleport, portalCount, GL_FALSE, portalTeleport.data());
        }
        if (activeWarpPlane && activeWarpPlane->enabled) {
            glUniform1i(loc_warpEnabled, 1);
            glUniform3f(loc_warpOrigin, activeWarpPlane->originX, activeWarpPlane->originY, activeWarpPlane->originZ);
            glm::mat4 rot = glm::mat4(1.0f);
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->yaw), glm::vec3(0,1,0));
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->pitch), glm::vec3(1,0,0));
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->roll), glm::vec3(0,0,1));
            glm::vec3 uAxis = glm::vec3(rot * glm::vec4(activeWarpPlane->sizeU, 0, 0, 0));
            glm::vec3 vAxis = glm::vec3(rot * glm::vec4(0, activeWarpPlane->sizeV, 0, 0));
            glUniform3fv(loc_warpAxisU, 1, &uAxis[0]);
            glUniform3fv(loc_warpAxisV, 1, &vAxis[0]);
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_2D, activeWarpPlane->displacementTex);
            glUniform1i(loc_warpDisplacementTex, 11);
        } else {
            glUniform1i(loc_warpEnabled, 0);
        }
        glUniform1f(loc_maxDist, Engine_settings.MAX_DIST);
        glUniform1f(loc_shadowBias, Engine_settings.SHADOW_BIAS);
        glUniform1f(loc_camWarpStrength, Engine_settings.CAM_WARP_STRENGTH);
        glUniform1f(loc_shadowWarpStrength, Engine_settings.SHADOW_WARP_STRENGTH);
        glUniform1f(loc_camStepSize, Engine_settings.CAM_STEP_SIZE);
        glUniform1f(loc_shadowStepSize, Engine_settings.SHADOW_STEP_SIZE);
        glUniform1i(loc_maxBounces, Engine_settings.MAX_BOUNCES);
        glUniform1i(loc_maxShadowBounces, Engine_settings.MAX_SHADOW_BOUNCES);
        glUniform1i(loc_raySamples, raySamples);
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
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glBindVertexArray(fullScreenVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
        g_projectionMatrix = prevProj;
        g_modelViewMatrix = prevModel;
        if (currentShaderProg) updateMatrixUniforms();
    }

    if (useFBO) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_w, window_h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        useShader(current2DShader);
        if (current2DShader) {
            glUseProgram(current2DShader);
            if (loc_u_projection_2d != -1) {
                glm::mat4 proj = glm::mat4(1.0f);
                glUniformMatrix4fv(loc_u_projection_2d, 1, GL_FALSE, &proj[0][0]);
            }
            if (loc_u_modelView_2d != -1) {
                glm::mat4 mv = glm::mat4(1.0f);
                glUniformMatrix4fv(loc_u_modelView_2d, 1, GL_FALSE, &mv[0][0]);
            }
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glUniform1i(loc_tex_2d, 0);

        static GLuint blitVAO = 0, blitVBO = 0;
        if (blitVAO == 0) {
            float vertices[] = {
                -1.0f, -1.0f,   0.0f, 0.0f,
                 1.0f, -1.0f,   1.0f, 0.0f,
                -1.0f,  1.0f,   0.0f, 1.0f,
                 1.0f,  1.0f,   1.0f, 1.0f
            };
            glGenVertexArrays(1, &blitVAO);
            glGenBuffers(1, &blitVBO);
            glBindVertexArray(blitVAO);
            glBindBuffer(GL_ARRAY_BUFFER, blitVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0); // позиция
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(8); // текстурные координаты (aTexCoord)
            glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glBindVertexArray(0);
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(blitVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    glEnable(GL_BLEND);
    if (!commands2D.empty()) {
        static GLuint sq_vao = 0, sq_vbo = 0, sq_ibo = 0;
        static bool sq_init = false;
        if (!sq_init) {
            sq_init = true;
            glGenVertexArrays(1, &sq_vao); glGenBuffers(1, &sq_vbo); glGenBuffers(1, &sq_ibo);
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
        glm::mat4 prevProj2d = g_projectionMatrix;
        glm::mat4 prevModel2d = g_modelViewMatrix;
        g_projectionMatrix = glm::ortho(0.0f, (float)window_w, 0.0f, (float)window_h, -1.0f, 1.0f);
        g_modelViewMatrix = glm::mat4(1.0f);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        useShader(current2DShader);
        if (currentShaderProg) updateMatrixUniforms();
        glUniform1i(loc_tex_2d, 0);
        for (auto& cmd : commands2D) {
            switch (cmd.type) {
                case CMD_SQUARE: {
                    const char* texName = cmd.tex.empty() ? nullptr : cmd.tex.c_str();
                    glActiveTexture(GL_TEXTURE0);
                    if (texName) { GLuint id = loadTextureFromFile(texName); glBindTexture(GL_TEXTURE_2D, id ? id : whiteTex); }
                    else { glBindTexture(GL_TEXTURE_2D, whiteTex); }
                    float ar = cmd.rotate * M_PI / -180.0f;
                    float tc[8] = {0,1, 1,1, 1,0, 0,0};
                    float data[32];
                    for (int i = 0; i < 4; ++i) {
                        float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                        rotatePoint(px, py, 0, 0, ar);
                        float vx = cmd.cx + px * cmd.scale, vy = cmd.cy + py * cmd.scale;
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
                case CMD_LINE_2D: {
                    float data[18] = {
                        cmd.verts[0], cmd.verts[1], 0, cmd.r, cmd.g, cmd.b, cmd.a, 0, 0,
                        cmd.verts[2], cmd.verts[3], 0, cmd.r, cmd.g, cmd.b, cmd.a, 0, 0
                    };
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
        g_projectionMatrix = prevProj2d;
        g_modelViewMatrix = prevModel2d;
        if (currentShaderProg) updateMatrixUniforms();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }
}

//              утилиты
// поворачиваем текстуру вокруг точки
void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad){
    // перенос в 0 для удобного рассчёта
    const float tx=x-cx,ty=y-cy;
    // рассчёт поворота и возвращаем как было
    const float c=cosf(angle_rad),s=sinf(angle_rad);
    x=cx+tx*c-ty*s;
    y=cy+tx*s+ty*c;
}

//              opengl
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    window_w = width;
    window_h = height;
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
    glfwSwapInterval(0);

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
    init2DShader();
    initLineVAO();
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}
//                                                                                      КОЛХОЗ!!! ПОТОМ СРОЧНО ИСПРАВИТЬ!!!
float global_pitch,global_yaw;
// настройка камеры
void setup_camera(float fov, float eye_x, float eye_y, float eye_z, float pitch, float yaw, float roll) {
    camera.fov = fov;
    camera.eye_x = eye_x; 
    camera.eye_y = eye_y;
    camera.eye_z = eye_z;

    float norm_pitch = fmod(pitch, 360.0f);
    if (norm_pitch < 0) norm_pitch += 360.0f;

    float adj_pitch = norm_pitch;
    float up_x = 0, up_y = 1, up_z = 0;

    bool is_inverted = (norm_pitch > 90.0f && norm_pitch < 270.0f);
    if (is_inverted != camera.was_inverted) {
        yaw += 180.0f; 
    }
    camera.was_inverted = is_inverted;

    if (is_inverted) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;
    } else {
        if (norm_pitch > 270.0f) adj_pitch = norm_pitch - 360.0f;
        up_y = 1.0f;
    }

    lookAtForward(eye_x, eye_y, eye_z, adj_pitch, yaw, camera.ctr_x, camera.ctr_y, camera.ctr_z, camera.dir_x, camera.dir_y, camera.dir_z);

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

    ma_engine_listener_set_position(&audio_engine, 0, eye_x, eye_y, eye_z);
    ma_engine_listener_set_direction(&audio_engine, 0, camera.dir_x, camera.dir_y, camera.dir_z);
    if (is_inverted)
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, -1.0f, 0.0f);
    else
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, 1.0f, 0.0f);

    global_pitch = pitch;
    global_yaw = yaw;
    camera.pitch = pitch;
    camera.yaw   = yaw;
    camera.roll  = roll;

    float aspect = (window_h > 0) ? float(window_w) / float(window_h) : 1.0f;
    g_projectionMatrix = glm::perspective(glm::radians(fov), aspect, camera.znear, camera.zfar);
    g_modelViewMatrix = glm::lookAt(glm::vec3(eye_x, eye_y, eye_z),
                                    glm::vec3(camera.ctr_x, camera.ctr_y, camera.ctr_z),
                                    glm::vec3(up_x, up_y, up_z));
    updateMatrixUniforms();
}
// перемещение камеры
void move_camera(float eye_x, float eye_y, float eye_z, float pitch, float yaw, float roll) {
    camera.eye_x = eye_x; 
    camera.eye_y = eye_y;
    camera.eye_z = eye_z;

    float norm_pitch = fmod(pitch, 360.0f);
    if (norm_pitch < 0) norm_pitch += 360.0f;

    float adj_pitch = norm_pitch;
    float up_x = 0, up_y = 1, up_z = 0;

    bool is_inverted = (norm_pitch > 90.0f && norm_pitch < 270.0f);
    if (is_inverted != camera.was_inverted) {
        yaw += 180.0f; 
    }
    camera.was_inverted = is_inverted;

    if (is_inverted) {
        adj_pitch = 180.0f - norm_pitch; 
        up_y = -1.0f;
    } else {
        if (norm_pitch > 270.0f) adj_pitch = norm_pitch - 360.0f;
        up_y = 1.0f;
    }

    lookAtForward(eye_x, eye_y, eye_z, adj_pitch, yaw, camera.ctr_x, camera.ctr_y, camera.ctr_z, camera.dir_x, camera.dir_y, camera.dir_z);

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

    ma_engine_listener_set_position(&audio_engine, 0, eye_x, eye_y, eye_z);
    ma_engine_listener_set_direction(&audio_engine, 0, camera.dir_x, camera.dir_y, camera.dir_z);
    if (is_inverted)
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, -1.0f, 0.0f);
    else
        ma_engine_listener_set_world_up(&audio_engine, 0, 0.0f, 1.0f, 0.0f);

    global_pitch = pitch;
    global_yaw = yaw;
    camera.pitch = pitch;
    camera.yaw   = yaw;
    camera.roll  = roll;

    float aspect = (window_h > 0) ? float(window_w) / float(window_h) : 1.0f;
    g_projectionMatrix = glm::perspective(glm::radians(camera.fov), aspect, camera.znear, camera.zfar);
    g_modelViewMatrix = glm::lookAt(glm::vec3(eye_x, eye_y, eye_z),
                                    glm::vec3(camera.ctr_x, camera.ctr_y, camera.ctr_z),
                                    glm::vec3(up_x, up_y, up_z));
    updateMatrixUniforms();
}