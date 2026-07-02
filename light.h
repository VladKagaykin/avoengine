#ifndef LIGHT
#define LIGHT

#include <vector>
#include <GL/glew.h>

extern GLint loc_numLights;
extern std::vector<GLint> loc_lightEnabled;
extern std::vector<GLint> loc_lightPosition;
extern std::vector<GLint> loc_lightDirection;
extern std::vector<GLint> loc_lightDiffuse;
extern std::vector<GLint> loc_lightCutoff;
extern std::vector<GLint> loc_lightAttenuation;

class Light {
public:
    Light();
    void setPosition(float x, float y, float z);
    void setDirectionFromPitchYaw(float pitch_deg, float yaw_deg);
    void setColor(float r, float g, float b);
    void setIntensity(float intensity);
    void setRadius(float radius_deg);
    void setAttenuation(float constant, float linear, float quadratic);
    void enable();
    void disable();
    bool isEnabled() const { return enabled; }

    bool enabled = false;
    float pos[3]   = {0,0,0};
    float dir[3]   = {0,0,-1};
    float color[3] = {1,1,1};
    float intensity = 1.0f;
    float cutoff    = 180.0f;
    float constAtt  = 1.0f;
    float linearAtt = 0.0f;
    float quadAtt   = 0.0f;
};

extern std::vector<Light*> activeLights;

void applyAllLights();

#endif