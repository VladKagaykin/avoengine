#include "portals_rc.h"
#include "avoengine.h"
#include <mutex>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

std::vector<Portal*> allPortals;

Portal::Portal(float ax, float ay, float az,
               float bx, float by, float bz,
               const std::vector<float>& verts,
               float yawA, float pitchA, float rollA,
               float yawB, float pitchB, float rollB)
    : ax(ax), ay(ay), az(az)
    , bx(bx), by(by), bz(bz)
    , vertices(verts)
    , yawA(yawA), pitchA(pitchA), rollA(rollA)
    , yawB(yawB), pitchB(pitchB), rollB(rollB)
{
    allPortals.push_back(this);
}

Portal::~Portal() {
    auto it = std::find(allPortals.begin(), allPortals.end(), this);
    if (it != allPortals.end()) allPortals.erase(it);
}

bool Portal::pointInPortalPolygon(const glm::vec2& point) const {
    int n = vertices.size() / 3;
    if (n < 3) return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = vertices[i * 3];
        float yi = vertices[i * 3 + 1];
        float xj = vertices[j * 3];
        float yj = vertices[j * 3 + 1];
        if (((yi > point.y) != (yj > point.y)) &&
            (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

glm::vec3 Portal::portalNormal(float px, float py, float pz, bool sideB) const {
    glm::vec3 localNormal(0.0f, 0.0f, -1.0f);
    if (sideB) localNormal = glm::vec3(0.0f, 0.0f, 1.0f); 

    float yaw   = sideB ? yawB   : yawA;
    float pitch = sideB ? pitchB : pitchA;
    float roll  = sideB ? rollB  : rollA;

    glm::mat4 rot4 = glm::mat4(1.0f);
    rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat3 rot = glm::mat3(rot4);

    return glm::normalize(rot * localNormal);
}

glm::mat4 Portal::getPortalTransform(float fx, float fy, float fz,
                                      float tx, float ty, float tz) const {
    bool srcIsA = (fx == ax && fy == ay && fz == az);

    float srcYaw, srcPitch, srcRoll, dstYaw, dstPitch, dstRoll;
    float srcX, srcY, srcZ, dstX, dstY, dstZ;

    if (srcIsA) {
        srcYaw = yawA; srcPitch = pitchA; srcRoll = rollA;
        dstYaw = yawB; dstPitch = pitchB; dstRoll = rollB;
        srcX = ax; srcY = ay; srcZ = az;
        dstX = bx; dstY = by; dstZ = bz;
    } else {
        srcYaw = yawB; srcPitch = pitchB; srcRoll = rollB;
        dstYaw = yawA; dstPitch = pitchA; dstRoll = rollA;
        srcX = bx; srcY = by; srcZ = bz;
        dstX = ax; dstY = ay; dstZ = az;
    }

    int n = (int)vertices.size() / 3;
    glm::vec3 centerSrc(0,0,0), centerDst(0,0,0);
    for (int i = 0; i < n; i++) {
        centerSrc += glm::vec3(srcX + vertices[i*3], srcY + vertices[i*3+1], srcZ + vertices[i*3+2]);
        centerDst += glm::vec3(dstX + vertices[i*3], dstY + vertices[i*3+1], dstZ + vertices[i*3+2]);
    }
    centerSrc /= (float)n;
    centerDst /= (float)n;

    auto makeBasis = [](float yaw, float pitch, float roll) {
        glm::mat4 rot4 = glm::mat4(1.0f);
        rot4 = glm::rotate(rot4, glm::radians(yaw),   glm::vec3(0.0f, 1.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        rot4 = glm::rotate(rot4, glm::radians(roll),  glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::mat3(rot4);
    };

    glm::mat3 rotSrc = makeBasis(srcYaw, srcPitch, srcRoll);
    glm::mat3 rotDst = makeBasis(dstYaw, dstPitch, dstRoll);

    glm::mat4 fromSrc(1.0f);
    fromSrc[0] = glm::vec4(rotSrc[0], 0.0f);
    fromSrc[1] = glm::vec4(rotSrc[1], 0.0f);
    fromSrc[2] = glm::vec4(rotSrc[2], 0.0f);
    fromSrc[3] = glm::vec4(centerSrc, 1.0f);

    glm::mat4 toDst(1.0f);
    toDst[0] = glm::vec4(rotDst[0], 0.0f);
    toDst[1] = glm::vec4(rotDst[1], 0.0f);
    toDst[2] = glm::vec4(rotDst[2], 0.0f);
    toDst[3] = glm::vec4(centerDst, 1.0f);

    return toDst * glm::inverse(fromSrc);
}

void Portal::draw() {
    checkTeleport();

    DrawCommand cmd;
    cmd.type = CMD_PORTAL;
    cmd.portal = this;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
}

bool Portal::teleportRay(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                         glm::vec3& newOrigin, glm::vec3& newDir) const {
    for (int side = 0; side < 2; ++side) {
        float px = (side == 0) ? ax : bx;
        float py = (side == 0) ? ay : by;
        float pz = (side == 0) ? az : bz;
        float yaw   = (side == 0) ? yawA : yawB;
        float pitch = (side == 0) ? pitchA : pitchB;
        float roll  = (side == 0) ? rollA : rollB;

        glm::vec3 N = portalNormal(px, py, pz, side == 1);
        float denom = glm::dot(dir, N);
        if (fabs(denom) < 0.0001f) continue;  
        if (denom > 0.0f) continue;           

        float t = glm::dot(N, glm::vec3(px, py, pz) - origin) / denom;
        if (t < 0.0f || t > maxDist) continue;

        glm::vec3 hitPoint = origin + dir * t;

        glm::mat4 worldSrc = glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0,1,0))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1,0,0))
                           * glm::rotate(glm::mat4(1.0f), glm::radians(roll), glm::vec3(0,0,1));
        glm::vec3 localPt = glm::vec3(glm::inverse(worldSrc) * glm::vec4(hitPoint, 1.0f));

        if (!pointInPortalPolygon(glm::vec2(localPt.x, localPt.y)))
            continue;

        float dx = (side == 0) ? bx : ax;
        float dy = (side == 0) ? by : ay;
        float dz = (side == 0) ? bz : az;
        glm::mat4 transform = getPortalTransform(px, py, pz, dx, dy, dz);

        newOrigin = glm::vec3(transform * glm::vec4(hitPoint, 1.0f));
        glm::mat3 rotMat = glm::mat3(transform);
        newDir = glm::normalize(rotMat * dir);

        return true;
    }
    return false;
}

void Portal::checkTeleport() {
    glm::vec3 camPos(camera.eye_x, camera.eye_y, camera.eye_z);

    auto computeWorldMatrix = [&](float px, float py, float pz,
                                   float yaw, float pitch, float roll) -> glm::mat4 {
        glm::mat4 rot = glm::mat4(1.0f);
        rot = glm::rotate(rot, glm::radians(yaw),   glm::vec3(0,1,0));
        rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1,0,0));
        rot = glm::rotate(rot, glm::radians(roll),  glm::vec3(0,0,1));
        return glm::translate(glm::mat4(1.0f), glm::vec3(px, py, pz)) * rot;
    };

    auto checkPortalSide = [&](SideState& state,
                                float sx, float sy, float sz,
                                float syaw, float spitch, float sroll,
                                float dx, float dy, float dz,
                                float dyaw, float dpitch, float droll,
                                glm::vec3 localNormal) -> bool
    {
        glm::mat4 worldSrc = computeWorldMatrix(sx, sy, sz, syaw, spitch, sroll);
        glm::vec3 normal = glm::mat3(worldSrc) * localNormal;

        glm::vec3 center(0.0f);
        int n = vertices.size() / 3;
        for (int i = 0; i < n; ++i) {
            glm::vec4 local(vertices[i*3], vertices[i*3+1], vertices[i*3+2], 1.0f);
            center += glm::vec3(worldSrc * local);
        }
        center /= float(n);

        float dist = glm::dot(normal, camPos - center);

        if (!state.prevValid) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            state.prevValid = true;
            return false;
        }

        float prevDist = state.prevSignedDist;

        if (prevDist * dist >= 0.0f) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            return false;
        }

        float t = prevDist / (prevDist - dist);
        glm::vec3 intersection = state.prevCamPos + t * (camPos - state.prevCamPos);

        glm::mat4 invWorld = glm::inverse(worldSrc);
        glm::vec3 localPt = glm::vec3(invWorld * glm::vec4(intersection, 1.0f));
        if (!pointInPortalPolygon(glm::vec2(localPt.x, localPt.y))) {
            state.prevCamPos = camPos;
            state.prevSignedDist = dist;
            return false;
        }

        glm::mat4 transform = getPortalTransform(sx, sy, sz, dx, dy, dz);
        glm::vec4 newPos4 = transform * glm::vec4(intersection, 1.0f);
        glm::vec3 newPos(newPos4.x, newPos4.y, newPos4.z);

        glm::mat3 rotMat = glm::mat3(transform);
        glm::vec3 oldForward(camera.dir_x, camera.dir_y, camera.dir_z);
        glm::vec3 oldUp(camera.up_x, camera.up_y, camera.up_z);
        glm::vec3 newForward = glm::normalize(rotMat * oldForward);
        glm::vec3 newUp      = rotMat * oldUp;

        float newPitch = glm::degrees(asinf(newForward.y));
        float newYaw   = glm::degrees(atan2f(newForward.x, newForward.z));

        const float epsilon = 0.001f;
        if (newUp.y < -epsilon) {
            newPitch = 180.0f - newPitch;
            newYaw   = newYaw + 180.0f;
            newYaw = fmod(newYaw, 360.0f)+180;
            if (newYaw < 0.0f) newYaw += 360.0f;
        }

        float np = fmod(newPitch, 360.0f);
        if (np < 0) np += 360.0f;
        glm::vec3 defUp = (np > 90.0f && np < 270.0f)
                            ? glm::vec3(0.0f, -1.0f, 0.0f)
                            : glm::vec3(0.0f,  1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(newUp, newForward));
        glm::vec3 correctedUp = glm::cross(newForward, right);
        float cosRoll = glm::dot(defUp, correctedUp);
        float sinRoll = glm::dot(glm::cross(defUp, correctedUp), newForward);
        float newRoll = glm::degrees(atan2f(sinRoll, cosRoll));

        glm::mat4 worldDst = computeWorldMatrix(dx, dy, dz, dyaw, dpitch, droll);
        glm::vec3 pushNormal = glm::mat3(worldDst) * (-localNormal);
        const float push = 0.1f;  
        if (prevDist > 0.0f) {
            newPos += pushNormal * push;
        } else {
            newPos -= pushNormal * push;
        }

        move_camera(newPos.x, newPos.y, newPos.z, newPitch, newYaw, newRoll);

        state.prevValid = false;
        SideState& otherState = (&state == &sideA) ? sideB : sideA;
        otherState.prevValid = false;
        return true;
    };

    if (checkPortalSide(sideA, ax, ay, az, yawA, pitchA, rollA,
                        bx, by, bz, yawB, pitchB, rollB,
                        glm::vec3(0.0f, 0.0f, -1.0f)))
        return;

    checkPortalSide(sideB, bx, by, bz, yawB, pitchB, rollB,
                    ax, ay, az, yawA, pitchA, rollA,
                    glm::vec3(0.0f, 0.0f, 1.0f));
}