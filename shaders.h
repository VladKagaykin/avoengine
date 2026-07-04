#ifndef SHADERS
#define SHADERS

#include <GL/glew.h>

extern GLuint currentShaderProg;

GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode);
void useShader(GLuint id);
void stopShader();

#endif