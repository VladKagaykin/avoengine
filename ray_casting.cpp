#include "avoengine.h"
#include "ray_casting.h"

#include "portals_rc.h"
#include "pseudo3dentity.h"
#include "light.h"
#include "ambient.h"
#include "audio_not_mini.h"
#include "2d_primitives.h"
#include "3d_primitives.h"
#include "shaders.h"
#include "textures.h"
#include "warp.h"
#include "baking_scene.h"

#include <iostream>
#include <cmath>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <SOIL/SOIL.h>

GLuint default_RC_Shader = 0;

static const char* defaultVertexShader = R"(
#version 120
attribute vec4 aVertex;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord;

varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec4 vClipPos;
varying vec2 vUV;

uniform mat4 u_projection;
uniform mat4 u_modelView;
uniform bool raycast;
uniform bool displayMode;

void main() {
    vec4 mvPos = u_modelView * aVertex;
    vN = normalize(mat3(u_modelView) * aNormal);
    vP = mvPos.xyz;
    vColor = aColor;
    vTexCoord = aTexCoord;
    vWorldPos = aVertex.xyz;
    gl_Position = u_projection * mvPos;
    vClipPos = gl_Position;
    if (raycast || displayMode)
        vUV = (aVertex.xy + 1.0) * 0.5;
    else
        vUV = vec2(0.0);
}
)";

