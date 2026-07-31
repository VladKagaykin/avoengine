#include"3d_primitives.h"
#include"avoengine.h"
#include "shaders.h"

//              3д(может быть потом ещё что-то будет)
// отрезок
void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float thickness,
                  int segments, float alpha) {
    if (segments < 3) segments = 3; 

    float wx1 = x + x1, wy1 = y + y1, wz1 = z + z1;
    float wx2 = x + x2, wy2 = y + y2, wz2 = z + z2;
    float dx = wx2 - wx1, dy = wy2 - wy1, dz = wz2 - wz1;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    float ndx = dx / len, ndy = dy / len, ndz = dz / len;

    float upx = 0, upy = 0, upz = 1;
    if (fabs(ndx) < 0.001f && fabs(ndz) < 0.001f) {
        upx = 1; upy = 0; upz = 0;
    }

    float rx = upy*ndz - upz*ndy;
    float ry = upz*ndx - upx*ndz;
    float rz = upx*ndy - upy*ndx;
    float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
    rx /= rlen; ry /= rlen; rz /= rlen;

    float ux = ndy*rz - ndz*ry;
    float uy = ndz*rx - ndx*rz;
    float uz = ndx*ry - ndy*rx;

    float half = thickness * 0.5f;

    int totalVerts = 2 * segments + 2;
    std::vector<float> vertices(totalVerts * 3);
    std::vector<float> normals(totalVerts * 3);
    std::vector<int> indices;

    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        float c = cosf(angle) * half;
        float s = sinf(angle) * half;

        vertices[i*3 + 0] = wx1 + rx * c + ux * s;
        vertices[i*3 + 1] = wy1 + ry * c + uy * s;
        vertices[i*3 + 2] = wz1 + rz * c + uz * s;

        float nx = rx * cosf(angle) + ux * sinf(angle);
        float ny = ry * cosf(angle) + uy * sinf(angle);
        float nz = rz * cosf(angle) + uz * sinf(angle);
        normals[i*3 + 0] = nx;
        normals[i*3 + 1] = ny;
        normals[i*3 + 2] = nz;
    }

    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        float c = cosf(angle) * half;
        float s = sinf(angle) * half;

        vertices[(i + segments)*3 + 0] = wx2 + rx * c + ux * s;
        vertices[(i + segments)*3 + 1] = wy2 + ry * c + uy * s;
        vertices[(i + segments)*3 + 2] = wz2 + rz * c + uz * s;

        float nx = rx * cosf(angle) + ux * sinf(angle);
        float ny = ry * cosf(angle) + uy * sinf(angle);
        float nz = rz * cosf(angle) + uz * sinf(angle);
        normals[(i + segments)*3 + 0] = nx;
        normals[(i + segments)*3 + 1] = ny;
        normals[(i + segments)*3 + 2] = nz;
    }

    int idxStartApex = 2 * segments;
    vertices[idxStartApex*3 + 0] = wx1 - ndx * half;
    vertices[idxStartApex*3 + 1] = wy1 - ndy * half;
    vertices[idxStartApex*3 + 2] = wz1 - ndz * half;
    normals[idxStartApex*3 + 0] = -ndx;
    normals[idxStartApex*3 + 1] = -ndy;
    normals[idxStartApex*3 + 2] = -ndz;

    int idxEndApex = 2 * segments + 1;
    vertices[idxEndApex*3 + 0] = wx2 + ndx * half;
    vertices[idxEndApex*3 + 1] = wy2 + ndy * half;
    vertices[idxEndApex*3 + 2] = wz2 + ndz * half;
    normals[idxEndApex*3 + 0] = ndx;
    normals[idxEndApex*3 + 1] = ndy;
    normals[idxEndApex*3 + 2] = ndz;

    for (int i = 0; i < segments; ++i) {
        int i0 = i;
        int i1 = (i + 1) % segments;
        int j0 = i + segments;
        int j1 = i1 + segments;

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(j0);

        indices.push_back(i1);
        indices.push_back(j1);
        indices.push_back(j0);
    }

    for (int i = 0; i < segments; ++i) {
        int i1 = (i + 1) % segments;
        indices.push_back(idxStartApex);
        indices.push_back(i1);
        indices.push_back(i);
    }

    for (int i = 0; i < segments; ++i) {
        int i1 = (i + 1) % segments;
        indices.push_back(idxEndApex);
        indices.push_back(i + segments);
        indices.push_back(i1 + segments);
    }

    std::vector<float> texcoords(totalVerts * 2, 0.0f);

    draw3DObject(0, 0, 0, r, g, b, nullptr, vertices, indices, texcoords, normals, 0.0f, 0.0f, 0.0f, alpha);
}
// рисуем 3д объект, указывая вершины треугольников
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw, float pitch , float roll,
                  float alpha){
    float maxDist = 0.0f;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        float dx = vertices[i] - cx;
        float dy = vertices[i+1] - cy;
        float dz = vertices[i+2] - cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > maxDist) maxDist = d2;
    }
    float radius = sqrtf(maxDist);
    DrawCommand cmd;
    cmd.type = CMD_3DOBJECT;
    cmd.obj_cx = cx; cmd.obj_cy = cy; cmd.obj_cz = cz;
    cmd.obj_r = (float)r; cmd.obj_g = (float)g; cmd.obj_b = (float)b;
    cmd.obj_alpha = alpha;
    cmd.obj_yaw = yaw; cmd.obj_pitch = pitch; cmd.obj_roll = roll;
    if (tex) cmd.obj_tex = tex;
    else cmd.obj_tex = "__white";
    cmd.obj_vertices = vertices;
    cmd.obj_indices  = indices;
    cmd.obj_texcoords = texcoords;
    cmd.obj_normals   = normals;
    cmd.radius = radius;
    cmd.shaderID = currentShaderProg;
    drawQueue.push_back(cmd);
}

// плоскость
void plane(float cx, float cy, float cz, double r, double g, double b,
           const char* tex, const std::vector<float>& vertices,float yaw, float pitch , float roll) {
    if (vertices.size() != 12) return;

    float ax = vertices[3] - vertices[0];
    float ay = vertices[4] - vertices[1];
    float az = vertices[5] - vertices[2];
    float bx = vertices[6] - vertices[0];
    float by = vertices[7] - vertices[1];
    float bz = vertices[8] - vertices[2];

    float nx = ay * bz - az * by;
    float ny = az * bx - ax * bz;
    float nz = ax * by - ay * bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }

    std::vector<int> indices = { 0, 1, 2, 0, 2, 3 };
    std::vector<float> texcoords = { 0,0, 1,0, 1,1, 0,1 };
    std::vector<float> normals = {
        nx, ny, nz,
        nx, ny, nz,
        nx, ny, nz,
        nx, ny, nz
    };

    draw3DObject(cx, cy, cz, r, g, b, tex, vertices, indices, texcoords, normals, yaw, pitch , roll);
}