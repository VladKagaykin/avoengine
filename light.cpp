#include "light.h"
#include "avoengine.h"
#include <algorithm>
#include <GLFW/glfw3.h>
#include "shaders.h"

GLint loc_numLights = -1;
std::vector<GLint> loc_lightEnabled;
std::vector<GLint> loc_lightPosition;
std::vector<GLint> loc_lightDirection;
std::vector<GLint> loc_lightDiffuse;
std::vector<GLint> loc_lightCutoff;
std::vector<GLint> loc_lightAttenuation;

// свет

Light::Light() {}

void Light::setPosition(float x, float y, float z) {pos[0] = x; pos[1] = y; pos[2] = z;}

void Light::setDirectionFromPitchYaw(float pitch_deg, float yaw_deg) {
    float pitch = pitch_deg * M_PI / 180.0f;
    float yaw   = yaw_deg   * M_PI / 180.0f;
    dir[0] = cosf(pitch) * sinf(yaw);
    dir[1] = sinf(pitch);
    dir[2] = cosf(pitch) * cosf(yaw);
}

void Light::setColor(float r, float g, float b) {
    color[0] = r; color[1] = g; color[2] = b;
}

void Light::setIntensity(float i) {
    intensity = i;
}

void Light::setRadius(float radius_deg) {
    cutoff = (radius_deg >= 360.0f) ? 180.0f : radius_deg;
}

void Light::setAttenuation(float constant, float linear, float quadratic) {
    constAtt = constant;
    linearAtt = linear;
    quadAtt = quadratic;
}

void Light::enable() {
    if (!enabled) {
        enabled = true;
        activeLights.push_back(this);
    }
}

void Light::disable() {
    if (enabled) {
        enabled = false;
        auto it = std::find(activeLights.begin(), activeLights.end(), this);
        if (it != activeLights.end()) activeLights.erase(it);
    }
}

// свет
std::vector<Light*> activeLights;

void applyAllLights() {
    if (currentShaderProg == 0) return;

    std::vector<Light*> candidates;
    for (Light* light : activeLights) {
        if (light->isEnabled()) {
            candidates.push_back(light);
        }
    }

    if (candidates.empty()) {
        if (loc_numLights != -1) glUniform1i(loc_numLights, 0);
        return;
    }

    const float camX = camera.eye_x;
    const float camY = camera.eye_y;
    const float camZ = camera.eye_z;
    const float camDirX = camera.dir_x;
    const float camDirY = camera.dir_y;
    const float camDirZ = camera.dir_z;

    std::sort(candidates.begin(), candidates.end(),
        [&](Light* a, Light* b) {
            float dx_a = a->pos[0] - camX;
            float dy_a = a->pos[1] - camY;
            float dz_a = a->pos[2] - camZ;
            float dist_a = sqrtf(dx_a*dx_a + dy_a*dy_a + dz_a*dz_a) + 0.001f;

            float dx_b = b->pos[0] - camX;
            float dy_b = b->pos[1] - camY;
            float dz_b = b->pos[2] - camZ;
            float dist_b = sqrtf(dx_b*dx_b + dy_b*dy_b + dz_b*dz_b) + 0.001f;

            float dot_a = (dx_a * camDirX + dy_a * camDirY + dz_a * camDirZ) / dist_a;
            float dot_b = (dx_b * camDirX + dy_b * camDirY + dz_b * camDirZ) / dist_b;

            float dirFactor_a = std::max(0.2f, dot_a);
            float dirFactor_b = std::max(0.2f, dot_b);

            float weight_a = a->intensity * dirFactor_a / (dist_a * dist_a);
            float weight_b = b->intensity * dirFactor_b / (dist_b * dist_b);

            return weight_a > weight_b;
        });

    int count = std::min((int)candidates.size(), (int)loc_lightEnabled.size());
    if (loc_numLights != -1) glUniform1i(loc_numLights, count);

    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);

    for (int i = 0; i < count; ++i) {
        Light* light = candidates[i];

        float worldPos[4] = { light->pos[0], light->pos[1], light->pos[2], 1.0f };
        float viewPos[4] = {0,0,0,0};
        for (int r = 0; r < 4; ++r) {
            viewPos[r] = mv[r]   * worldPos[0] +
                         mv[r+4] * worldPos[1] +
                         mv[r+8] * worldPos[2] +
                         mv[r+12]* worldPos[3];
        }

        if (loc_lightEnabled[i] != -1) glUniform1i(loc_lightEnabled[i], 1);
        if (loc_lightPosition[i] != -1) glUniform3f(loc_lightPosition[i], light->pos[0], light->pos[1], light->pos[2]);
        if (loc_lightDirection[i] != -1) glUniform3f(loc_lightDirection[i], light->dir[0], light->dir[1], light->dir[2]);

        float diff[3] = { light->color[0] * light->intensity,
                          light->color[1] * light->intensity,
                          light->color[2] * light->intensity };
        if (loc_lightDiffuse[i] != -1) glUniform3fv(loc_lightDiffuse[i], 1, diff);
        if (loc_lightCutoff[i] != -1) glUniform1f(loc_lightCutoff[i], cosf(light->cutoff * M_PI / 180.0f));
        if (loc_lightAttenuation[i] != -1) glUniform3f(loc_lightAttenuation[i], light->constAtt, light->linearAtt, light->quadAtt);
    }
}
