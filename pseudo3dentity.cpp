#include "pseudo3dentity.h"
#include "avoengine.h"
#include <cmath>

std::vector<pseudo_3d_entity*> allEntities;

//              класс для рисовки псевдо 3д существ
pseudo_3d_entity::pseudo_3d_entity(float x, float y, float z,
                                   float g_angle, float v_angle, float r_angle,
                                   const std::vector<std::string>& textures, int v_angles,
                                   const std::vector<float>& vertices)
    : x(x), y(y), z(z),
      g_angle(g_angle), v_angle(v_angle), r_angle(r_angle),
      textureFiles(textures), v_angles(v_angles),
      vertices_(vertices) {
    computeRadius();
    textureIDs.resize(textureFiles.size());
    for (size_t i = 0; i < textureFiles.size(); ++i)
        textureIDs[i] = loadTextureFromFile(textureFiles[i].c_str());
    allEntities.push_back(this);
}
// проверяем есть ли на экране
bool pseudo_3d_entity::isVisible(float cam_x, float cam_y, float cam_z) const{
    const float dx=x-cam_x,dy=y-cam_y,dz=z-cam_z;
    const float fx=camera.ctr_x-camera.eye_x;
    const float fy=camera.ctr_y-camera.eye_y;
    const float fz=camera.ctr_z-camera.eye_z;

    const float depth=dx*fx+dy*fy+dz*fz;

    if(depth+radius<camera.znear)return false;
    if(depth-radius>camera.zfar)return false;

    const float dist=sqrtf(dx*dx+dy*dy+dz*dz);
    if(dist<1e-4f)return true;

    const float aspect=(window_h>0)?float(window_w)/float(window_h):1.0f;
    const float half_v=camera.fov*0.5f*float(M_PI)/180.0f;
    const float half_h=atanf(tanf(half_v)*aspect);
    const float half_diag=sqrtf(half_h*half_h+half_v*half_v);
    const float slack=asinf(fminf(1.0f,radius/dist));

    return (depth/dist)>=cosf(half_diag+slack);
}
// вычисляем какую текстуру поставить
int pseudo_3d_entity::getTextureIndex(float dir_x, float dir_y, float dir_z) const {
    if (fabsf(dir_x - cachedDirX) < 0.01f &&
        fabsf(dir_y - cachedDirY) < 0.01f &&
        fabsf(dir_z - cachedDirZ) < 0.01f)
        return cachedTexIdx;

    cachedDirX = dir_x;
    cachedDirY = dir_y;
    cachedDirZ = dir_z;

    if (textureFiles.empty()) {
        cachedTexIdx = -1;
        return -1;
    }

    const int total = int(textureFiles.size());
    const int h_count = total / v_angles;
    if (h_count <= 0) {
        cachedTexIdx = -1;
        return -1;
    }
    const float ga = g_angle * float(M_PI) / 180.0f;
    const float va = v_angle * float(M_PI) / 180.0f;
    const float ra = r_angle * float(M_PI) / 180.0f;

    float lx = dir_x, ly = dir_y, lz = dir_z;

    float cos_ra = cosf(-ra), sin_ra = sinf(-ra);
    float tx = lx * cos_ra - ly * sin_ra;
    float ty = lx * sin_ra + ly * cos_ra;
    lx = tx; ly = ty;

    float cos_va = cosf(-va), sin_va = sinf(-va);
    tx = lx;
    ty = ly * cos_va - lz * sin_va;
    float tz = ly * sin_va + lz * cos_va;
    lx = tx; ly = ty; lz = tz;

    float cos_ga = cosf(-ga), sin_ga = sinf(-ga);
    tx = lx * cos_ga + lz * sin_ga;
    tz = -lx * sin_ga + lz * cos_ga;
    lx = tx; lz = tz;

    float local_h = atan2f(lx, lz) * 180.0f / float(M_PI);
    float local_v = atan2f(ly, sqrtf(lx*lx + lz*lz)) * 180.0f / float(M_PI);

    float v_rel = fmaxf(0.0f, fminf(180.0f, local_v + 90.0f));
    int v_index = int(fminf(v_rel / (180.0f / v_angles), float(v_angles - 1)));

    const float step_h = 360.0f / h_count;
    if (local_h < 0) local_h += 360.0f;
    int h_index = int((local_h + step_h * 0.5f) / step_h) % h_count;

    cachedTexIdx = v_index * h_count + h_index;
    if (cachedTexIdx >= total) cachedTexIdx = total - 1;
    return cachedTexIdx;
}

// рисуем сущность
void pseudo_3d_entity::draw(float cam_x, float cam_y, float cam_z) const {
    DrawCommand cmd;
    cmd.type = CMD_PSEUDO3D;
    cmd.entity = this;
    cmd.cam_x = cam_x; cmd.cam_y = cam_y; cmd.cam_z = cam_z;
    cmd.shaderID = currentShaderProg;
    drawQueue.push_back(cmd);
}

void pseudo_3d_entity::computeRadius() {
    float maxDist = 0.0f;
    for (size_t i = 0; i < vertices_.size(); i += 2) {
        float vx = vertices_[i];
        float vy = vertices_[i+1];
        float dist = sqrtf(vx*vx + vy*vy);
        if (dist > maxDist) maxDist = dist;
    }
    radius = maxDist;
}

GLuint pseudo_3d_entity::getTextureFromDirection(float lx, float ly, float lz) const {
    int idx = getTextureIndex(lx, ly, lz);
    if (idx < 0 || idx >= (int)textureIDs.size()) return 0;
    return textureIDs[idx];
}

GLuint pseudo_3d_entity::getShadowTexture(float lx, float ly, float lz) const {
    return getTextureFromDirection(lx, ly, lz);
}