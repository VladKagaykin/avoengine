#ifndef AVOENGINE_H
#define AVOENGINE_H

#include <GL/glut.h>
#include <map>
#include <string>
#include <vector>


// Функции работы с текстурами
GLuint loadTextureFromFile(const char* filename);
void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad);

// Функции отрисовки 2D-примитивов
void triangle(float scale, float center_x, float center_y, double r, double g, double b, 
              float rotate, float* vertices, const char* texture_file = nullptr);

void square(float local_size, float x, float y, double r, double g, double b, 
            float rotate, float* vertices, const char* texture_file, 
            std::vector<const char*> textures, bool condition);

void circle(float scale, float center_x, float center_y, double r, double g, double b, 
            float radius, float in_radius, float rotate, int slices, int loops,
            const char* texture_file = nullptr);

// Функции настройки OpenGL
void changeSize(int w, int h);
void setup_display(int* argc, char** argv, float r, float g, float b, float a, 
                   const char* name, int w, int h);

// Новые 3D-функции
void setup_camera(float fov, float aspect, float near_plane, float far_plane,
                  float eye_x, float eye_y, float eye_z,
                  float center_x, float center_y, float center_z,
                  float up_x, float up_y, float up_z);

void draw3DObject(float scale,
                  float center_x, float center_y, float center_z,
                  float rotate_x, float rotate_y, float rotate_z,
                  double r, double g, double b,
                  const char* texture_file,
                  int num_vertices, float* vertices,
                  int num_indices, int* indices,
                  float* texcoords = nullptr);

void cube3D(float scale,
            float center_x, float center_y, float center_z,
            double r, double g, double b,
            float rotate_x, float rotate_y, float rotate_z,
            const char* texture_file);

void sphere3D(float scale,
              float center_x, float center_y, float center_z,
              double r, double g, double b,
              float rotate_x, float rotate_y, float rotate_z,
              float radius, int slices, int stacks,
              const char* texture_file);

#endif