static const char* defaultFragmentShader = R"(
#version 120
struct Light {
    bool enabled;
    vec3 position;
    vec3 direction;
    vec3 diffuse;
    float cutoff;
    vec3 attenuation;
};
varying vec3 vN;
varying vec3 vP;
varying vec4 vColor;
varying vec2 vTexCoord;
varying vec4 vClipPos;
varying vec2 vUV;
uniform sampler2D tex;
uniform Light lights[16];
uniform int numLights;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform bool portalMode;
uniform bool portalDepthOnly;
uniform sampler2D portalTex;
uniform bool raycast;
uniform vec3 camPos;
uniform mat4 invViewProj;
uniform sampler3D sdfVolume;
uniform sampler3D sdfColorTex;
uniform sampler3D sdfUVTex;
uniform sampler3D sdfAtlasTex;
uniform sampler2D atlas[8];
uniform vec3 sdfBBoxMin;
uniform vec3 sdfBBoxMax;
uniform float sdfEpsilon;
uniform int sdfMaxSteps;
uniform float maxDist;
uniform float shadowBias;
uniform float camWarpStrength;
uniform float shadowWarpStrength;
uniform float camStepSize;
uniform float shadowStepSize;
uniform int maxBounces;
uniform int maxShadowBounces;
uniform int raySamples;
uniform bool displayMode;
uniform sampler2D accumulationTex;
uniform float frameCount;
uniform int portalCount;
uniform vec3 portalPos[8];
uniform vec3 portalNormal[8];
uniform float portalD[8];
uniform mat4 portalInvWorld[8];
uniform int portalVertCount[8];
uniform vec2 portalVerts[8 * 16];
uniform mat4 portalTeleport[8];
uniform bool warpPlaneEnabled;
uniform vec3 warpPlaneOrigin;
uniform vec3 warpPlaneAxisU;
uniform vec3 warpPlaneAxisV;
uniform sampler2D warpPlaneDisplacementTex;
uniform bool debugMode;
uniform vec3 debugColor;
const float PI = 3.14159265;
const float TWO_PI = 6.2831853;
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float sdfSample(vec3 worldPos) {
    vec3 texCoord = (worldPos - sdfBBoxMin) / (sdfBBoxMax - sdfBBoxMin);
    if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0)))) return 1e6;
    return texture3D(sdfVolume, texCoord).r;
}
vec3 sdfGradient(vec3 worldPos) {
    float eps = sdfEpsilon * 2.0;
    float dx = sdfSample(worldPos + vec3(eps,0,0)) - sdfSample(worldPos - vec3(eps,0,0));
    float dy = sdfSample(worldPos + vec3(0,eps,0)) - sdfSample(worldPos - vec3(0,eps,0));
    float dz = sdfSample(worldPos + vec3(0,0,eps)) - sdfSample(worldPos - vec3(0,0,eps));
    return normalize(vec3(dx, dy, dz));
}
bool traceSegment(vec3 ro, vec3 rd, float maxT,
                  out float hitT, out vec3 hitPos, out vec3 hitNormal,
                  out vec3 hitCol, out float hitAlpha, out int hitTexID,
                  out bool isPortalHit, out int portalIdx) {
    float t = 0.0;
    bool surfaceHit = false;
    float surfT = maxT;
    vec3 surfPos, surfNorm, surfCol;
    float surfAlpha;
    int surfTexID = 0;
    for (int i = 0; i < sdfMaxSteps; i++) {
        vec3 p = ro + rd * t;
        float d = sdfSample(p);
        if (d < sdfEpsilon) {
            surfT = t;
            surfPos = p;
            surfNorm = sdfGradient(p);
            vec3 texCoord = (p - sdfBBoxMin) / (sdfBBoxMax - sdfBBoxMin);
            vec4 objColor = texture3D(sdfColorTex, texCoord);
            vec2 uv = texture3D(sdfUVTex, texCoord).rg;
            float atlasIdxVal = texture3D(sdfAtlasTex, texCoord).r;
            int atlIdx = int(atlasIdxVal);
            vec4 texCol = vec4(1.0);
            if (atlIdx >= 0 && atlIdx < 8) {
                texCol = texture2D(atlas[atlIdx], uv);
            }
            surfCol = objColor.rgb * texCol.rgb;
            surfAlpha = objColor.a * texCol.a;
            surfTexID = atlIdx;
            surfaceHit = true;
            break;
        }
        t += max(abs(d), 0.001);
        if (t >= maxT) break;
    }
    hitT = maxT;
    isPortalHit = false;
    portalIdx = -1;
    float closest = maxT;
    if (surfaceHit && surfT < closest) {
        closest = surfT;
        hitPos = surfPos;
        hitNormal = surfNorm;
        hitCol = surfCol;
        hitAlpha = surfAlpha;
        hitTexID = surfTexID;
        isPortalHit = false;
    }
    for (int p = 0; p < portalCount; p++) {
        vec3 Np = portalNormal[p];
        float denom = dot(rd, Np);
        if (abs(denom) < 0.0001) continue;
        float tPortal = -(dot(ro, Np) + portalD[p]) / denom;
        if (tPortal > 0.001 && tPortal < closest) {
            vec3 candidatePos = ro + rd * tPortal;
            vec2 localPt = (portalInvWorld[p] * vec4(candidatePos, 1.0)).xy;
            int vc = portalVertCount[p];
            bool inside = false;
            for (int i = 0, j = vc-1; i < vc; j = i++) {
                vec2 vi = portalVerts[p * 16 + i];
                vec2 vj = portalVerts[p * 16 + j];
                if (((vi.y > localPt.y) != (vj.y > localPt.y)) &&
                    (localPt.x < (vj.x-vi.x)*(localPt.y-vi.y)/(vj.y-vi.y)+vi.x))
                    inside = !inside;
            }
            if (inside) {
                closest = tPortal;
                hitPos = candidatePos;
                isPortalHit = true;
                portalIdx = p;
                surfaceHit = false;
            }
        }
    }
    hitT = closest;
    return closest < maxT;
}
float shadowRay(int lightIdx, vec3 hitPos, vec3 N, mat4 portalTransform, int samples) {
    if (samples <= 0) samples = 1;
    float totalShadow = 0.0;
    for (int s = 0; s < samples; s++) {
        Light L = lights[lightIdx];
        vec3 lightPos = (portalTransform * vec4(L.position, 1.0)).xyz;
        vec3 lightDir = L.direction;
        bool isSpot = dot(lightDir, lightDir) > 0.000001;
        if (isSpot) {
            lightDir = normalize(mat3(portalTransform) * L.direction);
        }
        vec3 jitter = (samples > 1)
            ? vec3(hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(0.0, 1.0)) - 0.5,
                   hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(1.0, 0.0)) - 0.5,
                   hash(gl_FragCoord.xy + vec2(float(s)*0.123, float(lightIdx)*0.456) + vec2(1.0, 1.0)) - 0.5) * 0.01
            : vec3(0.0);
        vec3 ro = hitPos + jitter + N * shadowBias;
        vec3 toLight = lightPos - ro;
        float distToLight = length(toLight);
        if (isSpot) {
            vec3 dirToLight = toLight / distToLight;
            float cosAngle = dot(-dirToLight, lightDir);
            if (cosAngle < L.cutoff) { totalShadow += 0.0; continue; }
        }
        vec3 rd = normalize(toLight);
        mat4 currentTransform = portalTransform;
        int portalBounces = 0;
        float shadowContrib = 1.0;
        float remaining = distToLight;
        if (warpPlaneEnabled) {
            vec3 localPos = ro - warpPlaneOrigin;
            float u = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
            float v = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
            if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
                vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(u, v)).rgb;
                rd = normalize(rd + disp * shadowWarpStrength);
            }
        }
        while (remaining > 0.001 && portalBounces <= maxShadowBounces) {
            float tHit;
            vec3 segPos, segNorm, segCol;
            float hitAlpha;
            int segTex;
            bool isPortalHit;
            int portalIdx;
            bool segHit = traceSegment(ro, rd, remaining, tHit, segPos, segNorm, segCol, hitAlpha, segTex, isPortalHit, portalIdx);
            if (!segHit) break;
            if (isPortalHit) {
                ro = segPos;
                remaining -= tHit;
                currentTransform = portalTeleport[portalIdx] * currentTransform;
                ro = vec3(portalTeleport[portalIdx] * vec4(ro, 1.0));
                rd = normalize(mat3(portalTeleport[portalIdx]) * rd);
                ro += rd * 0.001;
                lightPos = (currentTransform * vec4(L.position, 1.0)).xyz;
                if (isSpot) lightDir = normalize(mat3(currentTransform) * L.direction);
                toLight = lightPos - ro;
                distToLight = length(toLight);
                rd = normalize(toLight);
                remaining = distToLight;
                if (isSpot && dot(-rd, lightDir) < L.cutoff) { shadowContrib = 0.0; break; }
                portalBounces++;
                continue;
            } else {
                if (tHit > shadowBias) {
                    shadowContrib *= (1.0 - hitAlpha);
                }
                break;
            }
        }
        totalShadow += shadowContrib;
    }
    return totalShadow / float(samples);
}
void main() {
    if (displayMode) {
        gl_FragColor = texture2D(accumulationTex, vUV);
        return;
    }
    if (portalMode) {
        if (portalDepthOnly) { gl_FragColor = vec4(0.0); return; }
        vec2 uv = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;
        gl_FragColor = texture2D(portalTex, uv);
        return;
    }
    float invFogRange = 1.0 / (fogEnd - fogStart);
    if (raycast) {
        int samples = raySamples > 1 ? raySamples : 1;
        vec3 accumulatedColor = vec3(0.0);
        for (int s = 0; s < samples; s++) {
            vec2 jitter = (samples > 1)
                ? vec2(hash(gl_FragCoord.xy + vec2(float(s), 0.0)),
                       hash(gl_FragCoord.xy + vec2(0.0, float(s)))) - 0.5
                : vec2(0.0);
            vec2 sampleUV = vUV + jitter / vec2(float(1024), float(768));
            vec4 clipPos = vec4(sampleUV * 2.0 - 1.0, -1.0, 1.0);
            vec4 worldPos = invViewProj * clipPos;
            worldPos /= worldPos.w;
            vec3 ro = camPos;
            vec3 rd = normalize(worldPos.xyz - ro);
            float totalDist = 0.0;
            mat4 cumulativePortalTransform = mat4(1.0);
            int bounce = 0;
            bool continueRay = true;
            float transparency = 1.0;
            while (continueRay && bounce < maxBounces) {
                float tHit;
                vec3 hitPos, hitNormal, hitCol;
                float hitAlpha;
                int hitTexID;
                bool isPortalHit;
                int portalHitIdx;
                bool segHit = false;
                if (warpPlaneEnabled) {
                    float travelled = 0.0;
                    for (int step = 0; step < int(ceil(maxDist / camStepSize)); step++) {
                        if (travelled >= maxDist) break;
                        float stepSize = min(camStepSize, maxDist - travelled);
                        vec3 localPos = ro - warpPlaneOrigin;
                        float u = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
                        float v = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
                        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
                            vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(u, v)).rgb;
                            rd = normalize(rd + disp * camWarpStrength);
                        }
                        segHit = traceSegment(ro, rd, stepSize, tHit, hitPos, hitNormal, hitCol, hitAlpha, hitTexID, isPortalHit, portalHitIdx);
                        if (segHit) {
                            totalDist += travelled + tHit;
                            break;
                        }
                        ro += rd * stepSize;
                        travelled += stepSize;
                    }
                } else {
                    segHit = traceSegment(ro, rd, maxDist, tHit, hitPos, hitNormal, hitCol, hitAlpha, hitTexID, isPortalHit, portalHitIdx);
                    if (segHit) totalDist = tHit;
                }
                if (!segHit) {
                    continueRay = false;
                    break;
                }
                if (isPortalHit) {
                    cumulativePortalTransform = portalTeleport[portalHitIdx] * cumulativePortalTransform;
                    ro = vec3(portalTeleport[portalHitIdx] * vec4(hitPos, 1.0));
                    rd = normalize(mat3(portalTeleport[portalHitIdx]) * rd);
                    totalDist = 0.0;
                    continue;
                } else {
                    float fogCoord = totalDist;
                    vec3 N = normalize(hitNormal);
                    vec3 litCol = ambientLight;
                    for (int j = 0; j < numLights; j++) {
                        if (!lights[j].enabled) continue;
                        if (lights[j].attenuation.x <= 0.0) continue;
                        vec3 effectiveLightPos = (cumulativePortalTransform * vec4(lights[j].position, 1.0)).xyz;
                        if (warpPlaneEnabled) {
                            vec3 localPos = hitPos - warpPlaneOrigin;
                            float wu = dot(localPos, normalize(warpPlaneAxisU)) / length(warpPlaneAxisU) + 0.5;
                            float wv = dot(localPos, normalize(warpPlaneAxisV)) / length(warpPlaneAxisV) + 0.5;
                            if (wu >= 0.0 && wu <= 1.0 && wv >= 0.0 && wv <= 1.0) {
                                vec3 disp = texture2D(warpPlaneDisplacementTex, vec2(wu, wv)).rgb;
                                effectiveLightPos += disp * shadowWarpStrength * 2.0;
                            }
                        }
                        vec3 Lpos = effectiveLightPos;
                        vec3 Ldir = lights[j].direction;
                        bool isSpot = dot(Ldir, Ldir) > 0.000001;
                        if (isSpot) Ldir = normalize(mat3(cumulativePortalTransform) * Ldir);
                        vec3 delta = Lpos - hitPos;
                        float d2 = dot(delta, delta);
                        if (d2 < 0.000001) continue;
                        float invDist = inversesqrt(d2);
                        float d = 1.0 / invDist;
                        float atten = 1.0 / (lights[j].attenuation.x + lights[j].attenuation.y*d + lights[j].attenuation.z*d2);
                        if (isSpot) {
                            vec3 dirToHit = -delta * invDist;
                            if (dot(dirToHit, Ldir) < lights[j].cutoff) continue;
                        }
                        vec3 L = delta * invDist;
                        float diff = max(dot(N, L), 0.0);
                        if (diff > 0.0) {
                            float shadow = shadowRay(j, hitPos, N, cumulativePortalTransform, samples);
                            litCol += lights[j].diffuse * diff * atten * shadow;
                        }
                    }
                    litCol *= hitCol;
                    float fogFactor = clamp((fogEnd - fogCoord) * invFogRange, 0.0, 1.0);
                    litCol = mix(fogColor, litCol, fogFactor);
                    accumulatedColor = mix(accumulatedColor, litCol, hitAlpha * transparency);
                    transparency *= (1.0 - hitAlpha);
                    if (transparency < 0.001) {
                        continueRay = false;
                        break;
                    }
                    ro = hitPos + rd * 0.001;
                    totalDist = fogCoord;
                    bounce++;
                    continue;
                }
                bounce++;
            }
        }
        gl_FragColor = vec4(accumulatedColor / float(samples), 1.0);
        return;
    }
    vec4 texColor = texture2D(tex, vTexCoord);
    if (texColor.a < 0.01) discard;
    vec3 N = normalize(vN);
    vec3 totalLight = ambientLight;
    int nL = numLights;
    if (nL > 16) nL = 16;
    for (int i = 0; i < nL; i++) {
        if (!lights[i].enabled || lights[i].attenuation.x <= 0.0) continue;
        vec3 Lv = lights[i].position - vP;
        float d2 = dot(Lv, Lv);
        if (d2 < 0.000001) continue;
        float invDist = inversesqrt(d2);
        float dist = 1.0 / invDist;
        vec3 L = Lv * invDist;
        if (dot(-L, normalize(lights[i].direction)) < lights[i].cutoff) continue;
        float att = 1.0 / (lights[i].attenuation.x + lights[i].attenuation.y*dist + lights[i].attenuation.z*d2);
        float diff = max(dot(N, L), 0.0);
        totalLight += lights[i].diffuse * diff * att;
    }
    vec3 finalColor = texColor.rgb * vColor.rgb * totalLight;
    float fogCoord = length(vP);
    float fogFactor = clamp((fogEnd - fogCoord) * invFogRange, 0.0f, 1.0);
    finalColor = mix(fogColor, finalColor, fogFactor);
    gl_FragColor = vec4(finalColor, texColor.a * vColor.a);
}
)";

