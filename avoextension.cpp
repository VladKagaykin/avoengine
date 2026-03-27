#include "avoextension.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#define GL_GLEXT_PROTOTYPES
#include "miniaudio.h"
#include <vector>
#include <cstring>
#include <GL/glut.h>
#include <SOIL/SOIL.h>
#include <string>
using namespace std;

int tick=0;
const int max_tick=64;

//              утилиты
// система тиков
int absolute_tick = 0;

void timer() {
    glutTimerFunc(16, [](int){ timer(); }, 0);
    tick++;
    if (tick > max_tick) tick = 0;
    absolute_tick++;
}
void init_tick_system(){
    timer();
}
void render_loop(int) {
    glutPostRedisplay();
    glutTimerFunc(16, render_loop, 0);
}
//          простые 3д примитивы
// плоскость
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices){
    if(vertices.size()<12)return;
    std::vector<int> indices={
        0,1,2,
        0,2,3};
    std::vector<float> texcoords={
        0.0f,0.0f,
        1.0f,0.0f,
        1.0f,1.0f,
        0.0f,1.0f};
    draw3DObject(cx,cy,cz,r,g,b,tex,vertices,indices,texcoords);
}
// туман
struct fog_params{
    bool enabled=false;
    float density=0.05f;
    float color[3]={0.7f,0.8f,0.9f};
    float start=5.0f;
    float end=30.0f;
};
static fog_params fog;
void enable_fog(float density,float r,float g,float b,float start,float end) {
    fog.enabled=true;
    fog.density=density;
    fog.color[0]=r;
    fog.color[1]=g;
    fog.color[2]=b;
    fog.start=start;
    fog.end=end;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE,GL_LINEAR);  
    glFogf(GL_FOG_START,fog.start);     
    glFogf(GL_FOG_END,fog.end);    
    glFogfv(GL_FOG_COLOR,fog.color);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER,0.1f);
}
void disable_fog(){
    fog.enabled=false;
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
}
void set_fog_density(float density){
    fog.density=density;
    if (fog.enabled) {
        fog.start=2.0f/density;
        fog.end=15.0f/density;
        glFogf(GL_FOG_START,fog.start);
        glFogf(GL_FOG_END,fog.end);
    }
}
void set_fog_range(float start,float end){
    fog.start=start;
    fog.end=end;
    if (fog.enabled) {
        glFogf(GL_FOG_START,fog.start);
        glFogf(GL_FOG_END,fog.end);
    }
}
void set_fog_color(float r,float g,float b){
    fog.color[0]=r;
    fog.color[1]=g;
    fog.color[2]=b;
    if (fog.enabled){
        glFogfv(GL_FOG_COLOR,fog.color);
    }
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
//              glb
glb_model::glb_model(float _x, float _y, float _z) : x(_x), y(_y), z(_z), rx(0), ry(0), rz(0), scale(1.0f) {}

bool glb_model::load(const std::string& filename) {
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    loaded = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    return loaded;
}

void glb_model::drawMesh(const tinygltf::Mesh& mesh) {
    for (const auto& primitive : mesh.primitives) {
        // Проверка на наличие атрибута POSITION
        if (primitive.attributes.find("POSITION") == primitive.attributes.end()) continue;

        const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

        glEnableClientState(GL_VERTEX_ARRAY);
        // Обрати внимание на model. buffers...
        glVertexPointer(3, GL_FLOAT, bufferView.byteStride, &buffer.data[bufferView.byteOffset + accessor.byteOffset]);

        if (primitive.indices >= 0) {
            const tinygltf::Accessor& indexAcc = model.accessors[primitive.indices];
            const tinygltf::BufferView& indexView = model.bufferViews[indexAcc.bufferView];
            glDrawElements(primitive.mode, indexAcc.count, indexAcc.componentType, 
                           &model.buffers[indexView.buffer].data[indexView.byteOffset + indexAcc.byteOffset]);
        }
        glDisableClientState(GL_VERTEX_ARRAY);
    }
}

void glb_model::drawNode(const tinygltf::Node& node) {
    glPushMatrix();
    // Здесь можно применить локальную трансформацию узла (T/R/S), если нужно
    if (node.mesh >= 0) drawMesh(model.meshes[node.mesh]);
    for (int childIdx : node.children) {
        drawNode(model.nodes[childIdx]);
    }
    glPopMatrix();
}

void glb_model::draw() {
    if (!loaded) return;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rx, 1, 0, 0); glRotatef(ry, 0, 1, 0); glRotatef(rz, 0, 0, 1);
    glScalef(scale, scale, scale);

    // Пример влияния тиков на анимацию (покачивание)
    float wave = sin(absolute_tick * 0.05f) * 2.0f;
    glTranslatef(0, wave * 0.1f, 0); 

    for (int nodeIdx : model.scenes[0].nodes) drawNode(model.nodes[nodeIdx]);
    glPopMatrix();
}