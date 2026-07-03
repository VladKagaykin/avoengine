#ifndef AMBIENT
#define AMBIENT

extern GLuint skyboxVAO, skyboxVBO, skyboxIBO;
extern int skyboxIndexCount;

extern GLint loc_ambientLight;
extern GLint loc_fogColor, loc_fogStart , loc_fogEnd;

struct sphere_panorama {
    bool enabled = false;
    GLuint texture = 0;
    std::string path;
};
extern sphere_panorama sphere_sky;

void set_panorama(const char* path);
void remove_panorama();
void draw_panorama(float camX, float camY, float camZ);

struct fog_params {
    bool enabled = false;
    float density = 0.05f;
    float color[3] = {0.7f, 0.8f, 0.9f};
    float start = 5.0f;
    float end = 30.0f;
};
extern fog_params fog;

extern float global_ambient[3];

void set_ambient_light(float r, float g, float b);
void enable_fog(float density, float r, float g, float b, float start = 5.0f, float end = 30.0f);
void disable_fog();
void set_fog_range(float start, float end);
void set_fog_color(float r, float g, float b);
void set_fog_density(float density);

#endif