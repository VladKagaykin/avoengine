#ifndef PRIMITIVES_3D
#define PRIMITIVES_3D

#include <vector>

void draw_line_3d(float x, float y, float z,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float r, float g, float b, float thickness,
                  int segments, float alpha = 1.0f);
void draw3DObject(float cx, float cy, float cz,
                  double r, double g, double b,
                  const char* tex,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords,
                  const std::vector<float>& normals,
                  float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f,
                  float alpha = 1.0f);

void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const std::vector<float>& vertices,float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f);

#endif