#include "avoextension.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#define GL_GLEXT_PROTOTYPES
#include "miniaudio.h"
#include <vector>
#include <cstring>
#include <GLFW/glfw3.h>
#include <SOIL/SOIL.h>
#include <string>
#include <iostream>
#include <map>
#include <unordered_map>
#include <chrono>

#include "portals_rc.h"
#include "pseudo3dentity.h"
#include "2d_primitives.h"
#include "3d_primitives.h"

using namespace std;

//              утилиты
// система тиков
int tick=0;
const int max_tick=20;
int absolute_tick = 0;
static std::chrono::steady_clock::time_point last_tick_time;
static const std::chrono::microseconds tick_interval(50000);

void init_tick_system() {
    last_tick_time = std::chrono::steady_clock::now();
}

void update_ticks() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick_time);

    int ticks_to_add = static_cast<int>(elapsed.count() / tick_interval.count());
    if (ticks_to_add > 0) {
        absolute_tick += ticks_to_add;
        tick = (tick + ticks_to_add) % (max_tick + 1);
        last_tick_time += ticks_to_add * tick_interval;
    }
}

// поставить иконку
void set_icon(const char* path){
    int width, height, channels;
    // загружаем с принудительным RGBA (4 канала)
    unsigned char* image = SOIL_load_image(
        path,
        &width, &height, &channels,
        SOIL_LOAD_RGBA
    );

    if (!image) {
        std::cerr << "SOIL failed to load icon: " << SOIL_last_result() << std::endl;
        return;
    }

    GLFWimage icon;
    icon.width  = width;
    icon.height = height;
    icon.pixels = image;

    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        glfwSetWindowIcon(window, 1, &icon);
    }

    // освобождаем память, занятую SOIL
    SOIL_free_image_data(image);
}
// считывание клавиш клавиатуры
bool keys[256]={},skeys[512]={};
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key >= 0 && key < 256) {
        keys[key] = (action == GLFW_PRESS || action == GLFW_REPEAT);
    }
    if (key >= 256 && key < 512) {
        skeys[key] = (action == GLFW_PRESS || action == GLFW_REPEAT);
    }
}
void init_keyboard(GLFWwindow* window) {
    glfwSetKeyCallback(window, key_callback);
}
// считывание мыши
std::map<std::string, bool> mouse;
int mouse_x = 0, mouse_y = 0;
bool mouse_captured = false;
static double last_mouse_x = 0.0, last_mouse_y = 0.0;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    std::string btn = (button == GLFW_MOUSE_BUTTON_LEFT)   ? "left" :
                      (button == GLFW_MOUSE_BUTTON_MIDDLE) ? "middle" : "right";
    mouse[btn] = (action == GLFW_PRESS);
    mouse[btn + "_click"] = (action == GLFW_PRESS);
    // Позиция обновляется в cursor_pos_callback
}

void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mouse_captured) {
        // Режим захвата: вычисляем дельту и варпим в центр
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        double center_x = width / 2.0;
        double center_y = height / 2.0;
        mouse_x += static_cast<int>(xpos - center_x);
        mouse_y += static_cast<int>(ypos - center_y);
        glfwSetCursorPos(window, center_x, center_y);
    } else {
        mouse_x = static_cast<int>(xpos);
        mouse_y = static_cast<int>(ypos);
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (yoffset > 0) mouse["wheel_up"] = true;
    else if (yoffset < 0) mouse["wheel_down"] = true;
}

void init_mouse(GLFWwindow* window) {
    mouse["left"] = false;
    mouse["right"] = false;
    mouse["middle"] = false;
    mouse["left_click"] = false;
    mouse["right_click"] = false;
    mouse["middle_click"] = false;
    mouse["wheel_up"] = false;
    mouse["wheel_down"] = false;

    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
}

void update_mouse() {
    mouse["left_click"] = false;
    mouse["right_click"] = false;
    mouse["middle_click"] = false;
    mouse["wheel_up"] = false;
    mouse["wheel_down"] = false;
}

