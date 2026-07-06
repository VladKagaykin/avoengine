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
#include "ray_casting.h"

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

// кэш uniform-локаций для основного шейдера
GLint loc_tex = -1;
GLint loc_portalMode = -1, loc_portalDepthOnly = -1, loc_portalTex = -1;

glm::mat4 g_projectionMatrix(1.0f);
glm::mat4 g_modelViewMatrix(1.0f);
GLint loc_u_projection_default = -1;
GLint loc_u_modelView_default = -1;

void updateMatrixUniforms() {
    if (loc_u_projection_default != -1) glUniformMatrix4fv(loc_u_projection_default, 1, GL_FALSE, &g_projectionMatrix[0][0]);
    if (loc_u_modelView_default != -1) glUniformMatrix4fv(loc_u_modelView_default, 1, GL_FALSE, &g_modelViewMatrix[0][0]);
    if (loc_u_projection_2d != -1) glUniformMatrix4fv(loc_u_projection_2d, 1, GL_FALSE, &g_projectionMatrix[0][0]);
    if (loc_u_modelView_2d != -1) glUniformMatrix4fv(loc_u_modelView_2d, 1, GL_FALSE, &g_modelViewMatrix[0][0]);
}

//              очень ужасный рэндеринг

std::vector<DrawCommand> drawQueue;
std::mutex drawQueueMutex;

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