struct AtlasTextureInfo {
    int atlasIndex;
    int tileX, tileY;
    float scaleX, scaleY;
    float offsetX, offsetY;
    int origWidth, origHeight;
};
static std::vector<AtlasTextureInfo> g_atlasInfos;
static GLuint g_atlasTextures[8] = {0};
static int g_atlasCount = 0;
static int g_atlasSide = Engine_settings.ATLAS_SIDE;
static int g_tileSize = Engine_settings.TEXTURE_SIDE;
static std::unordered_map<std::string, int> g_textureNameToIndex;
static std::vector<GLint> locAtlasSlot;

void initDefault_RC_Shader() {
    if (default_RC_Shader == 0) {
        default_RC_Shader = createShaderProgram(defaultVertexShader, defaultFragmentShader);
        loc_tex = glGetUniformLocation(default_RC_Shader, "tex");
        loc_numLights = glGetUniformLocation(default_RC_Shader, "numLights");
        loc_ambientLight = glGetUniformLocation(default_RC_Shader, "ambientLight");
        loc_fogColor = glGetUniformLocation(default_RC_Shader, "fogColor");
        loc_fogStart = glGetUniformLocation(default_RC_Shader, "fogStart");
        loc_fogEnd = glGetUniformLocation(default_RC_Shader, "fogEnd");
        loc_portalMode = glGetUniformLocation(default_RC_Shader, "portalMode");
        loc_portalDepthOnly = glGetUniformLocation(default_RC_Shader, "portalDepthOnly");
        loc_portalTex = glGetUniformLocation(default_RC_Shader, "portalTex");
        loc_u_projection_default = glGetUniformLocation(default_RC_Shader, "u_projection");
        loc_u_modelView_default = glGetUniformLocation(default_RC_Shader, "u_modelView");

        int maxLights = Engine_settings.MAX_LIGHTS;
        loc_lightEnabled.resize(maxLights, -1);
        loc_lightPosition.resize(maxLights, -1);
        loc_lightDirection.resize(maxLights, -1);
        loc_lightDiffuse.resize(maxLights, -1);
        loc_lightCutoff.resize(maxLights, -1);
        loc_lightAttenuation.resize(maxLights, -1);

        char buf[64];
        for (int i = 0; i < maxLights; ++i) {
            snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
            loc_lightEnabled[i] = glGetUniformLocation(default_RC_Shader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].position", i);
            loc_lightPosition[i] = glGetUniformLocation(default_RC_Shader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].direction", i);
            loc_lightDirection[i] = glGetUniformLocation(default_RC_Shader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
            loc_lightDiffuse[i] = glGetUniformLocation(default_RC_Shader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
            loc_lightCutoff[i] = glGetUniformLocation(default_RC_Shader, buf);
            snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
            loc_lightAttenuation[i] = glGetUniformLocation(default_RC_Shader, buf);
        }
        locAtlasSlot.resize(8, -1);
        for (int i = 0; i < 8; i++) {
            char name[32]; snprintf(name, sizeof(name), "atlas[%d]", i);
            locAtlasSlot[i] = glGetUniformLocation(default_RC_Shader, name);
        }
    }
}

static GLuint debugPointShader = 0;
static GLint loc_debugPoint_proj, loc_debugPoint_modelView;

static void initDebugPointShader() {
    if (debugPointShader) return;
    const char* vs = R"(
#version 120
attribute vec3 aPos;
uniform mat4 u_projection;
uniform mat4 u_modelView;
void main() {
    gl_Position = u_projection * u_modelView * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 120
uniform vec4 color;
void main() {
    gl_FragColor = color;
}
)";
    debugPointShader = createShaderProgram(vs, fs);
    loc_debugPoint_proj = glGetUniformLocation(debugPointShader, "u_projection");
    loc_debugPoint_modelView = glGetUniformLocation(debugPointShader, "u_modelView");
}

static GLuint sdfTex3D = 0;
static GLuint sdfColorTex3D = 0;
static GLuint sdfUVTex3D = 0;
static GLuint sdfAtlasTex3D = 0;
static int sdfRes = 128;
static glm::vec3 sdfBBoxMin, sdfBBoxMax;
static bool sdfNeedsUpdate = true;
static std::vector<float> globalPosData;
static std::vector<float> globalColData;
static std::vector<float> globalUVData;
static std::vector<float> globalIdxData;
static int globalTriCount = 0;

void generateSDFTexture() {
    if (sdfTex3D) glDeleteTextures(1, &sdfTex3D);
    if (sdfColorTex3D) glDeleteTextures(1, &sdfColorTex3D);
    if (sdfUVTex3D) glDeleteTextures(1, &sdfUVTex3D);
    if (sdfAtlasTex3D) glDeleteTextures(1, &sdfAtlasTex3D);

    glGenTextures(1, &sdfTex3D);
    glBindTexture(GL_TEXTURE_3D, sdfTex3D);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sdfRes, sdfRes, sdfRes, 0, GL_RED, GL_FLOAT, nullptr);

    glGenTextures(1, &sdfColorTex3D);
    glBindTexture(GL_TEXTURE_3D, sdfColorTex3D);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, sdfRes, sdfRes, sdfRes, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &sdfUVTex3D);
    glBindTexture(GL_TEXTURE_3D, sdfUVTex3D);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RG32F, sdfRes, sdfRes, sdfRes, 0, GL_RG, GL_FLOAT, nullptr);

    glGenTextures(1, &sdfAtlasTex3D);
    glBindTexture(GL_TEXTURE_3D, sdfAtlasTex3D);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sdfRes, sdfRes, sdfRes, 0, GL_RED, GL_FLOAT, nullptr);

    std::vector<float> sdfVol(sdfRes * sdfRes * sdfRes, 1e6f);
    std::vector<unsigned char> colVol(sdfRes * sdfRes * sdfRes * 4, 0);
    std::vector<float> uvVol(sdfRes * sdfRes * sdfRes * 2, 0.0f);
    std::vector<float> atlasVol(sdfRes * sdfRes * sdfRes, 0.0f);

    glm::vec3 size = sdfBBoxMax - sdfBBoxMin;
    glm::vec3 voxelSize = size / (float)sdfRes;

    for (int z = 0; z < sdfRes; ++z) {
        for (int y = 0; y < sdfRes; ++y) {
            for (int x = 0; x < sdfRes; ++x) {
                glm::vec3 voxelCenter = sdfBBoxMin + voxelSize * glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f);
                float minDist = 1e6f;
                int bestTri = -1;
                for (int i = 0; i < globalTriCount; ++i) {
                    const float* v0 = &globalPosData[i * 9 + 0];
                    const float* v1 = &globalPosData[i * 9 + 3];
                    const float* v2 = &globalPosData[i * 9 + 6];
                    glm::vec3 a(v0[0], v0[1], v0[2]);
                    glm::vec3 b(v1[0], v1[1], v1[2]);
                    glm::vec3 c(v2[0], v2[1], v2[2]);
                    glm::vec3 ab = b - a, ac = c - a, ap = voxelCenter - a;
                    float d1 = dot(ab, ap), d2 = dot(ac, ap);
                    if (d1 <= 0.0f && d2 <= 0.0f) {
                        float dist = glm::length(ap);
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    glm::vec3 bp = voxelCenter - b;
                    float d3 = dot(ab, bp), d4 = dot(ac, bp);
                    if (d3 >= 0.0f && d4 <= d3) {
                        float dist = glm::length(bp);
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    glm::vec3 cp = voxelCenter - c;
                    float d5 = dot(ab, cp), d6 = dot(ac, cp);
                    if (d6 >= 0.0f && d5 <= d6) {
                        float dist = glm::length(cp);
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    float vc = d1*d4 - d3*d2;
                    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                        float dist = glm::length(ap - ab*(d1/dot(ab,ab)));
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    float vb = d5*d2 - d1*d6;
                    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                        float dist = glm::length(ap - ac*(d2/dot(ac,ac)));
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    float va = d3*d6 - d5*d4;
                    if (va <= 0.0f && (d4-d3) >= 0.0f && (d5-d6) >= 0.0f) {
                        float dist = glm::length(bp - (c-b)*((d4-d3)/glm::length(c-b)));
                        if (dist < minDist) { minDist = dist; bestTri = i; }
                        continue;
                    }
                    glm::vec3 n = glm::cross(ab, ac);
                    float dist = glm::abs(glm::dot(n, ap)) / glm::length(n);
                    if (dist < minDist) { minDist = dist; bestTri = i; }
                }
                int idx = (z * sdfRes * sdfRes + y * sdfRes + x);
                sdfVol[idx] = minDist;
                if (bestTri >= 0) {
                    const float* col = &globalColData[bestTri * 12];
                    colVol[idx*4+0] = (unsigned char)(glm::clamp(col[0], 0.0f, 1.0f) * 255);
                    colVol[idx*4+1] = (unsigned char)(glm::clamp(col[1], 0.0f, 1.0f) * 255);
                    colVol[idx*4+2] = (unsigned char)(glm::clamp(col[2], 0.0f, 1.0f) * 255);
                    colVol[idx*4+3] = (unsigned char)(glm::clamp(col[3], 0.0f, 1.0f) * 255);

                    const float* v0 = &globalPosData[bestTri * 9 + 0];
                    const float* v1 = &globalPosData[bestTri * 9 + 3];
                    const float* v2 = &globalPosData[bestTri * 9 + 6];
                    glm::vec3 a(v0[0], v0[1], v0[2]);
                    glm::vec3 b(v1[0], v1[1], v1[2]);
                    glm::vec3 c(v2[0], v2[1], v2[2]);
                    glm::vec3 bary = glm::vec3(0.0f);
                    {
                        glm::vec3 ab = b - a, ac = c - a, ap = voxelCenter - a;
                        float d00 = dot(ab, ab);
                        float d01 = dot(ab, ac);
                        float d11 = dot(ac, ac);
                        float d20 = dot(ap, ab);
                        float d21 = dot(ap, ac);
                        float denom = d00 * d11 - d01 * d01;
                        if (denom > 1e-8f) {
                            bary.y = (d11 * d20 - d01 * d21) / denom;
                            bary.z = (d00 * d21 - d01 * d20) / denom;
                            bary.x = 1.0f - bary.y - bary.z;
                        }
                    }
                    const float* uv0 = &globalUVData[bestTri * 6 + 0];
                    const float* uv1 = &globalUVData[bestTri * 6 + 2];
                    const float* uv2 = &globalUVData[bestTri * 6 + 4];
                    float u = bary.x * uv0[0] + bary.y * uv1[0] + bary.z * uv2[0];
                    float v = bary.x * uv0[1] + bary.y * uv1[1] + bary.z * uv2[1];
                    uvVol[idx*2+0] = u;
                    uvVol[idx*2+1] = v;

                    const float* idxArr = &globalIdxData[bestTri * 12];
                    float atlasIdx = idxArr[2];
                    atlasVol[idx] = atlasIdx;
                }
            }
        }
    }

    glBindTexture(GL_TEXTURE_3D, sdfTex3D);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, sdfRes, sdfRes, sdfRes, GL_RED, GL_FLOAT, sdfVol.data());
    glBindTexture(GL_TEXTURE_3D, sdfColorTex3D);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, sdfRes, sdfRes, sdfRes, GL_RGBA, GL_UNSIGNED_BYTE, colVol.data());
    glBindTexture(GL_TEXTURE_3D, sdfUVTex3D);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, sdfRes, sdfRes, sdfRes, GL_RG, GL_FLOAT, uvVol.data());
    glBindTexture(GL_TEXTURE_3D, sdfAtlasTex3D);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, sdfRes, sdfRes, sdfRes, GL_RED, GL_FLOAT, atlasVol.data());
}

