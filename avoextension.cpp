#include "avoextension.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include <cstring>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------
// Глобальный контейнер моделей (для обёрток)
// ---------------------------------------------------------------------
static std::vector<GLBModel> g_models;

// ---------------------------------------------------------------------
// Реализация класса GLBModel
// ---------------------------------------------------------------------

GLBModel::GLBModel() = default;
GLBModel::~GLBModel() { unload(); }

bool GLBModel::load(const char* filename) {
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    if (!loader.LoadBinaryFromFile(&gltf, &err, &warn, filename)) {
        fprintf(stderr, "Failed to load GLB: %s\n", err.c_str());
        return false;
    }

    // Очистка предыдущих данных
    primitives.clear();
    nodes.clear();
    animations.clear();
    textures.clear();

    // ==================== 1. Текстуры ====================
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        tinygltf::Image& img = gltf.images[i];
        GLuint tid;
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        GLenum format = (img.component == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0,
                     format, GL_UNSIGNED_BYTE, img.image.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        textures.push_back(tid);
    }

    // ==================== 2. Узлы ====================
    nodes.resize(gltf.nodes.size());
    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        tinygltf::Node& src = gltf.nodes[i];
        Node& dst = nodes[i];
        dst.children = src.children;

        if (!src.matrix.empty()) {
            for (int j = 0; j < 16; ++j)
                dst.localMat[j] = (float)src.matrix[j];
        } else {
            dst.trans[0] = (src.translation.size() == 3) ? src.translation[0] : 0;
            dst.trans[1] = (src.translation.size() == 3) ? src.translation[1] : 0;
            dst.trans[2] = (src.translation.size() == 3) ? src.translation[2] : 0;
            dst.rot[0] = (src.rotation.size() == 4) ? src.rotation[0] : 0;
            dst.rot[1] = (src.rotation.size() == 4) ? src.rotation[1] : 0;
            dst.rot[2] = (src.rotation.size() == 4) ? src.rotation[2] : 0;
            dst.rot[3] = (src.rotation.size() == 4) ? src.rotation[3] : 1;
            dst.scale[0] = (src.scale.size() == 3) ? src.scale[0] : 1;
            dst.scale[1] = (src.scale.size() == 3) ? src.scale[1] : 1;
            dst.scale[2] = (src.scale.size() == 3) ? src.scale[2] : 1;
            trsToMatrix(dst.localMat, dst.trans, dst.rot, dst.scale);
        }
        dst.dirty = false;
    }

    // ==================== 3. Сбор примитивов по узлам ====================
    struct TempPrim { int nodeIdx; tinygltf::Primitive prim; };
    std::vector<TempPrim> tempPrims;
    for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx) {
        int nodeIdx = -1;
        for (size_t n = 0; n < gltf.nodes.size(); ++n) {
            if (gltf.nodes[n].mesh == (int)meshIdx) {
                nodeIdx = n;
                break;
            }
        }
        if (nodeIdx == -1) continue; // меш не привязан к узлу
        for (const tinygltf::Primitive& p : gltf.meshes[meshIdx].primitives) {
            tempPrims.push_back({nodeIdx, p});
        }
    }

    // ==================== 4. Создание OpenGL буферов ====================
    for (const TempPrim& tp : tempPrims) {
        Primitive p;
        p.nodeIdx = tp.nodeIdx;
        const tinygltf::Primitive& prim = tp.prim;

        // Позиции
        const tinygltf::Accessor& posAcc = gltf.accessors[prim.attributes.at("POSITION")];
        const tinygltf::BufferView& posView = gltf.bufferViews[posAcc.bufferView];
        glGenBuffers(1, &p.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glBufferData(GL_ARRAY_BUFFER, posView.byteLength,
                     &gltf.buffers[posView.buffer].data[posView.byteOffset], GL_STATIC_DRAW);

        // Нормали
        if (prim.attributes.count("NORMAL")) {
            const tinygltf::Accessor& normAcc = gltf.accessors[prim.attributes.at("NORMAL")];
            const tinygltf::BufferView& normView = gltf.bufferViews[normAcc.bufferView];
            glGenBuffers(1, &p.normVbo);
            glBindBuffer(GL_ARRAY_BUFFER, p.normVbo);
            glBufferData(GL_ARRAY_BUFFER, normView.byteLength,
                         &gltf.buffers[normView.buffer].data[normView.byteOffset], GL_STATIC_DRAW);
        }

        // UV
        if (prim.attributes.count("TEXCOORD_0")) {
            const tinygltf::Accessor& texAcc = gltf.accessors[prim.attributes.at("TEXCOORD_0")];
            const tinygltf::BufferView& texView = gltf.bufferViews[texAcc.bufferView];
            glGenBuffers(1, &p.texVbo);
            glBindBuffer(GL_ARRAY_BUFFER, p.texVbo);
            glBufferData(GL_ARRAY_BUFFER, texView.byteLength,
                         &gltf.buffers[texView.buffer].data[texView.byteOffset], GL_STATIC_DRAW);
        }

        // Индексы
        const tinygltf::Accessor& indAcc = gltf.accessors[prim.indices];
        const tinygltf::BufferView& indView = gltf.bufferViews[indAcc.bufferView];
        glGenBuffers(1, &p.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indView.byteLength,
                     &gltf.buffers[indView.buffer].data[indView.byteOffset], GL_STATIC_DRAW);
        p.indexCount = (int)indAcc.count;
        p.indexType = (indAcc.componentType == 5123) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

        // Текстура
        p.textureId = 0;
        if (prim.material >= 0) {
            int texIdx = gltf.materials[prim.material].pbrMetallicRoughness.baseColorTexture.index;
            if (texIdx >= 0 && texIdx < (int)gltf.textures.size()) {
                int imgIdx = gltf.textures[texIdx].source;
                if (imgIdx >= 0 && imgIdx < (int)textures.size())
                    p.textureId = textures[imgIdx];
            }
        }

        primitives.push_back(p);
        nodes[tp.nodeIdx].primitives.push_back((int)primitives.size() - 1);
    }

    // ==================== 5. Анимации ====================
    for (const tinygltf::Animation& anim : gltf.animations) {
        Animation newAnim;
        newAnim.name = anim.name;
        newAnim.samplers.resize(anim.samplers.size());
        for (size_t s = 0; s < anim.samplers.size(); ++s) {
            const tinygltf::AnimationSampler& src = anim.samplers[s];
            Sampler& dst = newAnim.samplers[s];
            // input
            const tinygltf::Accessor& inAcc = gltf.accessors[src.input];
            const tinygltf::BufferView& inView = gltf.bufferViews[inAcc.bufferView];
            const float* inData = (const float*)&gltf.buffers[inView.buffer].data[inView.byteOffset];
            dst.input.assign(inData, inData + inAcc.count);
            // output
            const tinygltf::Accessor& outAcc = gltf.accessors[src.output];
            const tinygltf::BufferView& outView = gltf.bufferViews[outAcc.bufferView];
            const float* outData = (const float*)&gltf.buffers[outView.buffer].data[outView.byteOffset];
            dst.stride = (outAcc.type == TINYGLTF_TYPE_VEC3) ? 3 : ((outAcc.type == TINYGLTF_TYPE_VEC4) ? 4 : 1);
            dst.output.assign(outData, outData + outAcc.count * dst.stride);
        }
        for (const tinygltf::AnimationChannel& ch : anim.channels) {
            Channel c;
            c.nodeIdx = ch.target_node;
            c.samplerIdx = ch.sampler;
            if (ch.target_path == "translation") c.path = 0;
            else if (ch.target_path == "rotation") c.path = 1;
            else c.path = 2;
            newAnim.channels.push_back(c);
        }
        // Длительность
        float maxT = 0;
        for (auto& s : newAnim.samplers)
            if (!s.input.empty()) maxT = std::max(maxT, s.input.back());
        newAnim.duration = maxT;
        animations.push_back(std::move(newAnim));
    }

    // ==================== 6. Инициализация мировых матриц ====================
    float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for (size_t i = 0; i < nodes.size(); ++i) {
        bool isRoot = true;
        for (size_t j = 0; j < nodes.size(); ++j)
            for (int c : nodes[j].children)
                if (c == (int)i) { isRoot = false; break; }
        if (isRoot) updateWorldMatrices((int)i, identity);
    }
    // После создания примитивов (перед return true) добавьте:
    if (!primitives.empty()) {
        // Для первого примитива прочитаем вершины из буфера (они уже в GPU, но мы можем временно их загрузить из gltf)
        // Но проще использовать gltf-данные, которые ещё есть в памяти. Для этого нужно сохранить их в load, но у нас они уже в gltf.
        // Мы можем вычислить AABB, пройдя по всем вершинам первого примитива из gltf-данных.
        // Для простоты предположим, что у нас есть доступ к gltf-данным в этом месте (можно сохранить ссылку, но проще повторить доступ).
        // Вместо этого я просто выведу количество вершин, чтобы убедиться, что данные есть.
        const Primitive& p = primitives[0];
        printf("Primitive 0: vbo=%u, normVbo=%u, texVbo=%u, ebo=%u, indices=%d, texture=%u\n",
            p.vbo, p.normVbo, p.texVbo, p.ebo, p.indexCount, p.textureId);
    }

    // Отладка: выведем количество примитивов и индексов
    printf("Model loaded: %zu primitives\n", primitives.size());
    for (size_t i = 0; i < primitives.size(); ++i) {
        printf("  Primitive %zu: indices = %d\n", i, primitives[i].indexCount);
    }
    return true;
}

