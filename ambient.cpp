#include "avoengine.h"
#include "ambient.h"
#include "shaders.h"
#include "baking_scene.h"

#include <SOIL/SOIL.h>

GLuint skyboxVAO = 0, skyboxVBO = 0, skyboxIBO = 0;
int skyboxIndexCount = 0;

GLint loc_ambientLight = -1;
GLint loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;

// туман
fog_params fog;

float global_ambient[3] = {0.05f, 0.05f, 0.05f};

// панорама

Function panorama;
 
void set_panorama(Function panorama_function) {
    panorama = panorama_function;
}
void remove_panorama() {
    panorama = nullptr;
}
void draw_panorama() {
    if (panorama) {
        panorama(); 
    }
}

void set_ambient_light(float r, float g, float b) {
    global_ambient[0] = r;
    global_ambient[1] = g;
    global_ambient[2] = b;
    if (loc_ambientLight != -1) glUniform3f(loc_ambientLight, r, g, b);
}

//              туман
void enable_fog(float density, float r, float g, float b, float start, float end) {
    fog.enabled = true;
    fog.density = density;
    fog.color[0] = r; fog.color[1] = g; fog.color[2] = b;
    fog.start = start;
    fog.end = end;

    if (currentShaderProg) {
        if (loc_fogColor != -1) glUniform3f(loc_fogColor, r, g, b);
        if (loc_fogStart != -1) glUniform1f(loc_fogStart, start);
        if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, end);
    }
}

void disable_fog(){
    fog.enabled = false;
}

void set_fog_density(float density) {
    fog.density = density;
    if (fog.enabled) {
        fog.start = 2.0f / density;
        fog.end = 15.0f / density;
        if (currentShaderProg) {
            if (loc_fogStart != -1) glUniform1f(loc_fogStart, fog.start);
            if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, fog.end);
        }
    }
}

void set_fog_color(float r, float g, float b) {
    fog.color[0] = r; fog.color[1] = g; fog.color[2] = b;
    if (fog.enabled) {
        if (currentShaderProg) {
            if (loc_fogColor != -1) glUniform3f(loc_fogColor, r, g, b);
        }
    }
}

void set_fog_range(float start, float end) {
    fog.start = start;
    fog.end = end;
    if (fog.enabled) {
        if (currentShaderProg) {
            if (loc_fogStart != -1) glUniform1f(loc_fogStart, start);
            if (loc_fogEnd != -1) glUniform1f(loc_fogEnd, end);
        }
    }
}
