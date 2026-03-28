#include "avoextension.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
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
#include <iostream>
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
struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
static fog_params fog;

void enable_fog(float density, float r, float g, float b, float start, float end) {
    fog.enabled = true;
    fog.density = density;
    fog.color[0] = r;
    fog.color[1] = g;
    fog.color[2] = b;
    fog.start = start;
    fog.end = end;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, fog.start);
    glFogf(GL_FOG_END, fog.end);
    glFogfv(GL_FOG_COLOR, fog.color);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
}

void disable_fog() {
    fog.enabled = false;
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    // GL_BLEND не отключаем, т.к. он может быть нужен для других эффектов
}

void set_fog_density(float density) {
    fog.density = density;
    if (fog.enabled) {
        fog.start = 2.0f / density;
        fog.end = 15.0f / density;
        glFogf(GL_FOG_START, fog.start);
        glFogf(GL_FOG_END, fog.end);
    }
}

void set_fog_color(float r, float g, float b) {
    fog.color[0] = r;
    fog.color[1] = g;
    fog.color[2] = b;
    if (fog.enabled) {
        glFogfv(GL_FOG_COLOR, fog.color);
    }
}

void set_fog_range(float start, float end) {
    fog.start = start;
    fog.end = end;
    if (fog.enabled) {
        glFogf(GL_FOG_START, fog.start);
        glFogf(GL_FOG_END, fog.end);
    }
}
// панорама
struct sphere_panorama {
    bool enabled = false;
    GLuint texture = 0;
};
sphere_panorama sphere_sky;
static GLuint skybox_list = 0; // "Запись" геометрии сферы
void set_panorama(const char* path) {
    if (sphere_sky.enabled) remove_panorama();

    // Загружаем текстуру. 
    // ВАЖНО: Используем флаги SOIL, а не чистый OpenGL здесь
    sphere_sky.texture = SOIL_load_OGL_texture(
        path,
        SOIL_LOAD_AUTO,
        SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT
    );

    if (sphere_sky.texture == 0) {
        printf("ERROR: Could not load texture from %s. Reason: %s\n", path, SOIL_last_result());
        // Выведи текущую директорию, чтобы понять, где ищет программа
        system("pwd"); 
        return;
    }

    // Убираем шов на стыке сферы
    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);

    skybox_list = glGenLists(1);
    glNewList(skybox_list, GL_COMPILE);
        float radius = 180.0f; // Чуть меньше MAX_DIST (200), чтобы не обрезалось
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
void remove_panorama() {
    if (sphere_sky.enabled) {
        glDeleteTextures(1, &sphere_sky.texture);
        glDeleteLists(skybox_list, 1);
        sphere_sky.enabled = false;
    }
}
void draw_panorama(float camX, float camY, float camZ) {
    if (!sphere_sky.enabled || sphere_sky.texture == 0) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS); // Сохраняем все настройки (туман, свет, текстуры)
    
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);          // ВЫКЛЮЧАЕМ ТУМАН ДЛЯ НЕБА
    glDepthMask(GL_FALSE);
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glColor4f(1, 1, 1, 1);      // Сброс цвета в белый, чтобы текстура была яркой

    glPushMatrix();
    glTranslatef(camX, camY, camZ);
    glRotatef(90, 1, 0, 0); 
    
    // Используй именно тот ID, который создается в set_panorama
    glCallList(skybox_list); 

    glPopMatrix();
    
    glPopAttrib(); // ВОЗВРАЩАЕМ настройки (туман включится обратно сам)
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
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
glb_model::glb_model(float _x, float _y, float _z) 
    : x(_x), y(_y), z(_z), rx(0), ry(0), rz(0), scale(1.0f), loaded(false), scene(nullptr) {}

bool glb_model::load(const std::string& filename) {
    scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights | aiProcess_FlipUVs);
    if (!scene) return false;

    loadTextures();
    base_vertices.assign(scene->mNumMeshes, std::vector<aiVector3D>());
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        for (unsigned int v = 0; v < scene->mMeshes[i]->mNumVertices; v++)
            base_vertices[i].push_back(scene->mMeshes[i]->mVertices[v]);
    }
    loaded = true; return true;
}