void GLBModel::unload() {
    for (const Primitive& p : primitives) {
        if (p.vbo) glDeleteBuffers(1, &p.vbo);
        if (p.normVbo) glDeleteBuffers(1, &p.normVbo);
        if (p.texVbo) glDeleteBuffers(1, &p.texVbo);
        if (p.ebo) glDeleteBuffers(1, &p.ebo);
    }
    primitives.clear();
    nodes.clear();
    animations.clear();
    for (GLuint tex : textures) glDeleteTextures(1, &tex);
    textures.clear();
    currentAnim = -1;
    playing = false;
}

// В avoextension.cpp замените метод draw на этот:
void GLBModel::draw(float x, float y, float z, float scale, float rx, float ry, float rz) const {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rx, 1, 0, 0);
    glRotatef(ry, 0, 1, 0);
    glRotatef(rz, 0, 0, 1);
    glScalef(scale, scale, scale);

    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColor4f(1,1,1,1);

    // Прямой рендеринг примитивов (без узлов)
    for (const Primitive& p : primitives) {
        if (p.textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, p.textureId);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glVertexPointer(3, GL_FLOAT, 0, 0);
        glEnableClientState(GL_VERTEX_ARRAY);
        if (p.normVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.normVbo);
            glNormalPointer(GL_FLOAT, 0, 0);
            glEnableClientState(GL_NORMAL_ARRAY);
        }
        if (p.texVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.texVbo);
            glTexCoordPointer(2, GL_FLOAT, 0, 0);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
        glDrawElements(GL_TRIANGLES, p.indexCount, p.indexType, 0);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    glPopMatrix();
}