// Управление захватом мыши
void set_mouse_capture(GLFWwindow* window, bool capture) {
    if (capture && !mouse_captured) {
        mouse_captured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // Сброс дельты при входе в захват
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        last_mouse_x = width / 2.0;
        last_mouse_y = height / 2.0;
        glfwSetCursorPos(window, last_mouse_x, last_mouse_y);
    } else if (!capture && mouse_captured) {
        mouse_captured = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
//          простые 3д примитивы
// плоскость
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices) {
    if (vertices.size() != 12) return;

    float ax = vertices[3] - vertices[0];
    float ay = vertices[4] - vertices[1];
    float az = vertices[5] - vertices[2];
    float bx = vertices[6] - vertices[0];
    float by = vertices[7] - vertices[1];
    float bz = vertices[8] - vertices[2];

    float nx = ay * bz - az * by;
    float ny = az * bx - ax * bz;
    float nz = ax * by - ay * bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }

    std::vector<int> indices = { 0, 1, 2, 0, 2, 3 };
    std::vector<float> texcoords = { 0,0, 1,0, 1,1, 0,1 };
    std::vector<float> normals = {
        nx, ny, nz,
        nx, ny, nz,
        nx, ny, nz,
        nx, ny, nz
    };

    draw3DObject(cx, cy, cz, r, g, b, tex, vertices, indices, texcoords, normals);
}

//              hud
void delay_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                float r, float g, float b, float a, int ticks, bool loop){
    int length = strlen(text);
    int current = loop ? absolute_tick % ticks : absolute_tick;
    float one_char_timing = (float)ticks / length;
    int visible = int(current / one_char_timing);
    if (visible > length) visible = length;
    char buff[length + 1];
    memset(buff, 0, length + 1);
    for (int c = 0; c < visible; c++) {
        buff[c] = text[c];
        draw_text(buff, x, y, fontPath, fontSize, r, g, b, a);
    }
    draw_text("", x, y, fontPath, fontSize, r, g, b, a);
}
void disappearing_text(const char* text, float x, float y, const char* fontPath, int fontSize,
                       float r, float g, float b, float a, int ticks, bool loop){
    int current = loop ? absolute_tick % ticks : absolute_tick;
    float current_alpha = a - (a / ticks) * current;
    if (current_alpha < 0) current_alpha = 0;
    draw_text(text, x, y, fontPath, fontSize, r, g, b, current_alpha);
}
//              звук
// audio_engine уже есть в avoengine.cpp
extern ma_engine audio_engine;
// белый шум
static ma_noise noise_src;
static ma_audio_buffer* noise_buf=nullptr;
static ma_sound noise_snd;
static bool noise_init=false;
static float noiseData[48000*2];
void play_white_noise_3d(float x,float y,float z,float volume){
    if (!noise_init){
        ma_noise_config cfg=ma_noise_config_init(ma_format_f32,2,ma_noise_type_white,0,0.3f);
        ma_noise_init(&cfg,nullptr,&noise_src);
        ma_noise_read_pcm_frames(&noise_src,noiseData,48000,nullptr);
        ma_audio_buffer_config bcfg=ma_audio_buffer_config_init(ma_format_f32,2,48000,noiseData,nullptr);
        ma_audio_buffer_alloc_and_init(&bcfg,&noise_buf);
        ma_sound_init_from_data_source(&audio_engine,noise_buf,0,nullptr,&noise_snd);
        noise_init=true;
    }
    ma_sound_set_positioning(&noise_snd,ma_positioning_absolute);
    ma_sound_set_position(&noise_snd,x,y,z);
    ma_sound_set_spatialization_enabled(&noise_snd,MA_TRUE);
    ma_sound_set_volume(&noise_snd,volume);
    ma_sound_set_looping(&noise_snd,MA_TRUE);
    ma_sound_start(&noise_snd);
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
    USER = 0x52455355,
    PORT = 0x54524F50 
};

// static void write_chunk(std::vector<uint8_t>& out, ChunkType type, const std::vector<uint8_t>& data) {
//     uint32_t fourcc = static_cast<uint32_t>(type);
//     out.push_back(fourcc & 0xFF);
//     out.push_back((fourcc >> 8) & 0xFF);
//     out.push_back((fourcc >> 16) & 0xFF);
//     out.push_back((fourcc >> 24) & 0xFF);
//     write_u32(out, (uint32_t)data.size());
//     out.insert(out.end(), data.begin(), data.end());
// }

// static bool load_map_internal(const std::vector<uint8_t>& filedata, MapData& map) {
//     const uint8_t* p = filedata.data();
//     size_t remaining = filedata.size();

//     if (remaining < 4) return false;
//     if (memcmp(p, "AVOM", 4) != 0) return false;
//     p += 4; remaining -= 4;

//     if (remaining < 4) return false;
//     uint16_t ver_major = p[0] | (p[1] << 8);
//     uint16_t ver_minor = p[2] | (p[3] << 8);
//     p += 4; remaining -= 4;

//     if (ver_major != 1) {
//         std::cerr << "Unsupported .avomap major version " << ver_major << std::endl;
//         return false;
//     }

//     while (remaining >= 8) {
//         uint32_t fourcc;
//         memcpy(&fourcc, p, 4);
//         p += 4; remaining -= 4;

