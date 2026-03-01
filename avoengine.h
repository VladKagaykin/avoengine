#ifndef AVOENGINE_H
#define AVOENGINE_H

#include <GL/glut.h>
#include <map>
#include <string>
#include <vector>
#include<cmath>

extern int window_w;
extern int window_h;
extern int screen_w;
extern int screen_h;

// Функции работы с текстурами
GLuint loadTextureFromFile(const char* filename);
void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad);

// Функции отрисовки 2D-примитивов
void triangle(float scale, float center_x, float center_y, double r, double g, double b, 
              float rotate, float* vertices, const char* texture_file = nullptr);

void square(float local_size, float x, float y, double r, double g, double b, 
            float rotate, float* vertices, const char* texture_file, 
            std::vector<const char*> textures, bool condition, const int max_tick, int tick);

void circle(float scale, float center_x, float center_y, double r, double g, double b, 
            float radius, float in_radius, float rotate, int slices, int loops,
            const char* texture_file = nullptr);

void draw_text(const char* text, float x, float y, void* font, float r, float g, float b);

class pseudo_3d_entity {
private:
    float x, y, z, g_angle, v_angle;
    std::vector<const char*> textures;
    int v_angles;
    float* vertices;

public:
    pseudo_3d_entity(float x, float y, float z, float g_angle, float v_angle,
                     std::vector<const char*> textures, int v_angles, float* vertices)
        : x(x), y(y), z(z), g_angle(g_angle), v_angle(v_angle),
          textures(textures), v_angles(v_angles), vertices(vertices) {}

    const char* getCurrentTexture(float cam_h, float cam_v) const {
        if (textures.empty()) return nullptr;
        int total = (int)textures.size();
        int h_count = total / v_angles;
        if (h_count <= 0) return nullptr;

        float v_relative = fmax(0.0f, fmin(180.0f, (cam_v - v_angle) + 90.0f));
        int v_index = fmin((int)(v_relative / (180.0f / v_angles)), v_angles - 1);

        int h_index = (int)((fmod(cam_h - g_angle + 360.0f, 360.0f) + 360.0f / h_count * 0.5f) / (360.0f / h_count)) % h_count;

        int idx = v_index * h_count + h_index;
        return (idx < total) ? textures[idx] : nullptr;
    }

    void draw(float cam_h, float cam_x, float cam_y, float cam_z) const {
        float dx = cam_x - x, dy = cam_y - y, dz = cam_z - z;
        float yaw   = atan2(dx, dz) * 180.0f / M_PI;
        float pitch = atan2(dy, sqrt(dx*dx + dz*dz)) * 180.0f / M_PI;

        glDisable(GL_TEXTURE_2D);
        glColor3f(1, 0, 0);
        glBegin(GL_LINES);
        glVertex3f(x, y, z);
        glVertex3f(x + sin(g_angle * M_PI / 180.0f) * 2, y, z + cos(g_angle * M_PI / 180.0f) * 2);
        glEnd();

        const char* tex = getCurrentTexture(cam_h, pitch);
        if (tex) {
            GLuint tid = loadTextureFromFile(tex);
            if (tid) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tid); }
        }

        glPushMatrix();
        glTranslatef(x, y, z);
        glRotatef(yaw,    0, 1, 0);
        glRotatef(-pitch, 1, 0, 0);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0,1); glVertex3f(vertices[0], vertices[1], 0);
        glTexCoord2f(1,1); glVertex3f(vertices[2], vertices[3], 0);
        glTexCoord2f(1,0); glVertex3f(vertices[4], vertices[5], 0);
        glTexCoord2f(0,0); glVertex3f(vertices[6], vertices[7], 0);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }

    void setGAngle(float a) { g_angle = a; }
    void setVAngle(float a) { v_angle = a; }
    float getGAngle() const { return g_angle; }
    float getVAngle() const { return v_angle; }
    float getX() const { return x; }
    float getY() const { return y; }
    float getZ() const { return z; }
};

// Функции настройки OpenGL
void changeSize3D(int w, int h);
void changeSize2D(int w, int h);
void setup_display(int* argc, char** argv, float r, float g, float b, float a, 
                   const char* name, int w, int h);

// Новые 3D-функции
void setup_camera(float fov,
                  float eye_x, float eye_y, float eye_z,
                  float pitch, float yaw);

void draw3DObject(float center_x, float center_y, float center_z,
                  double r, double g, double b,
                  const char* texture_file,
                  const std::vector<float>& vertices,
                  const std::vector<int>& indices,
                  const std::vector<float>& texcoords = {});

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

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw);

void begin_2d(int w, int h);
void end_2d();

#endif