void GLBModel::playAnimation(int idx, float speed, bool restart) {
    if (idx < 0 || idx >= (int)animations.size()) return;
    currentAnim = idx;
    animSpeed = speed;
    playing = true;
    if (restart) currentTime = 0.0f;
    update(0);
}

void GLBModel::stopAnimation() {
    playing = false;
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].dirty = false;
    }
    float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for (size_t i = 0; i < nodes.size(); ++i) {
        bool isRoot = true;
        for (size_t j = 0; j < nodes.size(); ++j)
            for (int c : nodes[j].children)
                if (c == (int)i) { isRoot = false; break; }
        if (isRoot) updateWorldMatrices((int)i, identity);
    }
}

void GLBModel::update(float deltaTime) {
    if (!playing || currentAnim < 0) return;
    currentTime += deltaTime * animSpeed;
    if (currentTime > animations[currentAnim].duration && animations[currentAnim].duration > 0)
        currentTime -= animations[currentAnim].duration;

    const Animation& anim = animations[currentAnim];
    for (const Channel& ch : anim.channels)
        if (ch.nodeIdx >= 0 && ch.nodeIdx < (int)nodes.size())
            nodes[ch.nodeIdx].dirty = true;

    for (const Channel& ch : anim.channels) {
        Node& node = nodes[ch.nodeIdx];
        const Sampler& samp = anim.samplers[ch.samplerIdx];
        if (samp.input.empty()) continue;
        if (ch.path == 0) {
            lerpVec3(currentTime, samp.input, samp.output, node.trans);
        } else if (ch.path == 1) {
            slerpQuat(currentTime, samp.input, samp.output, node.rot);
        } else {
            lerpVec3(currentTime, samp.input, samp.output, node.scale);
        }
    }

    float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for (size_t i = 0; i < nodes.size(); ++i) {
        bool isRoot = true;
        for (size_t j = 0; j < nodes.size(); ++j)
            for (int c : nodes[j].children)
                if (c == (int)i) { isRoot = false; break; }
        if (isRoot) updateWorldMatrices((int)i, identity);
    }
}

