#include "../avoengine.h"
#include "../avoextension.h"
#include <GL/glu.h>
#include <GL/glut.h>
#include <cmath>

using namespace std;

extern int window_w, window_h;

static float cam_x = 0, cam_y = 1.7f, cam_z = 0;
static float cam_pitch = 0, cam_yaw = 0;
static bool keys[256] = {}, skeys[512] = {};

static const float MOVE_SPEED = 6.0f;
static const float TURN_SPEED = 90.0f;
static const float CELL = 8.0f;
static const int FLOOR_R = 40, SPHERE_R = 25;
static const float MAX_DIST = 200.0f;

static GLuint sph_list = 0;

static void buildSphereList(float R, int stacks, int slices) {
    sph_list = glGenLists(1);
    glNewList(sph_list, GL_COMPILE);
    for (int i = 0; i < stacks; ++i) {
        const float phi0 = float(M_PI) * i / stacks;
        const float phi1 = float(M_PI) * (i + 1) / stacks;
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * float(M_PI) * j / slices;
            glVertex3f(R * sinf(phi0) * cosf(theta), R * cosf(phi0), R * sinf(phi0) * sinf(theta));
            glVertex3f(R * sinf(phi1) * cosf(theta), R * cosf(phi1), R * sinf(phi1) * sinf(theta));
        }
        glEnd();
    }
    glEndList();
}

static inline void sphereAt(int ix, int iz, float& x, float& y, float& z) {
    x = ix * CELL + 0.5f * CELL * sinf(ix * 1.3f + iz * 0.7f);
    z = iz * CELL + 0.5f * CELL * cosf(iz * 1.1f - ix * 0.9f);
    y = 6.0f + 3.0f * sinf(ix * 0.8f) * cosf(iz * 0.6f);
}

static float fwd_x = 0, fwd_z = 1;

static inline bool inFront(float px, float pz, float margin) {
    const float dx = px - cam_x, dz = pz - cam_z;
    const float dist2 = dx * dx + dz * dz;
    const float lim = MAX_DIST + margin;
    if (dist2 > lim * lim) return false;
    const float dot = dx * fwd_x + dz * fwd_z;
    return dot > -margin || dist2 < margin * margin * 4.0f;
}

static void drawFloor() {
    const int tcx = (int)floorf(cam_x * 0.5f);
    const int tcz = (int)floorf(cam_z * 0.5f);
    
    // Светлые плитки
    glColor3f(0.85f, 0.8f, 0.7f); 
    glBegin(GL_QUADS);
    for (int di = -FLOOR_R; di <= FLOOR_R; ++di)
        for (int dj = -FLOOR_R; dj <= FLOOR_R; ++dj) {
            const int tx = tcx + di, tz = tcz + dj;
            if (((tx + tz) & 1) != 0) continue;
            const float wx = tx * 2.0f, wz = tz * 2.0f;
            if (!inFront(wx, wz, 4.0f)) continue;
            glVertex3f(wx - 1, 0, wz - 1); glVertex3f(wx + 1, 0, wz - 1);
            glVertex3f(wx + 1, 0, wz + 1); glVertex3f(wx - 1, 0, wz + 1);
        }
    glEnd();

    // Тёмные плитки (теперь просто серые, не чёрные)
    glColor3f(0.65f, 0.6f, 0.5f);
    glBegin(GL_QUADS);
    for (int di = -FLOOR_R; di <= FLOOR_R; ++di)
        for (int dj = -FLOOR_R; dj <= FLOOR_R; ++dj) {
            const int tx = tcx + di, tz = tcz + dj;
            if (((tx + tz) & 1) != 1) continue;
            const float wx = tx * 2.0f, wz = tz * 2.0f;
            if (!inFront(wx, wz, 4.0f)) continue;
            glVertex3f(wx - 1, 0, wz - 1); glVertex3f(wx + 1, 0, wz - 1);
            glVertex3f(wx + 1, 0, wz + 1); glVertex3f(wx - 1, 0, wz + 1);
        }
    glEnd();
}

static void drawSpheres() {
    const int cx = (int)floorf(cam_x / CELL);
    const int cz = (int)floorf(cam_z / CELL);
    glColor3f(1.0f, 0.78f, 0.0f);
    for (int di = -SPHERE_R; di <= SPHERE_R; ++di)
        for (int dj = -SPHERE_R; dj <= SPHERE_R; ++dj) {
            float sx, sy, sz;
            sphereAt(cx + di, cz + dj, sx, sy, sz);
            if (!inFront(sx, sz, 2.0f)) continue;
            glPushMatrix();
            glTranslatef(sx, sy, sz);
            glCallList(sph_list);
            glPopMatrix();
        }
}

static void onDisplay() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float yr = cam_yaw * float(M_PI) / 180.0f;
    fwd_x = sinf(yr); fwd_z = cosf(yr);

    setup_camera(60.0f, cam_x, cam_y, cam_z, cam_pitch, cam_yaw);
    
    drawFloor();
    drawSpheres();
    draw_performance_hud(window_w, window_h);
    
    glutSwapBuffers();
}

static void onIdle() {
    static float prev_t = 0;
    const float t = glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    const float dt = fminf(t - prev_t, 0.05f);
    prev_t = t;

    if (skeys[GLUT_KEY_LEFT])  cam_yaw += TURN_SPEED * dt;
    if (skeys[GLUT_KEY_RIGHT]) cam_yaw -= TURN_SPEED * dt;
    if (skeys[GLUT_KEY_UP])    cam_pitch += TURN_SPEED * dt;
    if (skeys[GLUT_KEY_DOWN])  cam_pitch -= TURN_SPEED * dt;

    if (cam_pitch > 89) cam_pitch = 89;
    if (cam_pitch < -89) cam_pitch = -89;

    const float yr = cam_yaw * float(M_PI) / 180.0f;
    const float mv = MOVE_SPEED * dt;
    if (keys['w'] || keys['W']) { cam_x += sinf(yr) * mv; cam_z += cosf(yr) * mv; }
    if (keys['s'] || keys['S']) { cam_x -= sinf(yr) * mv; cam_z -= cosf(yr) * mv; }
    if (keys['a'] || keys['A']) { cam_x += cosf(yr) * mv; cam_z -= sinf(yr) * mv; }
    if (keys['d'] || keys['D']) { cam_x -= cosf(yr) * mv; cam_z += sinf(yr) * mv; }
    
    glutPostRedisplay();
}

int main(int argc, char** argv) {
    // Светлый фон (0.8f)
    setup_display(&argc, argv, 0.8f, 0.8f, 0.9f, 1.0f, "Bright Fog Demo", 1280, 720);
    
    // Светлый туман (параметры: плотность, R, G, B, start, end)
    enable_fog(0.05f, 0.8f, 0.8f, 0.9f, 1.0f, 10.0f);
    
    buildSphereList(0.45f, 6, 7);

    glutDisplayFunc(onDisplay);
    glutReshapeFunc([](int w, int h) { window_w = w; window_h = h; changeSize3D(w, h); });
    glutKeyboardFunc([](unsigned char k, int, int) { keys[k] = true; if(k==27) exit(0); });
    glutKeyboardUpFunc([](unsigned char k, int, int) { keys[k] = false; });
    glutSpecialFunc([](int k, int, int) { skeys[k] = true; });
    glutSpecialUpFunc([](int k, int, int) { skeys[k] = false; });
    glutIdleFunc(onIdle);

    glutMainLoop();
    return 0;
}