//         uint32_t chunkSize;
//         if (!read_u32(p, remaining, chunkSize)) return false;
//         if (chunkSize > remaining) return false;

//         const uint8_t* chunkData = p;
//         p += chunkSize;
//         remaining -= chunkSize;

//         ChunkType ctype = static_cast<ChunkType>(fourcc);

//         switch (ctype) {
//             case ChunkType::ENTY: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 MapEntity ent;
//                 if (!read_float(d, r, ent.x) || !read_float(d, r, ent.y) || !read_float(d, r, ent.z)) break;
//                 if (!read_float(d, r, ent.g_angle) || !read_float(d, r, ent.v_angle) || !read_float(d, r, ent.r_angle)) break;
//                 if (!read_u32(d, r, (uint32_t&)ent.v_angles)) break;
//                 uint32_t texCount;
//                 if (!read_u32(d, r, texCount)) break;
//                 ent.textures.resize(texCount);
//                 for (auto& tex : ent.textures) {
//                     if (!read_string(d, r, tex)) break;
//                 }
//                 uint32_t vertCount;
//                 if (!read_u32(d, r, vertCount)) break;
//                 ent.vertices.resize(vertCount);
//                 for (auto& v : ent.vertices) {
//                     if (!read_float(d, r, v)) break;
//                 }
//                 if (d <= p) {
//                     uint8_t shadowByte = 0;
//                     if (r > 0) {
//                         shadowByte = d[0];
//                         d++; r--;
//                     }
//                     ent.castShadow = (shadowByte != 0);
//                 }
//                 map.entities.push_back(ent);
//                 break;
//             }
//             case ChunkType::LITE: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 MapData::LightData light;
//                 uint32_t enabled32;
//                 if (!read_u32(d, r, enabled32)) break;
//                 light.enabled = (enabled32 != 0);
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.pos[i])) break;
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.dir[i])) break;
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, light.color[i])) break;
//                 if (!read_float(d, r, light.intensity)) break;
//                 if (!read_float(d, r, light.cutoff)) break;
//                 if (!read_float(d, r, light.constAtt)) break;
//                 if (!read_float(d, r, light.linearAtt)) break;
//                 if (!read_float(d, r, light.quadAtt)) break;
//                 map.lights.push_back(light);
//                 break;
//             }
//             case ChunkType::FOGS: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 uint32_t enabled32;
//                 if (!read_u32(d, r, enabled32)) break;
//                 map.fog_enabled = (enabled32 != 0);
//                 if (!read_float(d, r, map.fog_density)) break;
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.fog_color[i])) break;
//                 if (!read_float(d, r, map.fog_start)) break;
//                 if (!read_float(d, r, map.fog_end)) break;
//                 break;
//             }
//             case ChunkType::CAME: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.camera_eye[i])) break;
//                 if (!read_float(d, r, map.camera_pitch)) break;
//                 if (!read_float(d, r, map.camera_yaw)) break;
//                 break;
//             }
//             case ChunkType::PANO: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 if (!read_string(d, r, map.panorama_path)) break;
//                 break;
//             }
//             case ChunkType::AMBI: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 for (int i = 0; i < 3; ++i) if (!read_float(d, r, map.ambient[i])) break;
//                 break;
//             }
//             case ChunkType::USER: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 uint32_t numEntries;
//                 if (!read_u32(d, r, numEntries)) break;
//                 for (uint32_t i = 0; i < numEntries; ++i) {
//                     std::string key;
//                     if (!read_string(d, r, key)) break;
//                     uint32_t dataLen;
//                     if (!read_u32(d, r, dataLen)) break;
//                     if (dataLen > r) break;
//                     std::vector<uint8_t> blob(d, d + dataLen);
//                     d += dataLen; r -= dataLen;
//                     map.userData[key] = std::move(blob);
//                 }
//                 break;
//             }
//             case ChunkType::PORT: {
//                 if (chunkSize < 1) break;
//                 uint8_t ver = chunkData[0];
//                 if (ver != 0) break;
//                 const uint8_t* d = chunkData + 1;
//                 size_t r = chunkSize - 1;
//                 while (r >= 48) { 
//                     MapData::PortalData portal;
//                     auto read = [&](float& val) { return read_float(d, r, val); };
//                     if (!read(portal.ax) || !read(portal.ay) || !read(portal.az) ||
//                         !read(portal.bx) || !read(portal.by) || !read(portal.bz))
//                         break;
//                     if (!read(portal.yawA) || !read(portal.pitchA) || !read(portal.rollA) ||
//                         !read(portal.yawB) || !read(portal.pitchB) || !read(portal.rollB))
//                         break;
//                     uint32_t vcount;
//                     if (!read_u32(d, r, vcount)) break;
//                     if (r < vcount * sizeof(float)) break;
//                     portal.vertices.resize(vcount);
//                     for (uint32_t i = 0; i < vcount; ++i)
//                         if (!read_float(d, r, portal.vertices[i])) break;
//                     map.portals.push_back(portal);
//                 }
//                 break;
//             }
//             default:
//                 break;
//         }
//     }
//     return true;
// }

