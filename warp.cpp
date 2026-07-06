#include "avoengine.h"
#include "warp.h"
#include "textures.h"

// гравитационное искажение
WarpPlane* activeWarpPlane = nullptr;

WarpPlane::WarpPlane() : originX(0), originY(0), originZ(0), yaw(0), pitch(0), roll(0),
                          sizeU(1), sizeV(1), displacementTex(0), enabled(false) {}

void WarpPlane::setDisplacementTexture(const char* filename) {
    if (displacementTex) glDeleteTextures(1, &displacementTex);
    displacementTex = loadTextureFromFile(filename);
}

void WarpPlane::setDisplacementFromData(int w, int h, const float* data) {
    if (displacementTex) glDeleteTextures(1, &displacementTex);
    glGenTextures(1, &displacementTex);
    glBindTexture(GL_TEXTURE_2D, displacementTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WarpPlane::enable() {
    enabled = true;
    activeWarpPlane = this;
}

void WarpPlane::disable() {
    enabled = false;
    if (activeWarpPlane == this) activeWarpPlane = nullptr;
}

void set_active_warp_plane(WarpPlane* wp) {
    activeWarpPlane = wp;
}
//