void glb_model::readNodeHierarchy(float time, aiAnimation* anim, aiNode* node, const aiMatrix4x4& parent, std::map<std::string, aiMatrix4x4>& out) {
    aiMatrix4x4 nodeT = node->mTransformation;
    aiNodeAnim* channel = nullptr;
    for (unsigned int i = 0; i < anim->mNumChannels; i++) {
        if (std::string(anim->mChannels[i]->mNodeName.data) == node->mName.data) { channel = anim->mChannels[i]; break; }
    }

    if (channel) {
        // Позиция (Lerp)
        aiVector3D pos = channel->mPositionKeys[0].mValue;
        if (channel->mNumPositionKeys > 1) {
            unsigned int f = 0;
            for (unsigned int i = 0; i < channel->mNumPositionKeys - 1; i++) if (time < channel->mPositionKeys[i+1].mTime) { f = i; break; }
            float factor = (time - (float)channel->mPositionKeys[f].mTime) / (float)(channel->mPositionKeys[f+1].mTime - channel->mPositionKeys[f].mTime);
            pos = channel->mPositionKeys[f].mValue + (channel->mPositionKeys[f+1].mValue - channel->mPositionKeys[f].mValue) * factor;
        }
        // Вращение (Slerp)
        aiQuaternion rot = channel->mRotationKeys[0].mValue;
        if (channel->mNumRotationKeys > 1) {
            unsigned int f = 0;
            for (unsigned int i = 0; i < channel->mNumRotationKeys - 1; i++) if (time < channel->mRotationKeys[i+1].mTime) { f = i; break; }
            float factor = (time - (float)channel->mRotationKeys[f].mTime) / (float)(channel->mRotationKeys[f+1].mTime - channel->mRotationKeys[f].mTime);
            aiQuaternion::Interpolate(rot, channel->mRotationKeys[f].mValue, channel->mRotationKeys[f+1].mValue, factor);
        }
        nodeT = aiMatrix4x4(aiVector3D(1,1,1), rot, pos);
    }

    aiMatrix4x4 global = parent * nodeT;
    out[node->mName.data] = global;
    for (unsigned int i = 0; i < node->mNumChildren; i++) readNodeHierarchy(time, anim, node->mChildren[i], global, out);
}

void glb_model::updateAnimation(float time, int animIndex) {
    if (!loaded || !scene || animIndex >= (int)scene->mNumAnimations) return;
    aiAnimation* anim = scene->mAnimations[animIndex];
    float ticks = fmod(time * (float)(anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0f), (float)anim->mDuration);

    std::map<std::string, aiMatrix4x4> transforms;
    readNodeHierarchy(ticks, anim, scene->mRootNode, aiMatrix4x4(), transforms);
    aiMatrix4x4 rootInv = scene->mRootNode->mTransformation; rootInv.Inverse();

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        if (!mesh->HasBones()) continue;

        // Обнуляем вершины перед расчетом
        std::vector<aiVector3D> next_pos(mesh->mNumVertices, aiVector3D(0,0,0));
        std::vector<float> weight_check(mesh->mNumVertices, 0.0f);

        for (unsigned int b = 0; b < mesh->mNumBones; b++) {
            aiBone* bone = mesh->mBones[b];
            if (transforms.count(bone->mName.data)) {
                aiMatrix4x4 m = rootInv * transforms[bone->mName.data] * bone->mOffsetMatrix;
                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    unsigned int vId = bone->mWeights[w].mVertexId;
                    next_pos[vId] += (m * base_vertices[i][vId]) * bone->mWeights[w].mWeight;
                    weight_check[vId] += bone->mWeights[w].mWeight;
                }
            }
        }
        // Если вершина не привязана к костям, возвращаем оригинал, чтобы она не исчезала
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            mesh->mVertices[v] = (weight_check[v] > 0.001f) ? next_pos[v] : base_vertices[i][v];
        }
    }
}

void glb_model::draw() {
    if (!loaded) return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING); // Чтобы модель была видна без настройки ламп
    glColor4f(1, 1, 1, 1);   // Сброс цвета

    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rx, 1, 0, 0); // Вращение X
    glRotatef(ry, 0, 1, 0); // Вращение Y
    glRotatef(rz, 0, 0, 1); // Вращение Z
    glRotatef(-90, 1, 0, 0); // Поворот "стоя"
    glScalef(scale, scale, scale);

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
        aiString p;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &p) == AI_SUCCESS && p.data[0] == '*') {
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, embedded_textures[atoi(&p.data[1])]);
        }
        glBegin(GL_TRIANGLES);
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            for (unsigned int v = 0; v < 3; v++) {
                unsigned int idx = mesh->mFaces[f].mIndices[v];
                if (mesh->HasTextureCoords(0)) glTexCoord2f(mesh->mTextureCoords[0][idx].x, mesh->mTextureCoords[0][idx].y);
                glVertex3fv(&mesh->mVertices[idx].x);
            }
        }
        glEnd(); glDisable(GL_TEXTURE_2D);
    }
    glPopMatrix();
    glPopAttrib();
}

void glb_model::loadTextures() {
    for (unsigned int i = 0; i < scene->mNumTextures; i++) {
        int w, h, c;
        unsigned char* d = stbi_load_from_memory((unsigned char*)scene->mTextures[i]->pcData, scene->mTextures[i]->mWidth, &w, &h, &c, 4);
        if (d) {
            GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            embedded_textures[i] = t; stbi_image_free(d);
        }
    }
}