int loadTextureToAtlas(const char* filename) {
    if (g_atlasInfos.empty()) {
        int tileSizePad = g_tileSize + 2 * Engine_settings.ATLAS_PADDING;
        std::vector<unsigned char> whiteData(tileSizePad * tileSizePad * 4, 255);
        int tilePerSide = g_atlasSide / tileSizePad;
        glGenTextures(1, &g_atlasTextures[0]);
        glBindTexture(GL_TEXTURE_2D, g_atlasTextures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_atlasSide, g_atlasSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tileSizePad, tileSizePad, GL_RGBA, GL_UNSIGNED_BYTE, whiteData.data());
        g_atlasCount = 1;
        AtlasTextureInfo info;
        info.atlasIndex = 0;
        info.tileX = 0;
        info.tileY = 0;
        info.scaleX = 1.0f;
        info.scaleY = 1.0f;
        info.offsetX = 0.0f;
        info.offsetY = 0.0f;
        info.origWidth = 1;
        info.origHeight = 1;
        g_atlasInfos.push_back(info);
        g_textureNameToIndex["__white"] = 0;
    }

    if (!filename || strlen(filename) == 0) return 0;

    std::string fname(filename);
    auto it = g_textureNameToIndex.find(fname);
    if (it != g_textureNameToIndex.end()) return it->second;

    int w, h, comp;
    unsigned char* data = SOIL_load_image(filename, &w, &h, &comp, 4);
    if (!data) return 0;

    float aspect = (float)w / h;
    float scaleX = 1.0f, scaleY = 1.0f, offX = 0.0f, offY = 0.0f;
    if (aspect > 1.0f) {
        scaleY = 1.0f / aspect;
        offY = (1.0f - scaleY) * 0.5f;
    } else {
        scaleX = aspect;
        offX = (1.0f - scaleX) * 0.5f;
    }

    int tileSize = g_tileSize;
    int tileSizePad = tileSize + 2 * Engine_settings.ATLAS_PADDING;
    std::vector<unsigned char> squareData(tileSizePad * tileSizePad * 4, 0);

    for (int y = 0; y < tileSizePad; ++y) {
        for (int x = 0; x < tileSizePad; ++x) {
            int srcX = x - Engine_settings.ATLAS_PADDING;
            int srcY = y - Engine_settings.ATLAS_PADDING;
            if (srcX < 0) srcX = 0;
            if (srcX >= tileSize) srcX = tileSize - 1;
            if (srcY < 0) srcY = 0;
            if (srcY >= tileSize) srcY = tileSize - 1;

            float u = (srcX / (float)tileSize - offX) / scaleX;
            float v = (srcY / (float)tileSize - offY) / scaleY;
            v = 1.0f - v;
            if (u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f) {
                int sx = (int)(u * w);
                int sy = (int)(v * h);
                sx = std::max(0, std::min(sx, w-1));
                sy = std::max(0, std::min(sy, h-1));
                memcpy(&squareData[(y*tileSizePad + x)*4], &data[(sy*w + sx)*4], 4);
            }
        }
    }
    SOIL_free_image_data(data);

    int tilePerSide = g_atlasSide / tileSizePad;
    int maxTiles = tilePerSide * tilePerSide;
    int targetAtlas = -1, targetTile = -1;

    for (int a = 0; a < g_atlasCount; ++a) {
        int used = 0;
        for (auto& info : g_atlasInfos) {
            if (info.atlasIndex == a) used++;
        }
        if (used < maxTiles) {
            targetAtlas = a;
            std::vector<bool> taken(maxTiles, false);
            for (auto& info : g_atlasInfos) {
                if (info.atlasIndex == a) {
                    int idx = info.tileY * tilePerSide + info.tileX;
                    taken[idx] = true;
                }
            }
            for (int i = 0; i < maxTiles; ++i) {
                if (!taken[i]) {
                    targetTile = i;
                    break;
                }
            }
            if (targetTile != -1) break;
        }
    }

    if (targetAtlas == -1) {
        if (g_atlasCount >= 8) {
            targetAtlas = 0;
            targetTile = 0;
        } else {
            targetAtlas = g_atlasCount;
            glGenTextures(1, &g_atlasTextures[targetAtlas]);
            glBindTexture(GL_TEXTURE_2D, g_atlasTextures[targetAtlas]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_atlasSide, g_atlasSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g_atlasCount++;
            targetTile = 0;
        }
    }

    int tileX = targetTile % tilePerSide;
    int tileY = targetTile / tilePerSide;

    glBindTexture(GL_TEXTURE_2D, g_atlasTextures[targetAtlas]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, tileX * tileSizePad, tileY * tileSizePad,
                    tileSizePad, tileSizePad, GL_RGBA, GL_UNSIGNED_BYTE, squareData.data());

    AtlasTextureInfo info;
    info.atlasIndex = targetAtlas;
    info.tileX = tileX;
    info.tileY = tileY;
    info.scaleX = scaleX;
    info.scaleY = scaleY;
    info.offsetX = offX;
    info.offsetY = offY;
    info.origWidth = w;
    info.origHeight = h;

    int idx = (int)g_atlasInfos.size();
    g_atlasInfos.push_back(info);
    g_textureNameToIndex[fname] = idx;
    return idx;
}

void convertUVtoAtlas(float u, float v, int texIdx, float& outU, float& outV) {
    if (texIdx < 0 || texIdx >= (int)g_atlasInfos.size()) {
        outU = u; outV = v; return;
    }
    const AtlasTextureInfo& info = g_atlasInfos[texIdx];
    float cellU = info.offsetX + u * info.scaleX;
    float cellV = info.offsetY + v * info.scaleY;
    int tileSizePad = g_tileSize + 2 * Engine_settings.ATLAS_PADDING;
    float atlasSize = (float)g_atlasSide;
    float halfPixel = 0.5f / atlasSize;
    outU = (info.tileX * tileSizePad + Engine_settings.ATLAS_PADDING + cellU * g_tileSize) / atlasSize + halfPixel;
    outV = (info.tileY * tileSizePad + Engine_settings.ATLAS_PADDING + (1.0f - cellV) * g_tileSize) / atlasSize + halfPixel;
}

void flush_RC_DrawQueue() {
    applyAllLights();

    if (is_scene_changed && current_scene) {
        std::vector<DrawCommand> tempQueue;
        drawQueue.swap(tempQueue);
        current_scene();
        std::vector<DrawCommand> staticCommands;
        staticCommands.swap(drawQueue);
        drawQueue.swap(tempQueue);

        globalPosData.clear();
        globalColData.clear();
        globalUVData.clear();
        globalIdxData.clear();
        globalTriCount = 0;
        for (auto& cmd : staticCommands) {
            if (cmd.type == CMD_3DOBJECT) {
                if (cmd.obj_indices.size() < 3) continue;
                const auto& verts = cmd.obj_vertices;
                const auto& uvs = cmd.obj_texcoords;
                const auto& idxs = cmd.obj_indices;
                float cx = cmd.obj_cx, cy = cmd.obj_cy, cz = cmd.obj_cz;
                int ti = 0;
                if (!cmd.obj_tex.empty()) ti = loadTextureToAtlas(cmd.obj_tex.c_str());
                int atlasIdx = (ti >= 0 && ti < (int)g_atlasInfos.size()) ? g_atlasInfos[ti].atlasIndex : 0;
                float yaw = cmd.obj_yaw * M_PI / 180.0f;
                float pitch = cmd.obj_pitch * M_PI / 180.0f;
                float roll = cmd.obj_roll * M_PI / 180.0f;
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, yaw, glm::vec3(0,1,0));
                rotMat = glm::rotate(rotMat, pitch, glm::vec3(1,0,0));
                rotMat = glm::rotate(rotMat, roll, glm::vec3(0,0,1));
                for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
                    int i0 = idxs[i]*3, i1 = idxs[i+1]*3, i2 = idxs[i+2]*3;
                    glm::vec4 v0(verts[i0], verts[i0+1], verts[i0+2], 1.0f);
                    glm::vec4 v1(verts[i1], verts[i1+1], verts[i1+2], 1.0f);
                    glm::vec4 v2(verts[i2], verts[i2+1], verts[i2+2], 1.0f);
                    v0 = rotMat * v0; v1 = rotMat * v1; v2 = rotMat * v2;
                    globalPosData.push_back(v0.x + cx); globalPosData.push_back(v0.y + cy); globalPosData.push_back(v0.z + cz);
                    globalPosData.push_back(v1.x + cx); globalPosData.push_back(v1.y + cy); globalPosData.push_back(v1.z + cz);
                    globalPosData.push_back(v2.x + cx); globalPosData.push_back(v2.y + cy); globalPosData.push_back(v2.z + cz);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    float u0 = 0, vv0 = 0, u1 = 0, vv1 = 0, u2 = 0, vv2 = 0;
                    if (i0/2+1 < (int)uvs.size()) { u0 = uvs[i0/3*2]; vv0 = uvs[i0/3*2+1]; }
                    if (i1/2+1 < (int)uvs.size()) { u1 = uvs[i1/3*2]; vv1 = uvs[i1/3*2+1]; }
                    if (i2/2+1 < (int)uvs.size()) { u2 = uvs[i2/3*2]; vv2 = uvs[i2/3*2+1]; }
                    float newU0, newV0, newU1, newV1, newU2, newV2;
                    convertUVtoAtlas(u0, vv0, ti, newU0, newV0);
                    convertUVtoAtlas(u1, vv1, ti, newU1, newV1);
                    convertUVtoAtlas(u2, vv2, ti, newU2, newV2);
                    globalUVData.push_back(newU0); globalUVData.push_back(newV0);
                    globalUVData.push_back(newU1); globalUVData.push_back(newV1);
                    globalUVData.push_back(newU2); globalUVData.push_back(newV2);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalTriCount++;
                }
            }
        }
        sdfNeedsUpdate = true;
        is_scene_changed = 0;
    }

    std::vector<DrawCommand> commands2D;
    std::vector<DrawCommand> commands3D;
    std::vector<Portal*> portalCommands;
    bool hasPortalCommand = false;
    for (auto& cmd : drawQueue) {
        switch (cmd.type) {
            case CMD_PORTAL: hasPortalCommand = true; portalCommands.push_back(cmd.portal); break;
            case CMD_SQUARE: case CMD_LINE_2D: commands2D.push_back(cmd); break;
            default: commands3D.push_back(cmd); break;
        }
    }
    drawQueue.clear();

    if (!commands3D.empty()) {
        for (auto& cmd : commands3D) {
            if (cmd.type == CMD_3DOBJECT) {
                if (cmd.obj_indices.size() < 3) continue;
                const auto& verts = cmd.obj_vertices;
                const auto& uvs = cmd.obj_texcoords;
                const auto& idxs = cmd.obj_indices;
                float cx = cmd.obj_cx, cy = cmd.obj_cy, cz = cmd.obj_cz;
                int ti = 0;
                if (!cmd.obj_tex.empty()) ti = loadTextureToAtlas(cmd.obj_tex.c_str());
                int atlasIdx = (ti >= 0 && ti < (int)g_atlasInfos.size()) ? g_atlasInfos[ti].atlasIndex : 0;
                float yaw = cmd.obj_yaw * M_PI / 180.0f;
                float pitch = cmd.obj_pitch * M_PI / 180.0f;
                float roll = cmd.obj_roll * M_PI / 180.0f;
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, yaw, glm::vec3(0,1,0));
                rotMat = glm::rotate(rotMat, pitch, glm::vec3(1,0,0));
                rotMat = glm::rotate(rotMat, roll, glm::vec3(0,0,1));
                for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
                    int i0 = idxs[i]*3, i1 = idxs[i+1]*3, i2 = idxs[i+2]*3;
                    glm::vec4 v0(verts[i0], verts[i0+1], verts[i0+2], 1.0f);
                    glm::vec4 v1(verts[i1], verts[i1+1], verts[i1+2], 1.0f);
                    glm::vec4 v2(verts[i2], verts[i2+1], verts[i2+2], 1.0f);
                    v0 = rotMat * v0; v1 = rotMat * v1; v2 = rotMat * v2;
                    globalPosData.push_back(v0.x + cx); globalPosData.push_back(v0.y + cy); globalPosData.push_back(v0.z + cz);
                    globalPosData.push_back(v1.x + cx); globalPosData.push_back(v1.y + cy); globalPosData.push_back(v1.z + cz);
                    globalPosData.push_back(v2.x + cx); globalPosData.push_back(v2.y + cy); globalPosData.push_back(v2.z + cz);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    globalColData.push_back(cmd.obj_r); globalColData.push_back(cmd.obj_g); globalColData.push_back(cmd.obj_b); globalColData.push_back(cmd.obj_alpha);
                    float u0 = 0, vv0 = 0, u1 = 0, vv1 = 0, u2 = 0, vv2 = 0;
                    if (i0/2+1 < (int)uvs.size()) { u0 = uvs[i0/3*2]; vv0 = uvs[i0/3*2+1]; }
                    if (i1/2+1 < (int)uvs.size()) { u1 = uvs[i1/3*2]; vv1 = uvs[i1/3*2+1]; }
                    if (i2/2+1 < (int)uvs.size()) { u2 = uvs[i2/3*2]; vv2 = uvs[i2/3*2+1]; }
                    float newU0, newV0, newU1, newV1, newU2, newV2;
                    convertUVtoAtlas(u0, vv0, ti, newU0, newV0);
                    convertUVtoAtlas(u1, vv1, ti, newU1, newV1);
                    convertUVtoAtlas(u2, vv2, ti, newU2, newV2);
                    globalUVData.push_back(newU0); globalUVData.push_back(newV0);
                    globalUVData.push_back(newU1); globalUVData.push_back(newV1);
                    globalUVData.push_back(newU2); globalUVData.push_back(newV2);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalIdxData.push_back(0); globalIdxData.push_back(1.0f); globalIdxData.push_back((float)atlasIdx); globalIdxData.push_back(0.0f);
                    globalTriCount++;
                }
            }
        }
        sdfNeedsUpdate = true;
    }

    if (sdfNeedsUpdate && globalTriCount > 0) {
        sdfBBoxMin = glm::vec3(FLT_MAX);
        sdfBBoxMax = glm::vec3(-FLT_MAX);
        for (size_t i = 0; i < globalPosData.size(); i += 3) {
            glm::vec3 v(globalPosData[i], globalPosData[i+1], globalPosData[i+2]);
            sdfBBoxMin = glm::min(sdfBBoxMin, v);
            sdfBBoxMax = glm::max(sdfBBoxMax, v);
        }
        glm::vec3 camPosVec(camera.eye_x, camera.eye_y, camera.eye_z);
        sdfBBoxMin = glm::min(sdfBBoxMin, camPosVec - glm::vec3(1.0f));
        sdfBBoxMax = glm::max(sdfBBoxMax, camPosVec + glm::vec3(1.0f));
        glm::vec3 margin = (sdfBBoxMax - sdfBBoxMin) * 0.1f + glm::vec3(0.5f);
        sdfBBoxMin -= margin;
        sdfBBoxMax += margin;
        generateSDFTexture();
        sdfNeedsUpdate = false;
    }

    float mult = Engine_settings.RAY_MULTIPLY;
    int renderW, renderH;
    int raySamples;
    if (mult >= 1.0f) {
        renderW = window_w;
        renderH = window_h;
        raySamples = (int)mult;
    } else {
        float factor = sqrtf(mult);
        renderW = std::max(1, (int)(window_w * factor));
        renderH = std::max(1, (int)(window_h * factor));
        raySamples = 1;
    }

    static GLuint fbo = 0, fboTex = 0;
    static int lastRenderW = 0, lastRenderH = 0;
    if (fbo == 0 || renderW != lastRenderW || renderH != lastRenderH) {
        if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &fboTex); fbo = 0; fboTex = 0; }
        if (renderW != window_w || renderH != window_h) {
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &fboTex);
            glBindTexture(GL_TEXTURE_2D, fboTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, renderW, renderH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &fboTex); fbo = 0; fboTex = 0;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            lastRenderW = renderW;
            lastRenderH = renderH;
        } else {
            fbo = 0; fboTex = 0;
            lastRenderW = renderW;
            lastRenderH = renderH;
        }
    }

    static GLint loc_raycast = -1, loc_invViewProj = -1, loc_camPos = -1;
    static GLint loc_ambient = -1, loc_fogColor = -1, loc_fogStart = -1, loc_fogEnd = -1;
    static GLint loc_numLights = -1;
    static GLint loc_portalCount = -1, loc_portalPos = -1, loc_portalNormal = -1, loc_portalD = -1;
    static GLint loc_portalInvWorld = -1, loc_portalVertCount = -1, loc_portalVerts = -1, loc_portalTeleport = -1;
    static std::vector<GLint> locLightEnabled;
    static std::vector<GLint> locLightPosition;
    static std::vector<GLint> locLightDirection;
    static std::vector<GLint> locLightDiffuse;
    static std::vector<GLint> locLightCutoff;
    static std::vector<GLint> locLightAttenuation;
    static GLint loc_sdfVolume = -1, loc_sdfColorTex = -1, loc_sdfUVTex = -1, loc_sdfAtlasTex = -1;
    static GLint loc_sdfBBoxMin = -1, loc_sdfBBoxMax = -1, loc_sdfEpsilon = -1, loc_sdfMaxSteps = -1;
    static GLint loc_warpEnabled = -1, loc_warpOrigin = -1, loc_warpAxisU = -1, loc_warpAxisV = -1, loc_warpDisplacementTex = -1;
    static GLint loc_maxDist = -1, loc_shadowBias = -1, loc_camWarpStrength = -1;
    static GLint loc_shadowWarpStrength = -1, loc_camStepSize = -1, loc_shadowStepSize = -1;
    static GLint loc_maxBounces = -1, loc_maxShadowBounces = -1;
    static GLint loc_raySamples = -1;
    static GLint loc_debugMode = -1;
    static GLint loc_debugColor = -1;
    static bool uniformsCached = false;
    if (!uniformsCached && currentShaderProg) {
        loc_raycast = glGetUniformLocation(currentShaderProg, "raycast");
        loc_invViewProj = glGetUniformLocation(currentShaderProg, "invViewProj");
        loc_camPos = glGetUniformLocation(currentShaderProg, "camPos");
        loc_ambient = glGetUniformLocation(currentShaderProg, "ambientLight");
        loc_fogColor = glGetUniformLocation(currentShaderProg, "fogColor");
        loc_fogStart = glGetUniformLocation(currentShaderProg, "fogStart");
        loc_fogEnd = glGetUniformLocation(currentShaderProg, "fogEnd");
        loc_numLights = glGetUniformLocation(currentShaderProg, "numLights");
        loc_portalCount = glGetUniformLocation(currentShaderProg, "portalCount");
        loc_portalPos = glGetUniformLocation(currentShaderProg, "portalPos");
        loc_portalNormal = glGetUniformLocation(currentShaderProg, "portalNormal");
        loc_portalD = glGetUniformLocation(currentShaderProg, "portalD");
        loc_portalInvWorld = glGetUniformLocation(currentShaderProg, "portalInvWorld");
        loc_portalVertCount = glGetUniformLocation(currentShaderProg, "portalVertCount");
        loc_portalVerts = glGetUniformLocation(currentShaderProg, "portalVerts");
        loc_portalTeleport = glGetUniformLocation(currentShaderProg, "portalTeleport");
        int maxLights = Engine_settings.MAX_LIGHTS;
        locLightEnabled.resize(maxLights, -1);
        locLightPosition.resize(maxLights, -1);
        locLightDirection.resize(maxLights, -1);
        locLightDiffuse.resize(maxLights, -1);
        locLightCutoff.resize(maxLights, -1);
        locLightAttenuation.resize(maxLights, -1);
        for (int i = 0; i < maxLights; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "lights[%d].enabled", i);
            locLightEnabled[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].position", i);
            locLightPosition[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].direction", i);
            locLightDirection[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].diffuse", i);
            locLightDiffuse[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].cutoff", i);
            locLightCutoff[i] = glGetUniformLocation(currentShaderProg, buf);
            snprintf(buf, sizeof(buf), "lights[%d].attenuation", i);
            locLightAttenuation[i] = glGetUniformLocation(currentShaderProg, buf);
        }
        loc_sdfVolume = glGetUniformLocation(currentShaderProg, "sdfVolume");
        loc_sdfColorTex = glGetUniformLocation(currentShaderProg, "sdfColorTex");
        loc_sdfUVTex = glGetUniformLocation(currentShaderProg, "sdfUVTex");
        loc_sdfAtlasTex = glGetUniformLocation(currentShaderProg, "sdfAtlasTex");
        loc_sdfBBoxMin = glGetUniformLocation(currentShaderProg, "sdfBBoxMin");
        loc_sdfBBoxMax = glGetUniformLocation(currentShaderProg, "sdfBBoxMax");
        loc_sdfEpsilon = glGetUniformLocation(currentShaderProg, "sdfEpsilon");
        loc_sdfMaxSteps = glGetUniformLocation(currentShaderProg, "sdfMaxSteps");
        loc_warpEnabled = glGetUniformLocation(currentShaderProg, "warpPlaneEnabled");
        loc_warpOrigin = glGetUniformLocation(currentShaderProg, "warpPlaneOrigin");
        loc_warpAxisU = glGetUniformLocation(currentShaderProg, "warpPlaneAxisU");
        loc_warpAxisV = glGetUniformLocation(currentShaderProg, "warpPlaneAxisV");
        loc_warpDisplacementTex = glGetUniformLocation(currentShaderProg, "warpPlaneDisplacementTex");
        loc_maxDist = glGetUniformLocation(currentShaderProg, "maxDist");
        loc_shadowBias = glGetUniformLocation(currentShaderProg, "shadowBias");
        loc_camWarpStrength = glGetUniformLocation(currentShaderProg, "camWarpStrength");
        loc_shadowWarpStrength = glGetUniformLocation(currentShaderProg, "shadowWarpStrength");
        loc_camStepSize = glGetUniformLocation(currentShaderProg, "camStepSize");
        loc_shadowStepSize = glGetUniformLocation(currentShaderProg, "shadowStepSize");
        loc_maxBounces = glGetUniformLocation(currentShaderProg, "maxBounces");
        loc_maxShadowBounces = glGetUniformLocation(currentShaderProg, "maxShadowBounces");
        loc_raySamples = glGetUniformLocation(currentShaderProg, "raySamples");
        loc_debugMode = glGetUniformLocation(currentShaderProg, "debugMode");
        loc_debugColor = glGetUniformLocation(currentShaderProg, "debugColor");
        uniformsCached = true;
    }

    bool useFBO = (fbo != 0 && fboTex != 0);
    if (useFBO) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, renderW, renderH);
    } else {
        glViewport(0, 0, window_w, window_h);
    }

    if (globalTriCount > 0) {
        glm::mat4 prevProj = g_projectionMatrix;
        glm::mat4 prevModel = g_modelViewMatrix;
        g_projectionMatrix = glm::mat4(1.0f);
        g_modelViewMatrix = glm::mat4(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        useShader(currentShaderProg);
        if (currentShaderProg) updateMatrixUniforms();
        glUniform1i(loc_raycast, 1);
        glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), (float)renderW/(float)renderH, camera.znear, camera.zfar);
        glm::mat4 viewMat = glm::lookAt(glm::vec3(camera.eye_x, camera.eye_y, camera.eye_z),
                                        glm::vec3(camera.ctr_x, camera.ctr_y, camera.ctr_z),
                                        glm::vec3(camera.up_x, camera.up_y, camera.up_z));
        glm::mat4 invVP = glm::inverse(projMat * viewMat);
        glUniformMatrix4fv(loc_invViewProj, 1, GL_FALSE, &invVP[0][0]);
        glUniform3f(loc_camPos, camera.eye_x, camera.eye_y, camera.eye_z);
        glUniform3fv(loc_ambient, 1, global_ambient);
        glUniform3f(loc_fogColor, fog.color[0], fog.color[1], fog.color[2]);
        glUniform1f(loc_fogStart, fog.start);
        glUniform1f(loc_fogEnd, fog.end);
        int nLights = std::min((int)activeLights.size(), (int)locLightEnabled.size());
        glUniform1i(loc_numLights, nLights);
        for (int i = 0; i < nLights; i++) {
            Light* l = activeLights[i];
            glUniform1i(locLightEnabled[i], 1);
            glUniform3f(locLightPosition[i], l->pos[0], l->pos[1], l->pos[2]);
            glUniform3f(locLightDirection[i], l->dir[0], l->dir[1], l->dir[2]);
            glUniform3f(locLightDiffuse[i], l->color[0]*l->intensity, l->color[1]*l->intensity, l->color[2]*l->intensity);
            glUniform1f(locLightCutoff[i], cosf(l->cutoff * M_PI / 180.0f));
            glUniform3f(locLightAttenuation[i], l->constAtt, l->linearAtt, l->quadAtt);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, sdfTex3D);
        glUniform1i(loc_sdfVolume, 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, sdfColorTex3D);
        glUniform1i(loc_sdfColorTex, 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, sdfUVTex3D);
        glUniform1i(loc_sdfUVTex, 3);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_3D, sdfAtlasTex3D);
        glUniform1i(loc_sdfAtlasTex, 4);
        for (int i = 0; i < g_atlasCount; ++i) {
            glActiveTexture(GL_TEXTURE7 + i);
            glBindTexture(GL_TEXTURE_2D, g_atlasTextures[i]);
            glUniform1i(locAtlasSlot[i], 7 + i);
        }
        for (int i = g_atlasCount; i < 8; ++i) glUniform1i(locAtlasSlot[i], 0);
        glUniform3fv(loc_sdfBBoxMin, 1, &sdfBBoxMin[0]);
        glUniform3fv(loc_sdfBBoxMax, 1, &sdfBBoxMax[0]);
        glUniform1f(loc_sdfEpsilon, (sdfBBoxMax.x - sdfBBoxMin.x) / sdfRes * 0.5f);
        glUniform1i(loc_sdfMaxSteps, 256);

        std::vector<float> portalPos(Engine_settings.MAX_PORTALS * 3, 0.0f);
        std::vector<float> portalNormal(Engine_settings.MAX_PORTALS * 3, 0.0f);
        std::vector<float> portalD(Engine_settings.MAX_PORTALS, 0.0f);
        std::vector<float> portalInvWorld(Engine_settings.MAX_PORTALS * 16, 0.0f);
        std::vector<int>   portalVertCount(Engine_settings.MAX_PORTALS, 0);
        std::vector<float> portalVerts(Engine_settings.MAX_PORTALS * Engine_settings.MAX_PORTAL_VERTS * 2, 0.0f);
        std::vector<float> portalTeleport(Engine_settings.MAX_PORTALS * 16, 0.0f);
        int portalCount = 0;
        if (hasPortalCommand) {
            std::vector<Portal*> uniquePortals;
            for (Portal* p : portalCommands) {
                if (std::find(uniquePortals.begin(), uniquePortals.end(), p) == uniquePortals.end()) {
                    uniquePortals.push_back(p);
                }
            }
            for (Portal* p : uniquePortals) {
                if (portalCount >= Engine_settings.MAX_PORTALS) break;
                auto addPortalSide = [&](float px, float py, float pz, float yaw, float pitch, float roll,
                                         float nx, float ny, float nz, const glm::mat4& teleport) {
                    portalPos[portalCount*3+0] = px; portalPos[portalCount*3+1] = py; portalPos[portalCount*3+2] = pz;
                    portalNormal[portalCount*3+0] = nx; portalNormal[portalCount*3+1] = ny; portalNormal[portalCount*3+2] = nz;
                    portalD[portalCount] = -(nx*px + ny*py + nz*pz);
                    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0,1,0));
                    rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1,0,0));
                    rot = glm::rotate(rot, glm::radians(roll), glm::vec3(0,0,1));
                    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(px,py,pz)) * rot;
                    glm::mat4 invWorld = glm::inverse(world);
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                        portalInvWorld[portalCount*16 + c*4 + r] = invWorld[c][r];
                    int vCount = (int)p->vertices.size() / 3;
                    portalVertCount[portalCount] = vCount;
                    for (int i = 0; i < vCount && i < Engine_settings.MAX_PORTAL_VERTS; ++i) {
                        portalVerts[portalCount * Engine_settings.MAX_PORTAL_VERTS * 2 + i*2 + 0] = p->vertices[i*3 + 0];
                        portalVerts[portalCount * Engine_settings.MAX_PORTAL_VERTS * 2 + i*2 + 1] = p->vertices[i*3 + 1];
                    }
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                        portalTeleport[portalCount*16 + c*4 + r] = teleport[c][r];
                    portalCount++;
                };
                glm::vec3 nA = p->portalNormal(p->ax, p->ay, p->az, false);
                glm::mat4 teleAtoB = p->getPortalTransform(p->ax, p->ay, p->az, p->bx, p->by, p->bz);
                addPortalSide(p->ax, p->ay, p->az, p->yawA, p->pitchA, p->rollA, nA.x, nA.y, nA.z, teleAtoB);
                if (portalCount < Engine_settings.MAX_PORTALS) {
                    glm::vec3 nB = p->portalNormal(p->bx, p->by, p->bz, true);
                    glm::mat4 teleBtoA = p->getPortalTransform(p->bx, p->by, p->bz, p->ax, p->ay, p->az);
                    addPortalSide(p->bx, p->by, p->bz, p->yawB, p->pitchB, p->rollB, nB.x, nB.y, nB.z, teleBtoA);
                }
            }
        }
        glUniform1i(loc_portalCount, portalCount);
        if (portalCount > 0) {
            glUniform3fv(loc_portalPos, portalCount, portalPos.data());
            glUniform3fv(loc_portalNormal, portalCount, portalNormal.data());
            glUniform1fv(loc_portalD, portalCount, portalD.data());
            glUniformMatrix4fv(loc_portalInvWorld, portalCount, GL_FALSE, portalInvWorld.data());
            glUniform1iv(loc_portalVertCount, portalCount, portalVertCount.data());
            glUniform2fv(loc_portalVerts, Engine_settings.MAX_PORTALS * Engine_settings.MAX_PORTAL_VERTS, portalVerts.data());
            glUniformMatrix4fv(loc_portalTeleport, portalCount, GL_FALSE, portalTeleport.data());
        }
        if (activeWarpPlane && activeWarpPlane->enabled) {
            glUniform1i(loc_warpEnabled, 1);
            glUniform3f(loc_warpOrigin, activeWarpPlane->originX, activeWarpPlane->originY, activeWarpPlane->originZ);
            glm::mat4 rot = glm::mat4(1.0f);
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->yaw), glm::vec3(0,1,0));
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->pitch), glm::vec3(1,0,0));
            rot = glm::rotate(rot, glm::radians(activeWarpPlane->roll), glm::vec3(0,0,1));
            glm::vec3 uAxis = glm::vec3(rot * glm::vec4(activeWarpPlane->sizeU, 0, 0, 0));
            glm::vec3 vAxis = glm::vec3(rot * glm::vec4(0, activeWarpPlane->sizeV, 0, 0));
            glUniform3fv(loc_warpAxisU, 1, &uAxis[0]);
            glUniform3fv(loc_warpAxisV, 1, &vAxis[0]);
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_2D, activeWarpPlane->displacementTex);
            glUniform1i(loc_warpDisplacementTex, 11);
        } else {
            glUniform1i(loc_warpEnabled, 0);
        }
        glUniform1f(loc_maxDist, Engine_settings.MAX_DIST);
        glUniform1f(loc_shadowBias, Engine_settings.SHADOW_BIAS);
        glUniform1f(loc_camWarpStrength, Engine_settings.CAM_WARP_STRENGTH);
        glUniform1f(loc_shadowWarpStrength, Engine_settings.SHADOW_WARP_STRENGTH);
        glUniform1f(loc_camStepSize, Engine_settings.CAM_STEP_SIZE);
        glUniform1f(loc_shadowStepSize, Engine_settings.SHADOW_STEP_SIZE);
        glUniform1i(loc_maxBounces, Engine_settings.MAX_BOUNCES);
        glUniform1i(loc_maxShadowBounces, Engine_settings.MAX_SHADOW_BOUNCES);
        glUniform1i(loc_raySamples, raySamples);
        glUniform1i(loc_debugMode, Engine_settings.DEBUG_GRAPHICS ? 1 : 0);
        glUniform3f(loc_debugColor, Engine_settings.DEBUG_COLOR[0],
                                   Engine_settings.DEBUG_COLOR[1],
                                   Engine_settings.DEBUG_COLOR[2]);
        glActiveTexture(GL_TEXTURE0);
        static GLuint fullScreenVAO = 0, fullScreenVBO = 0;
        if (fullScreenVAO == 0) {
            float quad[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
            glGenVertexArrays(1, &fullScreenVAO);
            glGenBuffers(1, &fullScreenVBO);
            glBindVertexArray(fullScreenVAO);
            glBindBuffer(GL_ARRAY_BUFFER, fullScreenVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glBindVertexArray(0);
        }
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glBindVertexArray(fullScreenVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
        g_projectionMatrix = prevProj;
        g_modelViewMatrix = prevModel;
        if (currentShaderProg) updateMatrixUniforms();
    }

    if (Engine_settings.DEBUG_GRAPHICS && globalTriCount > 0) {
        initDebugPointShader();
        glViewport(0, 0, window_w, window_h);
        glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), (float)window_w/(float)window_h, camera.znear, camera.zfar);
        glm::mat4 viewMat = glm::lookAt(glm::vec3(camera.eye_x, camera.eye_y, camera.eye_z),
                                        glm::vec3(camera.ctr_x, camera.ctr_y, camera.ctr_z),
                                        glm::vec3(camera.up_x, camera.up_y, camera.up_z));
        glm::mat4 modelViewMat = viewMat * glm::mat4(1.0f);
        glUseProgram(debugPointShader);
        glUniformMatrix4fv(loc_debugPoint_proj, 1, GL_FALSE, &projMat[0][0]);
        glUniformMatrix4fv(loc_debugPoint_modelView, 1, GL_FALSE, &modelViewMat[0][0]);
        glUniform4f(glGetUniformLocation(debugPointShader, "color"),
                    Engine_settings.DEBUG_COLOR[0], Engine_settings.DEBUG_COLOR[1], Engine_settings.DEBUG_COLOR[2], 1.0f);
        static GLuint debugVBO = 0, debugVAO = 0;
        if (debugVAO == 0) {
            glGenVertexArrays(1, &debugVAO);
            glGenBuffers(1, &debugVBO);
        }
        glBindVertexArray(debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
        glBufferData(GL_ARRAY_BUFFER, globalPosData.size() * sizeof(float), globalPosData.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glPointSize(5.0f);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_POINTS, 0, globalPosData.size() / 3);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }

    if (useFBO) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_w, window_h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        useShader(current2DShader);
        if (current2DShader) {
            glUseProgram(current2DShader);
            if (loc_u_projection_2d != -1) {
                glm::mat4 proj = glm::mat4(1.0f);
                glUniformMatrix4fv(loc_u_projection_2d, 1, GL_FALSE, &proj[0][0]);
            }
            if (loc_u_modelView_2d != -1) {
                glm::mat4 mv = glm::mat4(1.0f);
                glUniformMatrix4fv(loc_u_modelView_2d, 1, GL_FALSE, &mv[0][0]);
            }
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glUniform1i(loc_tex_2d, 0);
        static GLuint blitVAO = 0, blitVBO = 0;
        if (blitVAO == 0) {
            float vertices[] = {
                -1.0f, -1.0f,   0.0f, 0.0f,
                 1.0f, -1.0f,   1.0f, 0.0f,
                -1.0f,  1.0f,   0.0f, 1.0f,
                 1.0f,  1.0f,   1.0f, 1.0f
            };
            glGenVertexArrays(1, &blitVAO);
            glGenBuffers(1, &blitVBO);
            glBindVertexArray(blitVAO);
            glBindBuffer(GL_ARRAY_BUFFER, blitVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glBindVertexArray(0);
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(blitVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    glEnable(GL_BLEND);
    if (!commands2D.empty()) {
        initSquareVAO();
        initLineVAO();
        ensureWhiteTex();
        glm::mat4 prevProj2d = g_projectionMatrix;
        glm::mat4 prevModel2d = g_modelViewMatrix;
        g_projectionMatrix = glm::ortho(0.0f, (float)window_w, 0.0f, (float)window_h, -1.0f, 1.0f);
        g_modelViewMatrix = glm::mat4(1.0f);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        useShader(current2DShader);
        if (currentShaderProg) updateMatrixUniforms();
        glUniform1i(loc_tex_2d, 0);
        for (auto& cmd : commands2D) {
            switch (cmd.type) {
                case CMD_SQUARE: {
                    const char* texName = cmd.tex.empty() ? nullptr : cmd.tex.c_str();
                    if (Engine_settings.DEBUG_GRAPHICS) {
                        glActiveTexture(GL_TEXTURE0);
                        if (texName) { GLuint id = loadTextureFromFile(texName); glBindTexture(GL_TEXTURE_2D, id ? id : whiteTex); }
                        else { glBindTexture(GL_TEXTURE_2D, whiteTex); }
                        float ar = cmd.rotate * M_PI / -180.0f;
                        float tc[8] = {0,1, 1,1, 1,0, 0,0};
                        float data[32];
                        for (int i = 0; i < 4; ++i) {
                            float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                            rotatePoint(px, py, 0, 0, ar);
                            float vx = cmd.cx + px * cmd.scale, vy = cmd.cy + py * cmd.scale;
                            data[i*8+0] = vx; data[i*8+1] = vy;
                            data[i*8+2] = cmd.r; data[i*8+3] = cmd.g; data[i*8+4] = cmd.b; data[i*8+5] = cmd.a;
                            data[i*8+6] = tc[i*2]; data[i*8+7] = tc[i*2+1];
                        }
                        glBindVertexArray(sq_vao);
                        glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                        glBindVertexArray(0);
                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, whiteTex);
                        float r = Engine_settings.DEBUG_COLOR[0];
                        float g = Engine_settings.DEBUG_COLOR[1];
                        float b = Engine_settings.DEBUG_COLOR[2];
                        float data2[32];
                        for (int i = 0; i < 4; ++i) {
                            float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                            rotatePoint(px, py, 0, 0, ar);
                            float vx = cmd.cx + px * cmd.scale, vy = cmd.cy + py * cmd.scale;
                            data2[i*8+0] = vx; data2[i*8+1] = vy;
                            data2[i*8+2] = r; data2[i*8+3] = g; data2[i*8+4] = b; data2[i*8+5] = 1.0f;
                            data2[i*8+6] = tc[i*2]; data2[i*8+7] = tc[i*2+1];
                        }
                        glBindVertexArray(sq_vao);
                        glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data2), data2);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                        glBindVertexArray(0);
                        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    } else {
                        glActiveTexture(GL_TEXTURE0);
                        if (texName) { GLuint id = loadTextureFromFile(texName); glBindTexture(GL_TEXTURE_2D, id ? id : whiteTex); }
                        else { glBindTexture(GL_TEXTURE_2D, whiteTex); }
                        float ar = cmd.rotate * M_PI / -180.0f;
                        float tc[8] = {0,1, 1,1, 1,0, 0,0};
                        float data[32];
                        for (int i = 0; i < 4; ++i) {
                            float px = cmd.verts[i*2], py = cmd.verts[i*2+1];
                            rotatePoint(px, py, 0, 0, ar);
                            float vx = cmd.cx + px * cmd.scale, vy = cmd.cy + py * cmd.scale;
                            data[i*8+0] = vx; data[i*8+1] = vy;
                            data[i*8+2] = cmd.r; data[i*8+3] = cmd.g; data[i*8+4] = cmd.b; data[i*8+5] = cmd.a;
                            data[i*8+6] = tc[i*2]; data[i*8+7] = tc[i*2+1];
                        }
                        glBindVertexArray(sq_vao);
                        glBindBuffer(GL_ARRAY_BUFFER, sq_vbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                        glBindVertexArray(0);
                    }
                    break;
                }
                case CMD_LINE_2D: {
                    float data[18] = {
                        cmd.verts[0], cmd.verts[1], 0, cmd.r, cmd.g, cmd.b, cmd.a, 0, 0,
                        cmd.verts[2], cmd.verts[3], 0, cmd.r, cmd.g, cmd.b, cmd.a, 0, 0
                    };
                    glBindVertexArray(lineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, whiteTex);
                    glLineWidth(cmd.scale);
                    glDrawArrays(GL_LINES, 0, 2);
                    glLineWidth(1.0f);
                    glBindVertexArray(0);
                    break;
                }
            }
        }
        g_projectionMatrix = prevProj2d;
        g_modelViewMatrix = prevModel2d;
        if (currentShaderProg) updateMatrixUniforms();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }
}