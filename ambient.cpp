#include "avoengine.h"
#include "ambient.h"

#include <SOIL/SOIL.h>

GLuint skyboxVAO = 0, skyboxVBO = 0, skyboxIBO = 0;
int skyboxIndexCount = 0;

GLint loc_ambientLight = -1;
GLint loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;

// туман
fog_params fog;

float global_ambient[3] = {0.05f, 0.05f, 0.05f};

// панорама
sphere_panorama sphere_sky;
 
void set_panorama(const char* path) {
    if (sphere_sky.enabled) remove_panorama();

    sphere_sky.texture = SOIL_load_OGL_texture(
        path,
        SOIL_LOAD_AUTO,
        SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT
    );

    if (sphere_sky.texture == 0) return;

    glBindTexture(GL_TEXTURE_2D, sphere_sky.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    float radius = 180.0f;
    int stacks = 32, slices = 32;

    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<float> colors;
    std::vector<unsigned int> indices;

    for (int i = 0; i <= stacks; ++i) {
        float lat0 = M_PI * (-0.5f + (float)i / stacks);
        float z0 = sin(lat0), zr0 = cos(lat0);
        for (int j = 0; j <= slices; ++j) {
            float lng = 2.0f * M_PI * (float)j / slices;
            float x = cos(lng), y = sin(lng);
            vertices.push_back(x * zr0 * radius);
            vertices.push_back(y * zr0 * radius);
            vertices.push_back(z0 * radius);
            texcoords.push_back((float)j / slices);
            texcoords.push_back((float)i / stacks);
            colors.push_back(1.0f); colors.push_back(1.0f); colors.push_back(1.0f); colors.push_back(1.0f);
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            unsigned int first = i * (slices + 1) + j;
            unsigned int second = first + slices + 1;
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    skyboxIndexCount = (int)indices.size();

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glGenBuffers(1, &skyboxIBO);

    glBindVertexArray(skyboxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    GLuint texVBO;
    glGenBuffers(1, &texVBO);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO);
    glBufferData(GL_ARRAY_BUFFER, texcoords.size() * sizeof(float), texcoords.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    GLuint colVBO;
    glGenBuffers(1, &colVBO);
    glBindBuffer(GL_ARRAY_BUFFER, colVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glDeleteBuffers(1, &texVBO);
    glDeleteBuffers(1, &colVBO);

    sphere_sky.enabled = true;
    sphere_sky.path = path;
}
void remove_panorama() {
    if (!sphere_sky.enabled) return;
    glDeleteTextures(1, &sphere_sky.texture);
    if (skyboxVAO) { glDeleteVertexArrays(1, &skyboxVAO); skyboxVAO = 0; }
    if (skyboxVBO) { glDeleteBuffers(1, &skyboxVBO); skyboxVBO = 0; }
    if (skyboxIBO) { glDeleteBuffers(1, &skyboxIBO); skyboxIBO = 0; }
    skyboxIndexCount = 0;
    sphere_sky.enabled = false;
}
void draw_panorama(float camX, float camY, float camZ) {
    if (!sphere_sky.enabled || sphere_sky.texture == 0 || skyboxVAO == 0) return;
    DrawCommand cmd;
    cmd.type = CMD_PANORAMA;
    cmd.obj_cx = camX; cmd.obj_cy = camY; cmd.obj_cz = camZ;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
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
