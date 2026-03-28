#include "../avoengine.h"
#include <GL/glut.h>
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
    // 1. Очистка
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // 2. ВЫКЛЮЧАЕМ ТУМАН (именно он делает текстуры чёрными в AvoEngine)
    glDisable(GL_FOG);
    
    // 3. Выключаем свет и сбрасываем цвет в белый
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    // 4. Включаем текстуры и прозрачность
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    if (entity) {
        entity->draw(cam_angle, camX(), camY(), camZ());
    }

    // HUD
    begin_2d(win_w, win_h);
    char buf_h[64], buf_v[64];
    snprintf(buf_h, sizeof(buf_h), "H angle: %.1f", cam_angle);
    snprintf(buf_v, sizeof(buf_v), "V angle: %.1f", cam_pitch);
    draw_text(buf_h, 10, win_h - 20, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_v, 10, win_h - 40, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
        case 'a': cam_angle -= 45.0f; break;
        case 'd': cam_angle += 45.0f; break;
        case 'w': cam_pitch = fmin(PITCH_MAX, cam_pitch + 45.0f); break;
        case 's': cam_pitch = fmax(PITCH_MIN, cam_pitch - 45.0f); break;
        case 'z': entity->setGAngle(entity->getGAngle() + 45.0f); break;
        case 'c': entity->setGAngle(entity->getGAngle() - 45.0f); break;
        case 't': entity->setVAngle(entity->getVAngle() + 45.0f); break;
        case 'g': entity->setVAngle(entity->getVAngle() - 45.0f); break;
        case 27:  exit(0);
    }
    glutPostRedisplay();
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    changeSize3D(w, h);
}

int main(int argc, char** argv) {
    // Включаем дисплей ПЕРЕД созданием сущности, чтобы контекст GL был готов
    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f, "entity_3D_Demo", 800, 600);

    std::vector<const char*> textures = {
        "demo/src/radio/render_000_ring00_az000.png", "demo/src/radio/render_001_ring00_az045.png",
        "demo/src/radio/render_002_ring00_az090.png", "demo/src/radio/render_003_ring00_az135.png",
        "demo/src/radio/render_004_ring00_az180.png", "demo/src/radio/render_005_ring00_az225.png",
        "demo/src/radio/render_006_ring00_az270.png", "demo/src/radio/render_007_ring00_az315.png",
        "demo/src/radio/render_008_ring01_az000.png", "demo/src/radio/render_009_ring01_az045.png",
        "demo/src/radio/render_010_ring01_az090.png", "demo/src/radio/render_011_ring01_az135.png",
        "demo/src/radio/render_012_ring01_az180.png", "demo/src/radio/render_013_ring01_az225.png",
        "demo/src/radio/render_014_ring01_az270.png", "demo/src/radio/render_015_ring01_az315.png",
        "demo/src/radio/render_016_ring02_az000.png", "demo/src/radio/render_017_ring02_az045.png",
        "demo/src/radio/render_018_ring02_az090.png", "demo/src/radio/render_019_ring02_az135.png",
        "demo/src/radio/render_020_ring02_az180.png", "demo/src/radio/render_021_ring02_az225.png",
        "demo/src/radio/render_022_ring02_az270.png", "demo/src/radio/render_023_ring02_az315.png",
        "demo/src/radio/render_024_ring03_az000.png", "demo/src/radio/render_025_ring03_az045.png",
        "demo/src/radio/render_026_ring03_az090.png", "demo/src/radio/render_027_ring03_az135.png",
        "demo/src/radio/render_028_ring03_az180.png", "demo/src/radio/render_029_ring03_az225.png",
        "demo/src/radio/render_030_ring03_az270.png", "demo/src/radio/render_031_ring03_az315.png",
        "demo/src/radio/render_032_ring04_az000.png", "demo/src/radio/render_033_ring04_az045.png",
        "demo/src/radio/render_034_ring04_az090.png", "demo/src/radio/render_035_ring04_az135.png",
        "demo/src/radio/render_036_ring04_az180.png", "demo/src/radio/render_037_ring04_az225.png",
        "demo/src/radio/render_038_ring04_az270.png", "demo/src/radio/render_039_ring04_az315.png",
        "demo/src/radio/render_040_ring05_az000.png", "demo/src/radio/render_041_ring05_az045.png",
        "demo/src/radio/render_042_ring05_az090.png", "demo/src/radio/render_043_ring05_az135.png",
        "demo/src/radio/render_044_ring05_az180.png", "demo/src/radio/render_045_ring05_az225.png",
        "demo/src/radio/render_046_ring05_az270.png", "demo/src/radio/render_047_ring05_az315.png",
        "demo/src/radio/render_048_ring06_az000.png", "demo/src/radio/render_049_ring06_az045.png",
        "demo/src/radio/render_050_ring06_az090.png", "demo/src/radio/render_051_ring06_az135.png",
        "demo/src/radio/render_052_ring06_az180.png", "demo/src/radio/render_053_ring06_az225.png",
        "demo/src/radio/render_054_ring06_az270.png", "demo/src/radio/render_055_ring06_az315.png",
        "demo/src/radio/render_056_ring07_az000.png", "demo/src/radio/render_057_ring07_az045.png",
        "demo/src/radio/render_058_ring07_az090.png", "demo/src/radio/render_059_ring07_az135.png",
        "demo/src/radio/render_060_ring07_az180.png", "demo/src/radio/render_061_ring07_az225.png",
        "demo/src/radio/render_062_ring07_az270.png", "demo/src/radio/render_063_ring07_az315.png"
    };

    static float verts[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };

    entity = new pseudo_3d_entity(0, 0, 0, 0, 0, textures, 8, verts);

    setup_camera(60.0f, camX(), camY(), camZ(), cam_pitch, cam_angle);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);

    glutMainLoop();

    delete entity;
    return 0;
}