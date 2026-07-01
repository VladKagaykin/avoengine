#ifndef PSEUDO3DENTITY_H
#define PSEUDO3DENTITY_H
#include <vector>
#include <string>
#include <GL/glew.h>

struct DrawCommand;
struct CameraParams;
extern std::vector<DrawCommand> drawQueue;
extern GLuint currentShaderProg;
extern CameraParams camera;
extern int window_w;
extern int window_h;

GLuint loadTextureFromFile(const char* filename);

class pseudo_3d_entity {
public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle, float r_angle,
                     const std::vector<std::string>& textures, int v_angles,
                     const std::vector<float>& vertices);

    void draw(float cam_x, float cam_y, float cam_z) const;

    float getRadius() const { return radius; }

    float getX() const { return x; }
    float getY() const { return y; }
    float getZ() const { return z; }

    void setGAngle(float a) { g_angle = a; }
    void setVAngle(float a) { v_angle = a; }
    void setRAngle(float a) { r_angle = a; }

    float getGAngle() const { return g_angle; }
    float getVAngle() const { return v_angle; }
    float getRAngle() const { return r_angle; }

    GLuint getShadowTexture(float dir_x, float dir_y, float dir_z) const;
    GLuint getTextureFromDirection(float dir_x, float dir_y, float dir_z) const;
    const std::vector<std::string>& getTextures() const { return textureFiles; }
    const std::vector<float>& getVertices() const { return vertices_; }
    const std::vector<GLuint>& getTextureIDs() const { return textureIDs; }
    int getVAngles() const { return v_angles; }

    int getTextureIndex(float dir_x, float dir_y, float dir_z) const;
    bool isVisible(float cam_x, float cam_y, float cam_z) const;

    GLuint getTextureID(int index) const { return (index >= 0 && index < (int)textureIDs.size()) ? textureIDs[index] : 0; }

    float x, y, z;
    float g_angle, v_angle, r_angle;
    float radius;
private:
    void computeRadius();

    std::vector<std::string> textureFiles;
    std::vector<GLuint> textureIDs;
    int v_angles;
    std::vector<float> vertices_;

    mutable int cachedTexIdx = -1;
    mutable float cachedDirX = 1e9f, cachedDirY = 1e9f, cachedDirZ = 1e9f;
};

extern std::vector<pseudo_3d_entity*> allEntities;
#endif