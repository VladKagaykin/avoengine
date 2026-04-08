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
#include <GL/glut.h>
#include <SOIL/SOIL.h>
#include <string>
#include <iostream>
#include <map>
#include <unordered_map>
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
// поставить иконку
#include <GL/freeglut_ext.h>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <GL/glx.h>
#endif

void set_icon(const char* path){
    int width, height, channels;
    unsigned char* image=SOIL_load_image(path, &width, &height, &channels, SOIL_LOAD_RGBA);
    if (!image) {
        return;
    }
#ifdef _WIN32
    HWND hwnd = (HWND)glutGetWindowData();
    if (!hwnd) hwnd = GetActiveWindow();
    if (hwnd) {
        HICON hIcon = CreateIcon(GetModuleHandle(NULL), width, height, 1, 32, NULL, image);
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
#else
    Display* display = glXGetCurrentDisplay();
    Window win = glXGetCurrentDrawable();
    if (display && win) {
        std::vector<unsigned long> icon_data;
        icon_data.push_back(width);
        icon_data.push_back(height);
        for (int i = 0; i < width * height; i++) {
            unsigned char r = image[i * 4];
            unsigned char g = image[i * 4 + 1];
            unsigned char b = image[i * 4 + 2];
            unsigned char a = image[i * 4 + 3];
            icon_data.push_back((a << 24) | (r << 16) | (g << 8) | b);
        }
        Atom net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
        Atom cardinal = XInternAtom(display, "CARDINAL", False);
        XChangeProperty(display, win, net_wm_icon, cardinal, 32,PropModeReplace, (unsigned char*)icon_data.data(), icon_data.size());
        XFlush(display);
    }
#endif
    SOIL_free_image_data(image);
}
// считывание клавиш клавиатуры
bool keys[256]={},skeys[512]={};
void keyboard_down(unsigned char key,int,int){keys[key]=true;}
void keyboard_up(unsigned char key,int,int){keys[key]=false;}
void special_up(int key,int,int){skeys[key]=false;}
void special_down(int key,int,int){skeys[key]=true;}
// считывание мыши
std::map<std::string, bool> mouse;
int mouse_x = 0, mouse_y = 0;
bool mouse_captured = false;
static int capture_center_x = 400, capture_center_y = 300;

void init_mouse(){
    mouse["left"] = false;
    mouse["right"] = false;
    mouse["middle"] = false;
    mouse["left_click"] = false;
    mouse["right_click"] = false;
    mouse["middle_click"] = false;
    mouse["wheel_up"] = false;
    mouse["wheel_down"] = false;
    glutMouseFunc([](int button, int state, int x, int y) {
        std::string btn = (button == GLUT_LEFT_BUTTON) ? "left" : 
                          (button == GLUT_MIDDLE_BUTTON) ? "middle" : "right";
        mouse[btn] = (state == GLUT_DOWN);
        mouse[btn + "_click"] = (state == GLUT_DOWN);
        mouse_x = x; mouse_y = y;
    });
    
    #ifdef __FREEGLUT_EXT_H__
    glutMouseWheelFunc([](int wheel, int dir, int x, int y) {
        if (dir > 0) mouse["wheel_up"] = true;
        else if (dir < 0) mouse["wheel_down"] = true;
    });
    #endif
    glutMotionFunc([](int x, int y) { mouse_x = x; mouse_y = y; });
    glutPassiveMotionFunc([](int x, int y) { mouse_x = x; mouse_y = y; });
}

void set_mouse_capture(bool capture){
    if (capture && !mouse_captured) {
        mouse_captured = true;
        capture_center_x = glutGet(GLUT_WINDOW_WIDTH) / 2;
        capture_center_y = glutGet(GLUT_WINDOW_HEIGHT) / 2;
        glutSetCursor(GLUT_CURSOR_NONE);
        glutWarpPointer(capture_center_x, capture_center_y);
        glutMotionFunc([](int x, int y) {
            mouse_x += x - capture_center_x;
            mouse_y += y - capture_center_y;
            glutWarpPointer(capture_center_x, capture_center_y);
        });
        glutPassiveMotionFunc([](int x, int y) {
            mouse_x += x - capture_center_x;
            mouse_y += y - capture_center_y;
            glutWarpPointer(capture_center_x, capture_center_y);
        });
    } 
    else if (!capture && mouse_captured) {
        mouse_captured = false;
        glutSetCursor(GLUT_CURSOR_INHERIT);
        glutMotionFunc([](int x, int y) { mouse_x = x; mouse_y = y; });
        glutPassiveMotionFunc([](int x, int y) { mouse_x = x; mouse_y = y; });
    }
}

void update_mouse(){
    mouse["left_click"] = false;
    mouse["right_click"] = false;
    mouse["middle_click"] = false;
    mouse["wheel_up"] = false;
    mouse["wheel_down"] = false;
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
// туман
struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
static fog_params fog;

void enable_fog(float density, float r, float g, float b, float start, float end){
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

void disable_fog(){
    fog.enabled = false;
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
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
//              glb
glb_model::glb_model(float _x,float _y,float _z)
    : x(_x),y(_y),z(_z) {}

glb_model::~glb_model() {
    for (auto& m : meshes) {
        if (m.pos_vbo) glDeleteBuffers(1, &m.pos_vbo);
        if (m.uv_vbo)  glDeleteBuffers(1, &m.uv_vbo);
        if (m.ibo)     glDeleteBuffers(1, &m.ibo);
        if (m.norm_vbo) glDeleteBuffers(1, &m.norm_vbo);
    }
    for (auto& [i, t] : emb_tex)
        glDeleteTextures(1, &t);
}
bool glb_model::load(const std::string& path) {
    scene = importer.ReadFile(path,
        aiProcess_Triangulate        | 
        aiProcess_JoinIdenticalVertices|
        aiProcess_LimitBoneWeights   | 
        aiProcess_FlipUVs            | 
        aiProcess_GenSmoothNormals); 

    if (!scene) {
        fprintf(stderr, "glb_model::load — %s\n", importer.GetErrorString());
        return false;
    }
    loadTextures();
    buildMeshes();
    buildChanCache();
    loaded = true;
    return true;
}

void glb_model::loadTextures() {
    for (unsigned i = 0; i < scene->mNumTextures; ++i) {
        const aiTexture* src = scene->mTextures[i];
        int w, h, c;
        unsigned char* d = stbi_load_from_memory(
            reinterpret_cast<unsigned char*>(src->pcData),
            (int)src->mWidth, &w, &h, &c, 4);
        if (!d) continue;

        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, d);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        emb_tex[i] = t;
        stbi_image_free(d);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void glb_model::buildMeshes() {
    meshes.resize(scene->mNumMeshes);

    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        aiMesh*  am = scene->mMeshes[mi];
        GPUMesh& m  = meshes[mi];

        m.vert_count = (int)am->mNumVertices;
        m.skinned = am->HasBones();
        m.has_uv = am->HasTextureCoords(0);

        m.rest.resize(m.vert_count * 3);
        for (int v = 0; v < m.vert_count; ++v) {
            m.rest[v*3+0] = am->mVertices[v].x;
            m.rest[v*3+1] = am->mVertices[v].y;
            m.rest[v*3+2] = am->mVertices[v].z;
        }
        m.skin = m.rest;
        std::vector<float> uvs;
        if (m.has_uv){
            uvs.resize(m.vert_count * 2);
            for (int v = 0; v < m.vert_count; ++v) {
                uvs[v*2+0] = am->mTextureCoords[0][v].x;
                uvs[v*2+1] = am->mTextureCoords[0][v].y;
            }
        }
        std::vector<float> norms;
        if (am->HasNormals()) {
            norms.resize(m.vert_count * 3);
            for (int v = 0; v < m.vert_count; ++v) {
                norms[v*3+0] = am->mNormals[v].x;
                norms[v*3+1] = am->mNormals[v].y;
                norms[v*3+2] = am->mNormals[v].z;
            }
        }
        if (!norms.empty()) {
            glGenBuffers(1, &m.norm_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, m.norm_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                        (GLsizeiptr)(norms.size() * sizeof(float)),
                        norms.data(), GL_STATIC_DRAW);
        }
        if (am->HasNormals()) {
            norms.resize(m.vert_count * 3);
            for (int v = 0; v < m.vert_count; ++v) {
                norms[v*3+0] = am->mNormals[v].x;
                norms[v*3+1] = am->mNormals[v].y;
                norms[v*3+2] = am->mNormals[v].z;
            }
            m.rest_normals = norms;
            m.skin_normals = norms;  
        }

        std::vector<unsigned int> idx;
        idx.reserve(am->mNumFaces * 3);
        for (unsigned f = 0; f < am->mNumFaces; ++f)
            for (unsigned k = 0; k < 3; ++k)
                idx.push_back(am->mFaces[f].mIndices[k]);
        m.idx_count = (int)idx.size();

        if (m.skinned) {
            m.slots.resize(m.vert_count);  
            m.offset.resize(am->mNumBones);
            m.bname.resize(am->mNumBones);

            for (unsigned b = 0; b < am->mNumBones; ++b) {
                aiBone* bone  = am->mBones[b];
                m.offset[b]  = bone->mOffsetMatrix;
                m.bname[b]   = bone->mName.data;
                for (unsigned w = 0; w < bone->mNumWeights; ++w)
                    m.slots[bone->mWeights[w].mVertexId]
                        .push((int)b, bone->mWeights[w].mWeight);
            }
        }

        aiMaterial* mat = scene->mMaterials[am->mMaterialIndex];
        aiString p;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &p) == AI_SUCCESS
                && p.data[0] == '*') {
            int ti = atoi(&p.data[1]);
            if (emb_tex.count(ti)) m.tex = emb_tex[ti];
        }
        glGenBuffers(1, &m.pos_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m.pos_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(m.skin.size() * sizeof(float)),
                     m.skin.data(),
                     m.skinned ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

        if (m.has_uv) {
            glGenBuffers(1, &m.uv_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, m.uv_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(uvs.size() * sizeof(float)),
                         uvs.data(), GL_STATIC_DRAW);
        }

        glGenBuffers(1, &m.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     (GLsizeiptr)(idx.size() * sizeof(unsigned int)),
                     idx.data(), GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void glb_model::buildChanCache() {
    chan_cache.resize(scene->mNumAnimations);
    for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
        aiAnimation* anim = scene->mAnimations[a];
        chan_cache[a].reserve(anim->mNumChannels);
        for (unsigned c = 0; c < anim->mNumChannels; ++c)
            chan_cache[a][anim->mChannels[c]->mNodeName.data] = anim->mChannels[c];
    }
}
aiVector3D glb_model::interpPos(float t, const aiNodeAnim* ch) {
    if (ch->mNumPositionKeys == 1)
        return ch->mPositionKeys[0].mValue;
    unsigned lo = 0, hi = ch->mNumPositionKeys - 2;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if ((float)ch->mPositionKeys[mid+1].mTime <= t) lo = mid+1;
        else                                             hi = mid;
    }
    float dt = (float)(ch->mPositionKeys[lo+1].mTime
                     - ch->mPositionKeys[lo].mTime);
    float f  = dt > 1e-6f
             ? (t - (float)ch->mPositionKeys[lo].mTime) / dt
             : 0.f;
    f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
    const aiVector3D& a = ch->mPositionKeys[lo].mValue;
    const aiVector3D& b = ch->mPositionKeys[lo+1].mValue;
    return a + (b - a) * f;
}

aiQuaternion glb_model::interpRot(float t, const aiNodeAnim* ch) {
    if (ch->mNumRotationKeys == 1)
        return ch->mRotationKeys[0].mValue;

    unsigned lo = 0, hi = ch->mNumRotationKeys - 2;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if ((float)ch->mRotationKeys[mid+1].mTime <= t) lo = mid+1;
        else                                             hi = mid;
    }

    float dt = (float)(ch->mRotationKeys[lo+1].mTime
                     - ch->mRotationKeys[lo].mTime);
    float f  = dt > 1e-6f
             ? (t - (float)ch->mRotationKeys[lo].mTime) / dt
             : 0.f;
    f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);

    aiQuaternion result;
    aiQuaternion::Interpolate(result,
        ch->mRotationKeys[lo].mValue,
        ch->mRotationKeys[lo+1].mValue, f);
    return result.Normalize();
}
void glb_model::traverseNode(float ticks, int animIdx,const aiNode* node, const aiMatrix4x4& parent,std::unordered_map<std::string,aiMatrix4x4>& globals){
    aiMatrix4x4 local = node->mTransformation;

    auto it = chan_cache[animIdx].find(node->mName.data);
    if (it != chan_cache[animIdx].end()) {
        const aiNodeAnim* ch = it->second;
        aiVector3D   pos = interpPos(ticks, ch);
        aiQuaternion rot = interpRot(ticks, ch);
        local = aiMatrix4x4(aiVector3D(1,1,1), rot, pos);
    }

    globals[node->mName.data] = parent * local;

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        traverseNode(ticks, animIdx, node->mChildren[i],
                     globals[node->mName.data], globals);
}
void glb_model::applySkinning(GPUMesh& m,const std::unordered_map<std::string,aiMatrix4x4>& globals,const aiMatrix4x4& rootInv){
    const int B = (int)m.offset.size();
    std::vector<aiMatrix4x4> final_mat(B);
    for (int b = 0; b < B; ++b) {
        auto it = globals.find(m.bname[b]);
        final_mat[b] = (it != globals.end())
                     ? rootInv * it->second * m.offset[b]
                     : aiMatrix4x4();
    }
    const int N = m.vert_count;
    const float* src_pos = m.rest.data();
    float*       dst_pos = m.skin.data();

    for (int v = 0; v < N; ++v) {
        aiVector3D p(src_pos[v*3], src_pos[v*3+1], src_pos[v*3+2]);
        aiVector3D o(0.f, 0.f, 0.f);
        const BoneSlot& s = m.slots[v];
        for (int i = 0; i < 4; ++i) {
            if (s.w[i] > 0.f) {
                o += final_mat[s.id[i]] * p * s.w[i];
            }
        }
        dst_pos[v*3+0] = o.x;
        dst_pos[v*3+1] = o.y;
        dst_pos[v*3+2] = o.z;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m.pos_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(m.skin.size() * sizeof(float)),
                    dst_pos);

    if (!m.rest_normals.empty()) {
        if (m.skin_normals.size() != m.rest_normals.size())
            m.skin_normals = m.rest_normals;

        const float* src_nrm = m.rest_normals.data();
        float*       dst_nrm = m.skin_normals.data();

        for (int v = 0; v < N; ++v) {
            aiVector3D n(src_nrm[v*3], src_nrm[v*3+1], src_nrm[v*3+2]);
            aiVector3D on(0.f, 0.f, 0.f);
            const BoneSlot& s = m.slots[v];
            for (int i = 0; i < 4; ++i) {
                if (s.w[i] > 0.f) {
                    const aiMatrix4x4& mat = final_mat[s.id[i]];
                    aiMatrix3x3 rot3(
                        mat.a1, mat.a2, mat.a3,
                        mat.b1, mat.b2, mat.b3,
                        mat.c1, mat.c2, mat.c3
                    );
                    aiVector3D tn = rot3 * n;
                    on = on + tn * s.w[i];
                }
            }
            on.Normalize();
            dst_nrm[v*3+0] = on.x;
            dst_nrm[v*3+1] = on.y;
            dst_nrm[v*3+2] = on.z;
        }

        glBindBuffer(GL_ARRAY_BUFFER, m.norm_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(m.skin_normals.size() * sizeof(float)),
                        dst_nrm);
    }
}
void glb_model::updateAnimation(float time, int animIndex) {
    if (!loaded || !scene || animIndex >= (int)scene->mNumAnimations) return;

    aiAnimation* anim = scene->mAnimations[animIndex];
    double tps   = anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 25.0;
    float  ticks = fmodf(time * (float)tps, (float)anim->mDuration);

    std::unordered_map<std::string, aiMatrix4x4> globals;
    globals.reserve(128); 
    traverseNode(ticks, animIndex, scene->mRootNode, aiMatrix4x4(), globals);

    aiMatrix4x4 rootInv = scene->mRootNode->mTransformation;
    rootInv.Inverse();

    for (auto& m : meshes)
        if (m.skinned)
            applySkinning(m, globals, rootInv);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void glb_model::draw() {
    if (!loaded) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glColor4f(1.f, 1.f, 1.f, 1.f);

    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rx,  1, 0, 0);
    glRotatef(ry,  0, 1, 0);
    glRotatef(rz,  0, 0, 1);
    glRotatef(-90, 1, 0, 0);
    glScalef(scale, scale, scale);

    glEnableClientState(GL_VERTEX_ARRAY);

    for (const auto& m : meshes) {
        if (m.tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m.tex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBindBuffer(GL_ARRAY_BUFFER, m.pos_vbo);
        glVertexPointer(3, GL_FLOAT, 0, nullptr);

        if (m.norm_vbo) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glBindBuffer(GL_ARRAY_BUFFER, m.norm_vbo);
            glNormalPointer(GL_FLOAT, 0, nullptr);
        }

        if (m.has_uv && m.tex) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glBindBuffer(GL_ARRAY_BUFFER, m.uv_vbo);
            glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glDrawElements(GL_TRIANGLES, m.idx_count, GL_UNSIGNED_INT, nullptr);

        if (m.norm_vbo) glDisableClientState(GL_NORMAL_ARRAY);
        if (m.has_uv && m.tex) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glPopMatrix();
    glPopAttrib();
}