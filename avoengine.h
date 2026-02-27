#ifndef AVOENGINE_H
#define AVOENGINE_H

#include <GL/glut.h>
#include <map>
#include <string>
#include <vector>
#include<cmath>

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

class pseudo_3d_entity {
private:
    float x, y, z;
    float g_angle;
    float v_angle;
    std::vector<const char*> textures;

public:
    pseudo_3d_entity(float x, float y, float z,
                     float g_angle, float v_angle,
                     std::vector<const char*> textures)
        : x(x), y(y), z(z),
          g_angle(g_angle), v_angle(v_angle),
          textures(textures) {}

    int getTextureIndex(float camera_h_angle) const {
        if (textures.empty()) return -1;
        int count = (int)textures.size();
        float sector = 360.0f / count;
        float relative = (camera_h_angle + 180.0f) - g_angle;
        while (relative < 0)    relative += 360.0f;
        while (relative >= 360) relative -= 360.0f;
        relative = fmod(relative + sector * 0.5f, 360.0f);
        int index = (int)(relative / sector);
        if (index >= count) index = count - 1;
        return index;
    }

    const char* getCurrentTexture(float camera_h_angle) const {
        int idx = getTextureIndex(camera_h_angle);
        if (idx < 0) return nullptr;
        return textures[idx];
    }

    void draw(float camera_h_angle,
          float cam_x, float cam_y, float cam_z,
          float size = 0.25f) const {
        const char* tex = getCurrentTexture(camera_h_angle);

        if (tex != nullptr) {
            GLuint textureID = loadTextureFromFile(tex);
            if (textureID != 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, textureID);
            }
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        // Вектор от entity к камере (для billboard-поворота)
        float dx = cam_x - x;
        float dz = cam_z - z;
        float billboard_angle = atan2(dx, dz) * 180.0f / M_PI;

        glPushMatrix();
        glTranslatef(x, y, z);
        glRotatef(billboard_angle, 0, 1, 0); // всегда лицом к камере

        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex3f(-size, -size, 0);
        glTexCoord2f(1, 1); glVertex3f( size, -size, 0);
        glTexCoord2f(1, 0); glVertex3f( size,  size, 0);
        glTexCoord2f(0, 0); glVertex3f(-size,  size, 0);
        glEnd();

        glDisable(GL_TEXTURE_2D);

        // Стрелка — куда смотрит entity (в горизонтальной плоскости)
        float rad = g_angle * M_PI / 180.0f;
        glColor3f(1, 0, 0);
        glBegin(GL_LINES);
        glVertex3f(0, 0, 0);
        glVertex3f(sin(rad) * size * 2, 0, cos(rad) * size * 2);
        glEnd();

        glPopMatrix();
    }

    void setGAngle(float a) { g_angle = a; }
    float getGAngle() const { return g_angle; }
    float getX() const { return x; }
    float getY() const { return y; }
};

// Функции настройки OpenGL
void changeSize3D(int w, int h);
void changeSize2D(int w, int h);
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