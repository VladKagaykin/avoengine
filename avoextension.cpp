#define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include "miniaudio.h"
#include "avoengine.h"
#include <vector>
#include <cstring>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <GL/glut.h>
#include "avoextension.h"
#include <SOIL/SOIL.h>
#include <string>
#include <map>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

using namespace std;

// Хранилище всех загруженных моделей
static std::vector<GLBModel> loaded_models;

// Кэш текстур: ключ — "путь_к_файлу_индекс_текстуры", значение — OpenGL ID
static std::map<std::string, GLuint> glb_tex_cache;

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

// импорт glb моделей
// Загружает .glb файл, создает VAO/VBO/EBO и загружает текстуры в GPU
int load_glb_model(const char* filename) {
    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    if (!loader.LoadBinaryFromFile(&gltf_model, &err, &warn, filename)) return -1;
    
    GLBModel model;
    model.name = filename;
    std::vector<GLuint> textures;

    // 1. ЗАГРУЗКА ТЕКСТУР (как в том коде)
    for (size_t i = 0; i < gltf_model.images.size(); i++) {
        tinygltf::Image &image = gltf_model.images[i];
        GLuint tid;
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        
        // В GLB текстуры часто в RGBA, но если 3 компонента — ставим GL_RGB
        GLenum format = GL_RGBA;
        if (image.component == 3) format = GL_RGB;
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, &image.image[0]);
        
        // Настройки из "того самого" примера для стабильной картинки
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);

        textures.push_back(tid);
        glb_tex_cache[std::string(filename) + "_" + std::to_string(i)] = tid;
    }

    // 2. РАЗБОР МЕШЕЙ С УЧЕТОМ МАТЕРИАЛОВ
    for (auto& mesh : gltf_model.meshes) {
        for (auto& primitive : mesh.primitives) {
            GLBPrimitive prim;
            glGenVertexArrays(1, &prim.vao);
            glBindVertexArray(prim.vao);

            // Позиции (Attribute 0)
            const tinygltf::Accessor& posAcc = gltf_model.accessors[primitive.attributes["POSITION"]];
            const tinygltf::BufferView& posView = gltf_model.bufferViews[posAcc.bufferView];
            glBindBuffer(GL_ARRAY_BUFFER, (glGenBuffers(1, &prim.vbo), prim.vbo));
            glBufferData(GL_ARRAY_BUFFER, posView.byteLength, &gltf_model.buffers[posView.buffer].data[posView.byteOffset], GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(0);

            // UV (Attribute 1) - Берем TEXCOORD_0
            if (primitive.attributes.count("NORMAL")) {
                const tinygltf::Accessor& normAcc = gltf_model.accessors[primitive.attributes["NORMAL"]];
                const tinygltf::BufferView& normView = gltf_model.bufferViews[normAcc.bufferView];
                glGenBuffers(1, &prim.normVbo);
                glBindBuffer(GL_ARRAY_BUFFER, prim.normVbo);
                glBufferData(GL_ARRAY_BUFFER, normView.byteLength, 
                             &gltf_model.buffers[normView.buffer].data[normView.byteOffset], GL_STATIC_DRAW);
            }

            // 2. ЗАГРУЗКА UV-КООРДИНАТ (Чтобы была текстура)
            if (primitive.attributes.count("TEXCOORD_0")) {
                const tinygltf::Accessor& texAcc = gltf_model.accessors[primitive.attributes["TEXCOORD_0"]];
                const tinygltf::BufferView& texView = gltf_model.bufferViews[texAcc.bufferView];
                glGenBuffers(1, &prim.texVbo);
                glBindBuffer(GL_ARRAY_BUFFER, prim.texVbo);
                glBufferData(GL_ARRAY_BUFFER, texView.byteLength, 
                             &gltf_model.buffers[texView.buffer].data[texView.byteOffset], GL_STATIC_DRAW);
            }

            // Индексы
            const tinygltf::Accessor& indAcc = gltf_model.accessors[primitive.indices];
            const tinygltf::BufferView& indView = gltf_model.bufferViews[indAcc.bufferView];
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (glGenBuffers(1, &prim.ebo), prim.ebo));
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indView.byteLength, &gltf_model.buffers[indView.buffer].data[indView.byteOffset], GL_STATIC_DRAW);
            
            prim.indexCount = (int)indAcc.count;
            prim.indexType = indAcc.componentType;
            // СВЯЗКА ТЕКСТУРЫ (подход из примера)
            prim.textureId = 0;
            if (primitive.material >= 0) {
                const auto& mat = gltf_model.materials[primitive.material];
                int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIdx >= 0) {
                    // В GLTF индекс текстуры ссылается на объект Texture, который ссылается на Image
                    int imageIdx = gltf_model.textures[texIdx].source;
                    prim.textureId = textures[imageIdx];
                }
            }
            model.primitives.push_back(prim);
        }
    }
    glBindVertexArray(0);
    loaded_models.push_back(model);
    return (int)loaded_models.size() - 1;
}

void draw_glb_model(int model_id, float x, float y, float z, float scale, float rx, float ry, float rz) {
    if (model_id < 0 || model_id >= (int)loaded_models.size()) return;
    GLBModel& m = loaded_models[model_id];

    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rx, 1, 0, 0);
    glRotatef(ry, 0, 1, 0);
    glRotatef(rz, 0, 0, 1);
    glScalef(scale, scale, scale);

    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL); 
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Белый цвет-фильтр для текстуры

    for (const auto& p : m.primitives) {
        // 1. ТЕКСТУРА
        if (p.textureId != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, p.textureId);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        // 2. СВЯЗКА БУФЕРОВ (Compatibility Profile)
        // ВАЖНО: Мы не используем glVertexAttribPointer(0,1,2), так как нет шейдеров.
        // Используем стандартные указатели OpenGL 1.1 для работы с VBO.
        
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glVertexPointer(3, GL_FLOAT, 0, 0);
        glEnableClientState(GL_VERTEX_ARRAY);

        if (p.texVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.texVbo);
            glTexCoordPointer(2, GL_FLOAT, 0, 0);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }

        // ВОТ ТУТ РЕШАЕТСЯ ПРОБЛЕМА ЧЕРНОТЫ:
        // Нужно найти VBO с нормалями. Если его нет в структуре p, 
        // освещение работать не будет.
        // Допустим, мы добавили p.normVbo в загрузчик (см. ниже)
        if (p.normVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.normVbo);
            glNormalPointer(GL_FLOAT, 0, 0);
            glEnableClientState(GL_NORMAL_ARRAY);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
        
        // 3. ОТРИСОВКА
        // Проверь в загрузчике: если типы индексов в GLB — 5123, это GL_UNSIGNED_SHORT
        glDrawElements(GL_TRIANGLES, p.indexCount, p.indexType, 0);

        // Чистим состояния
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    
    glPopMatrix();
}

// Очистка конкретной модели (GPU буферы)
void unload_glb_model(int id) {
    if (id < 0 || id >= (int)loaded_models.size()) return;
    for (auto& p : loaded_models[id].primitives) {
        glDeleteVertexArrays(1, &p.vao);
        glDeleteBuffers(1, &p.vbo);
        glDeleteBuffers(1, &p.ebo);
        if (p.texVbo) glDeleteBuffers(1, &p.texVbo);
    }
    loaded_models[id].primitives.clear();
}

// Полная очистка всех текстур из памяти видеокарты
void clear_embedded_texture_cache() {
    for (auto const& [name, id] : glb_tex_cache) {
        if (id != 0) glDeleteTextures(1, &id);
    }
    glb_tex_cache.clear();
}