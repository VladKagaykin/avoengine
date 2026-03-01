#include "avoengine.h"
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>
#include <vector>

static float cam_angle = 0.0f;
static float cam_dist  = 3.0f;
static float cam_pitch = 0.0f;
static const float PITCH_MIN = -89.99f;
static const float PITCH_MAX =  89.99f;
static int win_w = 800, win_h = 600;
static pseudo_3d_entity* entity = nullptr;

float camX() { return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }
float camY() { return sin(cam_pitch * M_PI / 180.0f) * cam_dist; }
float camZ() { return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    entity->draw(cam_angle, camX(), camY(), camZ());

    // HUD
    float dx = camX() - entity->getX();
    float dy = camY() - entity->getY();
    float dz = camZ() - entity->getZ();
    float pitch_to_entity = atan2(dy, sqrt(dx*dx + dz*dz)) * 180.0f / M_PI;
    const char* cur_tex = entity->getCurrentTexture(cam_angle, pitch_to_entity);

    char buf_h[64], buf_v[64], buf_t[256];
    snprintf(buf_h, sizeof(buf_h), "H angle: %.1f", cam_angle);
    snprintf(buf_v, sizeof(buf_v), "V angle: %.1f", cam_pitch);
    snprintf(buf_t, sizeof(buf_t), "Texture: %s", cur_tex ? cur_tex : "none");

    begin_2d(win_w, win_h);
    draw_text(buf_h, 10, win_h - 20,  GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_v, 10, win_h - 40,  GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_t, 10, win_h - 60,  GLUT_BITMAP_HELVETICA_12, 1, 1, 0);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
        case 'a': cam_angle -= 10.0f; break;
        case 'd': cam_angle += 10.0f; break;
        case 'w': cam_pitch = fmin(PITCH_MAX, cam_pitch + 10.0f); break;
        case 's': cam_pitch = fmax(PITCH_MIN, cam_pitch - 10.0f); break;
        case 'z': entity->setGAngle(entity->getGAngle() + 10.0f); break;
        case 'c': entity->setGAngle(entity->getGAngle() - 10.0f); break;
        case 27:  exit(0);
    }
    glutPostRedisplay();
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    changeSize3D(w, h);
}

int main(int argc, char** argv) {
    std::vector<const char*> textures = {
        "src/radio/render_000_ring0_az000.png",
        "src/radio/render_001_ring0_az045.png",
        "src/radio/render_002_ring0_az090.png",
        "src/radio/render_003_ring0_az135.png",
        "src/radio/render_004_ring0_az180.png",
        "src/radio/render_005_ring0_az225.png",
        "src/radio/render_006_ring0_az270.png",
        "src/radio/render_007_ring0_az315.png",
        "src/radio/render_008_ring1_az000.png",
        "src/radio/render_009_ring1_az045.png",
        "src/radio/render_010_ring1_az090.png",
        "src/radio/render_011_ring1_az135.png",
        "src/radio/render_012_ring1_az180.png",
        "src/radio/render_013_ring1_az225.png",
        "src/radio/render_014_ring1_az270.png",
        "src/radio/render_015_ring1_az315.png",
        "src/radio/render_016_ring2_az000.png",
        "src/radio/render_017_ring2_az045.png",
        "src/radio/render_018_ring2_az090.png",
        "src/radio/render_019_ring2_az135.png",
        "src/radio/render_020_ring2_az180.png",
        "src/radio/render_021_ring2_az225.png",
        "src/radio/render_022_ring2_az270.png",
        "src/radio/render_023_ring2_az315.png",
        "src/radio/render_024_ring3_az000.png",
        "src/radio/render_025_ring3_az045.png",
        "src/radio/render_026_ring3_az090.png",
        "src/radio/render_027_ring3_az135.png",
        "src/radio/render_028_ring3_az180.png",
        "src/radio/render_029_ring3_az225.png",
        "src/radio/render_030_ring3_az270.png",
        "src/radio/render_031_ring3_az315.png",
        "src/radio/render_032_ring4_az000.png",
        "src/radio/render_033_ring4_az045.png",
        "src/radio/render_034_ring4_az090.png",
        "src/radio/render_035_ring4_az135.png",
        "src/radio/render_036_ring4_az180.png",
        "src/radio/render_037_ring4_az225.png",
        "src/radio/render_038_ring4_az270.png",
        "src/radio/render_039_ring4_az315.png",
        "src/radio/render_040_ring5_az000.png",
        "src/radio/render_041_ring5_az045.png",
        "src/radio/render_042_ring5_az090.png",
        "src/radio/render_043_ring5_az135.png",
        "src/radio/render_044_ring5_az180.png",
        "src/radio/render_045_ring5_az225.png",
        "src/radio/render_046_ring5_az270.png",
        "src/radio/render_047_ring5_az315.png",
        "src/radio/render_048_ring6_az000.png",
        "src/radio/render_049_ring6_az045.png",
        "src/radio/render_050_ring6_az090.png",
        "src/radio/render_051_ring6_az135.png",
        "src/radio/render_052_ring6_az180.png",
        "src/radio/render_053_ring6_az225.png",
        "src/radio/render_054_ring6_az270.png",
        "src/radio/render_055_ring6_az315.png",
        "src/radio/render_056_ring7_az000.png",
        "src/radio/render_057_ring7_az045.png",
        "src/radio/render_058_ring7_az090.png",
        "src/radio/render_059_ring7_az135.png",
        "src/radio/render_060_ring7_az180.png",
        "src/radio/render_061_ring7_az225.png",
        "src/radio/render_062_ring7_az270.png",
        "src/radio/render_063_ring7_az315.png"
    };

    // Вершины billboard-quad в локальных координатах (x, y) — 4 точки
    static float verts[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f
    };

    entity = new pseudo_3d_entity(0, 0, 0, 0, 0, textures, 8, verts);

    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f, "entity_3D_Demo", 800, 600);
    setup_camera(60.0f, camX(), camY(), camZ(), cam_pitch, cam_angle);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);

    glutMainLoop();

    delete entity;
    return 0;
}