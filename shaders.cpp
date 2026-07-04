#include "shaders.h"
#include "avoengine.h"

#include <iostream>
#include <GL/glew.h>

//              шейдеры
// Переменная для хранения текущей программы шейдеров
GLuint currentShaderProg = 0;
// Функция для проверки ошибок компиляции
void checkShaderErrors(GLuint shader, string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << endl;
        }
    }
}

GLuint createShaderProgram(const char* vertexCode, const char* fragmentCode) {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexCode, NULL);
    glCompileShader(vertex);
    checkShaderErrors(vertex, "VERTEX");

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentCode, NULL);
    glCompileShader(fragment);
    checkShaderErrors(fragment, "FRAGMENT");

    GLuint ID = glCreateProgram();
    
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);

    glBindAttribLocation(ID, 0, "aVertex");
    glBindAttribLocation(ID, 2, "aNormal");
    glBindAttribLocation(ID, 3, "aColor");
    glBindAttribLocation(ID, 8, "aTexCoord");

    glLinkProgram(ID);
    checkShaderErrors(ID, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return ID;
}

void useShader(GLuint id) {
    glUseProgram(id);
    currentShaderProg = id;
}

void stopShader() {
    glUseProgram(0);
    currentShaderProg = 0;
}