int GLBModel::getAnimationCount() const {
    return (int)animations.size();
}

const char* GLBModel::getAnimationName(int index) const {
    if (index < 0 || index >= (int)animations.size()) return "";
    return animations[index].name.c_str();
}

// ---------------------------------------------------------------------
// Вспомогательные статические методы
// ---------------------------------------------------------------------
void GLBModel::matIdentity(float* m) {
    static const float id[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    memcpy(m, id, 16 * sizeof(float));
}
void GLBModel::matMul(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i*4+j] = a[i*4+0]*b[0*4+j] + a[i*4+1]*b[1*4+j] +
                         a[i*4+2]*b[2*4+j] + a[i*4+3]*b[3*4+j];
}
void GLBModel::matTranslate(float* m, float x, float y, float z) {
    m[12] += x; m[13] += y; m[14] += z;
}
void GLBModel::matScale(float* m, float x, float y, float z) {
    m[0] *= x; m[1] *= x; m[2] *= x; m[3] *= x;
    m[4] *= y; m[5] *= y; m[6] *= y; m[7] *= y;
    m[8] *= z; m[9] *= z; m[10] *= z; m[11] *= z;
}
void GLBModel::matFromQuat(float* m, const float* q) {
    float xx = q[0]*q[0], yy = q[1]*q[1], zz = q[2]*q[2];
    float xy = q[0]*q[1], xz = q[0]*q[2], yz = q[1]*q[2];
    float wx = q[3]*q[0], wy = q[3]*q[1], wz = q[3]*q[2];
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy+wz);    m[2] = 2*(xz-wy);    m[3] = 0;
    m[4] = 2*(xy-wz);    m[5] = 1 - 2*(xx+zz); m[6] = 2*(yz+wx);    m[7] = 0;
    m[8] = 2*(xz+wy);    m[9] = 2*(yz-wx);     m[10]= 1 - 2*(xx+yy); m[11]= 0;
    m[12]=0; m[13]=0; m[14]=0; m[15]=1;
}
void GLBModel::trsToMatrix(float* out, const float* t, const float* r, const float* s) {
    matFromQuat(out, r);
    matScale(out, s[0], s[1], s[2]);
    matTranslate(out, t[0], t[1], t[2]);
}
void GLBModel::lerpVec3(float t, const std::vector<float>& times,
                        const std::vector<float>& values, float* out) {
    if (times.empty()) { out[0]=out[1]=out[2]=0; return; }
    if (t <= times.front()) {
        out[0]=values[0]; out[1]=values[1]; out[2]=values[2]; return;
    }
    if (t >= times.back()) {
        size_t last = values.size() - 3;
        out[0]=values[last]; out[1]=values[last+1]; out[2]=values[last+2]; return;
    }
    size_t i = 0;
    while (i+1 < times.size() && times[i+1] < t) ++i;
    float t0 = times[i], t1 = times[i+1];
    float f = (t1 - t0) > 0 ? (t - t0) / (t1 - t0) : 0;
    out[0] = values[i*3] + f*(values[(i+1)*3] - values[i*3]);
    out[1] = values[i*3+1] + f*(values[(i+1)*3+1] - values[i*3+1]);
    out[2] = values[i*3+2] + f*(values[(i+1)*3+2] - values[i*3+2]);
}
void GLBModel::slerpQuat(float t, const std::vector<float>& times,
                         const std::vector<float>& values, float* out) {
    if (times.empty()) { out[0]=out[1]=out[2]=0; out[3]=1; return; }
    if (t <= times.front()) {
        out[0]=values[0]; out[1]=values[1]; out[2]=values[2]; out[3]=values[3]; return;
    }
    if (t >= times.back()) {
        size_t last = values.size() - 4;
        out[0]=values[last]; out[1]=values[last+1]; out[2]=values[last+2]; out[3]=values[last+3]; return;
    }
    size_t i = 0;
    while (i+1 < times.size() && times[i+1] < t) ++i;
    float t0 = times[i], t1 = times[i+1];
    float f = (t1 - t0) > 0 ? (t - t0) / (t1 - t0) : 0;
    const float* q0 = &values[i*4];
    const float* q1 = &values[(i+1)*4];
    float dot = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
    float q1_neg[4];
    if (dot < 0) {
        dot = -dot;
        for (int j=0; j<4; ++j) q1_neg[j] = -q1[j];
        q1 = q1_neg;
    }
    if (dot > 0.9995f) {
        for (int j=0; j<4; ++j) out[j] = q0[j] + f*(q1[j] - q0[j]);
        float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        if (len > 0) for (int j=0; j<4; ++j) out[j] /= len;
    } else {
        float theta0 = acosf(dot);
        float theta = theta0 * f;
        float sinTheta = sinf(theta);
        float sinTheta0 = sinf(theta0);
        float s0 = cosf(theta) - dot * sinTheta / sinTheta0;
        float s1 = sinTheta / sinTheta0;
        for (int j=0; j<4; ++j) out[j] = s0 * q0[j] + s1 * q1[j];
    }
}
void GLBModel::updateWorldMatrices(int nodeIdx, const float* parentMat) {
    Node& node = nodes[nodeIdx];
    if (node.dirty) {
        trsToMatrix(node.localMat, node.trans, node.rot, node.scale);
        node.dirty = false;
    }
    matMul(parentMat, node.localMat, node.worldMat);
    for (int child : node.children)
        updateWorldMatrices(child, node.worldMat);
}
void GLBModel::drawNode(int nodeIdx) const {
    const Node& node = nodes[nodeIdx];
    glPushMatrix();
    glMultMatrixf(node.worldMat);
    for (int primIdx : node.primitives) {
        const Primitive& p = primitives[primIdx];
        if (p.textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, p.textureId);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glVertexPointer(3, GL_FLOAT, 0, 0);
        glEnableClientState(GL_VERTEX_ARRAY);
        if (p.normVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.normVbo);
            glNormalPointer(GL_FLOAT, 0, 0);
            glEnableClientState(GL_NORMAL_ARRAY);
        }
        if (p.texVbo) {
            glBindBuffer(GL_ARRAY_BUFFER, p.texVbo);
            glTexCoordPointer(2, GL_FLOAT, 0, 0);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
        glDrawElements(GL_TRIANGLES, p.indexCount, p.indexType, 0);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    for (int child : node.children) drawNode(child);
    glPopMatrix();
}

// ---------------------------------------------------------------------
// Реализация функций-обёрток (для обратной совместимости)
// ---------------------------------------------------------------------
int load_glb_model(const char* filename) {
    GLBModel model;
    if (!model.load(filename)) return -1;
    g_models.push_back(std::move(model));
    return (int)g_models.size() - 1;
}
void draw_glb_model(int model_id, float x, float y, float z,
                    float scale, float rx, float ry, float rz) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return;
    g_models[model_id].draw(x, y, z, scale, rx, ry, rz);
}
void unload_glb_model(int model_id) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return;
    g_models[model_id].unload();
}
void clear_embedded_texture_cache() {
    // Оставлено для совместимости – текстуры управляются моделями
}
void play_model_animation(int model_id, int anim_index, float speed, bool restart) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return;
    g_models[model_id].playAnimation(anim_index, speed, restart);
}
void stop_model_animation(int model_id) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return;
    g_models[model_id].stopAnimation();
}
void update_model_animation(int model_id, float delta_time) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return;
    g_models[model_id].update(delta_time);
}
int get_model_animation_count(int model_id) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return 0;
    return g_models[model_id].getAnimationCount();
}
const char* get_model_animation_name(int model_id, int anim_index) {
    if (model_id < 0 || model_id >= (int)g_models.size()) return "";
    return g_models[model_id].getAnimationName(anim_index);
}