// bool save_map(const char* filename, const MapData& map) {
//     std::vector<uint8_t> out;

//     out.push_back('A'); out.push_back('V'); out.push_back('O'); out.push_back('M');
//     out.push_back(1); out.push_back(0);      // major = 1 (little‑endian)
//     out.push_back(0); out.push_back(0);

//     for (const auto& ent : map.entities) {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         write_float(data, ent.x);
//         write_float(data, ent.y);
//         write_float(data, ent.z);
//         write_float(data, ent.g_angle);
//         write_float(data, ent.v_angle);
//         write_float(data, ent.r_angle);
//         write_u32(data, ent.v_angles);
//         write_u32(data, (uint32_t)ent.textures.size());
//         for (const auto& t : ent.textures) write_string(data, t);
//         write_u32(data, (uint32_t)ent.vertices.size());
//         for (float v : ent.vertices) write_float(data, v);
//         data.push_back(ent.castShadow ? 1 : 0);
//         write_chunk(out, ChunkType::ENTY, data);
//     }

//     for (const auto& light : map.lights) {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         write_u32(data, light.enabled ? 1 : 0);
//         for (int i = 0; i < 3; ++i) write_float(data, light.pos[i]);
//         for (int i = 0; i < 3; ++i) write_float(data, light.dir[i]);
//         for (int i = 0; i < 3; ++i) write_float(data, light.color[i]);
//         write_float(data, light.intensity);
//         write_float(data, light.cutoff);
//         write_float(data, light.constAtt);
//         write_float(data, light.linearAtt);
//         write_float(data, light.quadAtt);
//         write_chunk(out, ChunkType::LITE, data);
//     }

//     {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         write_u32(data, map.fog_enabled ? 1 : 0);
//         write_float(data, map.fog_density);
//         for (int i = 0; i < 3; ++i) write_float(data, map.fog_color[i]);
//         write_float(data, map.fog_start);
//         write_float(data, map.fog_end);
//         write_chunk(out, ChunkType::FOGS, data);
//     }

//     {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         for (int i = 0; i < 3; ++i) write_float(data, map.camera_eye[i]);
//         write_float(data, map.camera_pitch);
//         write_float(data, map.camera_yaw);
//         write_chunk(out, ChunkType::CAME, data);
//     }

//     if (!map.panorama_path.empty()) {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         write_string(data, map.panorama_path);
//         write_chunk(out, ChunkType::PANO, data);
//     }

//     {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         for (int i = 0; i < 3; ++i) write_float(data, map.ambient[i]);
//         write_chunk(out, ChunkType::AMBI, data);
//     }

//     if (!map.portals.empty()) {
//         std::vector<uint8_t> data;
//         data.push_back(0); 
//         for (const auto& p : map.portals) {
//             write_float(data, p.ax); write_float(data, p.ay); write_float(data, p.az);
//             write_float(data, p.bx); write_float(data, p.by); write_float(data, p.bz);
//             write_float(data, p.yawA); write_float(data, p.pitchA); write_float(data, p.rollA);
//             write_float(data, p.yawB); write_float(data, p.pitchB); write_float(data, p.rollB);
//             write_u32(data, (uint32_t)p.vertices.size());
//             for (float v : p.vertices) write_float(data, v);
//         }
//         write_chunk(out, ChunkType::PORT, data);
//     }

//     if (!map.userData.empty()) {
//         std::vector<uint8_t> data;
//         data.push_back(0);
//         write_u32(data, (uint32_t)map.userData.size());
//         for (const auto& [key, blob] : map.userData) {
//             write_string(data, key);
//             write_u32(data, (uint32_t)blob.size());
//             data.insert(data.end(), blob.begin(), blob.end());
//         }
//         write_chunk(out, ChunkType::USER, data);
//     }

//     FILE* f = fopen(filename, "wb");
//     if (!f) return false;
//     fwrite(out.data(), 1, out.size(), f);
//     fclose(f);
//     return true;
// }

