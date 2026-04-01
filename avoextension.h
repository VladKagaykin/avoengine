#pragma once
#ifndef AVOEXTENSION_H
#define AVOEXTENSION_H
#include <vector>
#include <string>
#include <GL/glew.h>
#include <vector>
#include <GL/gl.h>
#include "avoengine.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>
#include <unordered_map>

extern int tick;
extern const int max_tick;
extern int absolute_tick;

void timer();
void render_loop(int);
void init_tick_system();
void set_icon(const char* path);
extern bool keys[256];
extern bool skeys[512];
void keyboard_down(unsigned char key,int,int);
void keyboard_up(unsigned char key,int,int);
void special_up(int key,int,int);
void special_down(int key,int,int);

void plane(float cx, float cy, float cz, double r, double g, double b,
          const char* tex, const std::vector<float>& vertices);

void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);
void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);
void delay_text(const char* text, float x, float y, void* font,
                float r, float g, float b, float a, int ticks, bool loop = false);
void disappearing_text(const char* text, float x, float y, void* font,
                       float r, float g, float b, float a, int ticks, bool loop = false);

void play_white_noise_3d(float x, float y, float z, float volume);
class glb_model {
public:
    float x=0,y=0,z=0;
    float rx=0,ry=0,rz=0;
    float scale=1.0f;
    bool  loaded=false;
 
    glb_model(float _x=0,float _y=0,float _z=0);
    ~glb_model();
 
    bool load(const std::string& path);
    void updateAnimation(float time,int animIndex=0);
    void draw();
 
    void setRotation(float _rx,float _ry,float _rz){rx=_rx;ry=_ry;rz=_rz;}
    void setScale(float s){scale=s;}
 
private:
    // ---- влияние костей на вершину (максимум 4, как в aiProcess_LimitBoneWeights) ----
    struct BoneSlot {
        int   id[4]={0,0,0,0};
        float w[4] ={0.f,0.f,0.f,0.f};
        void push(int bone,float weight){
            for(int i=0;i<4;++i)
                if(w[i]==0.f){id[i]=bone;w[i]=weight;return;}
        }
    };
 
    // ---- данные одного меша на GPU и CPU ----
    struct GPUMesh {
        // GPU
        GLuint pos_vbo=0;   // xyz-позиции (DYNAMIC для скинованных, STATIC иначе)
        GLuint uv_vbo =0;   // UV-координаты (STATIC)
        GLuint ibo    =0;   // индексы треугольников (STATIC)
        GLuint tex    =0;   // id текстуры
 
        int idx_count =0;
        int vert_count=0;
        bool skinned  =false;
        bool has_uv   =false;
 
        // CPU-скиннинг (только для скинованных мешей)
        std::vector<float>      rest;   // оригинальные xyz (3 на вершину)
        std::vector<float>      skin;   // текущие xyz после скиннинга
        std::vector<BoneSlot>   slots;  // влияние костей на каждую вершину
        std::vector<aiMatrix4x4>offset; // offset-матрица каждой кости
        std::vector<std::string>bname;  // имя ноды для каждой кости
    };
 
    Assimp::Importer importer;
    const aiScene*   scene=nullptr;
 
    std::vector<GPUMesh>        meshes;
    std::map<int,GLuint>        emb_tex;
 
    // chan_cache[anim_idx][node_name] = указатель на aiNodeAnim*
    // строится один раз при load(), избавляет от O(n)-поиска на каждом кадре
    std::vector<std::unordered_map<std::string,const aiNodeAnim*>> chan_cache;
 
    // ---- внутренние методы ----
    void loadTextures();
    void buildMeshes();
    void buildChanCache();
 
    // рекурсивный обход дерева нод, заполняет глобальные трансформации
    void traverseNode(float ticks,int animIdx,
                      const aiNode* node,const aiMatrix4x4& parent,
                      std::unordered_map<std::string,aiMatrix4x4>& globals);
 
    // применяет скиннинг к одному мешу и заливает pos_vbo через glBufferSubData
    void applySkinning(GPUMesh& m,
                       const std::unordered_map<std::string,aiMatrix4x4>& globals,
                       const aiMatrix4x4& rootInv);
 
    // бинарный поиск ключевых кадров + линейная/сферическая интерполяция
    static aiVector3D   interpPos(float t,const aiNodeAnim* ch);
    static aiQuaternion interpRot(float t,const aiNodeAnim* ch);
};
#endif