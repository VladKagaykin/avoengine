#ifndef AVOENGINE_H
#define AVOENGINE_H

#include <GL/glut.h>
#include <map>
#include <string>
#include <vector>

// Глобальные переменные (объявляем как extern)
extern bool isFullscreen;
extern bool is_moving;
extern int tick;
extern const int max_tick;
extern std::map<int, bool> keyStates;

// Функции работы с текстурами
GLuint loadTextureFromFile(const char* filename);
void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad);

// Функции отрисовки примитивов
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
void keyboardUp(unsigned char key, int x, int y);
void timer(int value);

#endif