// bool load_map(const char* filename, MapData& map) {
//     FILE* f = fopen(filename, "rb");
//     if (!f) return false;
//     fseek(f, 0, SEEK_END);
//     long size = ftell(f);
//     fseek(f, 0, SEEK_SET);
//     std::vector<uint8_t> data(size);
//     fread(data.data(), 1, size, f);
//     fclose(f);
//     return load_map_internal(data, map);
// }

// MapEntity entityToMapData(const pseudo_3d_entity& ent) {
//     MapEntity me;
//     me.x = ent.getX();
//     me.y = ent.getY();
//     me.z = ent.getZ();
//     me.g_angle = ent.getGAngle();
//     me.v_angle = ent.getVAngle();
//     me.r_angle = ent.getRAngle();
//     me.v_angles = ent.getVAngles();
//     me.textures = ent.getTextures();
//     me.vertices = ent.getVertices();
//     return me;
// }

// pseudo_3d_entity* mapDataToEntity(const MapEntity& data) {
//     return new pseudo_3d_entity(data.x, data.y, data.z,
//                                 data.g_angle, data.v_angle, data.r_angle,
//                                 data.textures, data.v_angles, data.vertices);
// }

// MapData::LightData lightToMapData(const Light& light) {
//     MapData::LightData l;
//     l.enabled = light.isEnabled();
//     memcpy(l.pos, light.pos, sizeof(l.pos));
//     memcpy(l.dir, light.dir, sizeof(l.dir));
//     memcpy(l.color, light.color, sizeof(l.color));
//     l.intensity = light.intensity;
//     l.cutoff = light.cutoff;
//     l.constAtt = light.constAtt;
//     l.linearAtt = light.linearAtt;
//     l.quadAtt = light.quadAtt;
//     return l;
// }

// void mapDataToLight(const MapData::LightData& data, Light& out) {
//     out.setPosition(data.pos[0], data.pos[1], data.pos[2]);
//     memcpy(out.dir, data.dir, sizeof(out.dir));
//     out.setColor(data.color[0], data.color[1], data.color[2]);
//     out.setIntensity(data.intensity);
//     out.setRadius(data.cutoff);
//     out.setAttenuation(data.constAtt, data.linearAtt, data.quadAtt);
//     if (data.enabled) out.enable(); else out.disable();
// }

// void registerEntity(pseudo_3d_entity* e) {
//     if (std::find(allEntities.begin(), allEntities.end(), e) == allEntities.end()) {
//         allEntities.push_back(e);
//     }
// }

// void unregisterEntity(pseudo_3d_entity* e) {
//     auto it = std::find(allEntities.begin(), allEntities.end(), e);
//     if (it != allEntities.end()) allEntities.erase(it);
// }

// void save_current_scene(const char* filename) {
//     MapData map;

//     for (auto* e : allEntities) {
//         map.entities.push_back(entityToMapData(*e));
//     }
//     for (auto* p : allPortals) {
//         map.portals.push_back(portalToMapData(*p));
//     }
//     for (auto* l : activeLights) {
//         map.lights.push_back(lightToMapData(*l));
//     }

//     map.fog_enabled = fog.enabled;
//     map.fog_density = fog.density;
//     memcpy(map.fog_color, fog.color, sizeof(fog.color));
//     map.fog_start = fog.start;
//     map.fog_end = fog.end;

//     map.camera_eye[0] = camera.eye_x;
//     map.camera_eye[1] = camera.eye_y;
//     map.camera_eye[2] = camera.eye_z;
//     map.camera_pitch = camera.pitch;
//     map.camera_yaw = camera.yaw;

//     extern sphere_panorama sphere_sky;
//     if (sphere_sky.enabled) {
//         map.panorama_path = sphere_sky.path;
//     }

//     memcpy(map.ambient, global_ambient, sizeof(global_ambient));

//     save_map(filename, map);
// }
// MapData::PortalData portalToMapData(const Portal& p) {
//     MapData::PortalData data;
//     data.ax = p.ax; data.ay = p.ay; data.az = p.az;
//     data.bx = p.bx; data.by = p.by; data.bz = p.bz;
//     data.yawA = p.yawA; data.pitchA = p.pitchA; data.rollA = p.rollA;
//     data.yawB = p.yawB; data.pitchB = p.pitchB; data.rollB = p.rollB;
//     data.vertices = p.vertices;
//     return data;
// }

// Portal* mapDataToPortal(const MapData::PortalData& data) {
//     return new Portal(data.ax, data.ay, data.az,
//                       data.bx, data.by, data.bz,
//                       data.vertices,
//                       data.yawA, data.pitchA, data.rollA,
//                       data.yawB, data.pitchB, data.rollB);
// }