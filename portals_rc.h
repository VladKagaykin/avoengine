#ifndef PORTALS_RC_H
#define PORTALS_RC_H
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>

struct DrawCommand;
struct CameraParams;
extern std::mutex drawQueueMutex;
extern std::vector<DrawCommand> drawQueue;

class Portal {
public:
    Portal(
        float ax, float ay, float az,
        float bx, float by, float bz,
        const std::vector<float>& vertices,
        float yawA = 0.0f, float pitchA = 0.0f, float rollA = 0.0f,
        float yawB = 0.0f, float pitchB = 0.0f, float rollB = 0.0f
    );
    ~Portal();

    void draw();                  
    void checkTeleport();        

    bool teleportRay(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                     glm::vec3& newOrigin, glm::vec3& newDir) const;

    float ax, ay, az;   
    float bx, by, bz;   
    float yawA, pitchA, rollA;
    float yawB, pitchB, rollB;
    std::vector<float> vertices;   

    glm::vec3 portalNormal(float px, float py, float pz, bool sideB = false) const;

    bool pointInPortalPolygon(const glm::vec2& point) const;

    glm::mat4 getPortalTransform(float fx, float fy, float fz,
                                  float tx, float ty, float tz) const;

    struct SideState {
        glm::vec3 prevCamPos = glm::vec3(0.0f);
        float prevSignedDist = 0.0f;
        bool prevValid = false;
    };
    SideState sideA, sideB;
};
extern std::vector<Portal*> allPortals;
#endif