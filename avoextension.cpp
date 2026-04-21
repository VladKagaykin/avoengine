#include "avoextension.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#define GL_GLEXT_PROTOTYPES
#include "miniaudio.h"
#include <vector>
#include <cstring>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include <SOIL/SOIL.h>
#include <string>
#include <iostream>
#include <map>
#include <unordered_map>
#include <chrono>
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
void set_icon(const char* path) {
    int width, height, channels;
    unsigned char* image = stbi_load(path, &width, &height, &channels, 4); // принудительно RGBA
    if (!image) return;

    GLFWimage icon;
    icon.width = width;
    icon.height = height;
    icon.pixels = image;

    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        glfwSetWindowIcon(window, 1, &icon);
    }

    stbi_image_free(image);
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
void plane(float cx, float cy, float cz,double r, double g, double b,const char* tex,const std::vector<float>& vertices){
    if (vertices.size() < 12) return;
    std::vector<int> indices = { 
        0,1,4, 
        1,2,4,  
        2,3,4,  
        3,0,4  
    };
    std::vector<float> texcoords = { 
        0,0, 
        1,0,  
        1,1,  
        0,1,  
        0.5f,0.5f 
    };
    std::vector<float> normals;
    for (int i = 0; i < 5; ++i) {
        normals.push_back(0.0f);  
        normals.push_back(1.0f); 
        normals.push_back(0.0f); 
    }
    draw3DObject(cx, cy, cz, r, g, b, tex,vertices, indices, texcoords, normals);
}

// панорама
struct sphere_panorama{
    bool enabled = false;
    GLuint texture = 0;
};
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
//              hud
void delay_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop){
    int length=strlen(text);
    int current=loop?absolute_tick%ticks:absolute_tick;
    float one_char_timing=(float)ticks/length;
    int visible=int(current/one_char_timing);
    if(visible>length)visible=length;
    char buff[length+1];
    memset(buff,0,length+1);
    for (int c=0;c<visible;c++){
        buff[c]=text[c];
        draw_text(buff,x,y,font,r,g,b,a);
    }
    draw_text("",x,y,font,r,g,b,a);
}
void disappearing_text(const char* text,float x,float y,void* font,float r,float g,float b,float a,int ticks,bool loop){
    int current=loop?absolute_tick%ticks:absolute_tick;
    float current_alpha=a-(a/ticks)*current;
    if(current_alpha<0)current_alpha=0;
    draw_text(text,x,y,font,r,g,b,current_alpha);
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
