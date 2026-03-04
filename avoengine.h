#ifndef AVOENGINE_H
#define AVOENGINE_H

#include <GL/glut.h>
#include <map>
#include <string>
#include <vector>
#include <cmath>

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
            float rotate, float* vertices, const char* texture_file);

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

        // Переводим углы камеры в вектор направления
        float ch = cam_h * M_PI / 180.0f;
        float cv = cam_v * M_PI / 180.0f;
        float dir_x = cos(cv) * sin(ch);
        float dir_y = sin(cv);
        float dir_z = cos(cv) * cos(ch);

        // Поворачиваем вектор в систему координат entity
        // Entity повёрнут на g_angle по горизонтали и v_angle по вертикали
        float ga = g_angle * M_PI / 180.0f;
        float va = v_angle * M_PI / 180.0f;

        // Сначала отменяем горизонтальный поворот entity
        float lx =  dir_x * cos(-ga) + dir_z * sin(-ga);
        float ly =  dir_y;
        float lz = -dir_x * sin(-ga) + dir_z * cos(-ga);

        // Затем отменяем вертикальный поворот entity (вращение вокруг оси X)
        float fx =  lx;
        float fy =  ly * cos(va) + lz * sin(va);
        float fz = -ly * sin(va) + lz * cos(va);

        // Из локального вектора получаем локальные углы
        float local_v = atan2(fy, sqrt(fx*fx + fz*fz)) * 180.0f / M_PI;
        float local_h = (fabs(local_v) > 88.0f) ? 0.0f : atan2(fx, fz) * 180.0f / M_PI;

        float v_relative = fmax(0.0f, fmin(180.0f, local_v + 90.0f));
        int v_index = fmin((int)(v_relative / (180.0f / v_angles)), v_angles - 1);

        int h_index = (int)((fmod(local_h + 360.0f, 360.0f) + 360.0f / h_count * 0.5f) 
                            / (360.0f / h_count)) % h_count;

        int idx = v_index * h_count + h_index;
        return (idx < total) ? textures[idx] : nullptr;
    }

    void draw(float cam_h, float cam_x, float cam_y, float cam_z) const {
        float dx = cam_x - x, dy = cam_y - y, dz = cam_z - z;
        float dist = sqrt(dx*dx + dy*dy + dz*dz);
        float pitch = atan2(dy, sqrt(dx*dx + dz*dz)) * 180.0f / M_PI;

        float ga = g_angle * M_PI / 180.0f;
        float va = v_angle * M_PI / 180.0f;

        // Стрелка направления entity
        // glDisable(GL_TEXTURE_2D);
        // glColor3f(1, 0, 0);
        // glBegin(GL_LINES);
        // glVertex3f(x, y, z);
        // glVertex3f(
        //     x + cos(va) * sin(ga) * 2,
        //     y - sin(va) * 2,
        //     z + cos(va) * cos(ga) * 2
        // );
        // glEnd();

        const char* tex = getCurrentTexture(cam_h, pitch);
        if (tex) {
            GLuint tid = loadTextureFromFile(tex);
            if (tid) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tid); }
        }

        // Billboard: forward = от entity к камере
        float fx = (dist > 0.0001f) ? dx / dist : 0.0f;
        float fy = (dist > 0.0001f) ? dy / dist : 1.0f;
        float fz = (dist > 0.0001f) ? dz / dist : 0.0f;

        float wx = 0.0f, wy = 1.0f, wz = 0.0f;
        if (fabs(fy) > 0.999f) { wx = 0.0f; wy = 0.0f; wz = 1.0f; }

        float rx = wy*fz - wz*fy;
        float ry = wz*fx - wx*fz;
        float rz = wx*fy - wy*fx;
        float rlen = sqrt(rx*rx + ry*ry + rz*rz);
        if (rlen > 0.0001f) { rx /= rlen; ry /= rlen; rz /= rlen; }

        float ux = fy*rz - fz*ry;
        float uy = fz*rx - fx*rz;
        float uz = fx*ry - fy*rx;

        float mat[16] = {
            rx, ry, rz, 0,
            ux, uy, uz, 0,
            fx, fy, fz, 0,
            0,  0,  0,  1
        };

        // "Вверх" entity в мировых координатах (local Y)
        float eu_x = -sin(ga) * sin(va);
        float eu_y = -cos(va);
        float eu_z = -cos(ga) * sin(va);

        // "Вперёд" entity в мировых координатах (local Z) — запасной вектор
        float ef_x = cos(va) * sin(ga);
        float ef_y = -sin(va);
        float ef_z = cos(va) * cos(ga);

        // Проецируем "вверх" entity на плоскость билборда
        float dot = eu_x*fx + eu_y*fy + eu_z*fz;
        float pu_x = eu_x - dot*fx;
        float pu_y = eu_y - dot*fy;
        float pu_z = eu_z - dot*fz;
        float plen = sqrt(pu_x*pu_x + pu_y*pu_y + pu_z*pu_z);

        // Вырожденный случай: "вверх" entity совпадает с направлением взгляда —
        // используем "вперёд" entity как ориентир
        if (plen < 0.01f) {
            float dot2 = ef_x*fx + ef_y*fy + ef_z*fz;
            pu_x = ef_x - dot2*fx;
            pu_y = ef_y - dot2*fy;
            pu_z = ef_z - dot2*fz;
            plen = sqrt(pu_x*pu_x + pu_y*pu_y + pu_z*pu_z);
        }

        float roll = atan2(
                -(pu_x*rx + pu_y*ry + pu_z*rz),
                pu_x*ux + pu_y*uy + pu_z*uz
            ) * 180.0f / M_PI;

        glPushMatrix();
        glTranslatef(x, y, z);
        glMultMatrixf(mat);
        glRotatef(roll+180, 0, 0, 1);
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

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw);

void begin_2d(int w, int h);
void end_2d();

void init_audio();
void play_sound(const char* filename, float volume = 1.0f);
void play_sound_3d(const char* filename, float x, float y, float z, float volume = 1.0f);
void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume = 1.0f);
void play_white_noise_3d(float x, float y, float z, float volume = 1.0f);

#endif