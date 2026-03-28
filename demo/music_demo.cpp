#include "../avoengine.h"
#include "../avoextension.h"
#include <GL/glut.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace std;

static float cam_angle = 0.0f;
static float cam_pitch = 20.0f;
static float cam_x = 0.0f;
static float cam_z = 3.0f;
static const float CAM_HEIGHT = 1.0f;
static const float MOVE_SPEED = 0.2f;
static int win_w = 800, win_h = 600;

static pseudo_3d_entity* radio_vol = nullptr;
static pseudo_3d_entity* radio_mus = nullptr;

static const float C = 1.0f;

// --- Возвращаем пропущенную функцию отрисовки плитки пола ---
static void drawFloorTile(float wx, float wz, float r, float g, float b) {
    float hw = C * 0.5f;
    vector<float> v = {
        -hw, 0, -hw,  hw, 0, -hw,  hw, 0, hw,  -hw, 0, hw
    };
    vector<int> idx = {0,1,2, 0,2,3}; // Упростили индексы для одного квадрата
    vector<float> tc = {0,0, 1,0, 1,1, 0,1};
    draw3DObject(wx, 0, wz, r, g, b, nullptr, v, idx, tc);
}

void draw_floor() {
    const int TILES = 20;
    const float OFFSET = TILES * C / 2.0f;

    // Сделали пол ярким (убрали темноту)
    float color1[3] = {1.0f, 0.95f, 0.9f}; 
    float color2[3] = {0.9f, 0.85f, 0.8f};

    for (int row = 0; row < TILES; row++) {
        for (int col = 0; col < TILES; col++) {
            float* c = ((row + col) % 2 == 0) ? color1 : color2;
            float wx = col * C - OFFSET + C * 0.5f;
            float wz = row * C - OFFSET + C * 0.5f;
            drawFloorTile(wx, wz, c[0], c[1], c[2]);
        }
    }
}

// --- Возвращаем reshape ---
void reshape(int w, int h) {
    win_w = w; win_h = h;
    changeSize3D(w, h);
}

// --- Возвращаем управление клавиатурой ---
void keyboard(unsigned char key, int, int) {
    float rad = cam_angle * float(M_PI) / 180.0f;
    switch (key) {
        case 'a': cam_angle += 5.0f; break;
        case 'd': cam_angle -= 5.0f; break;
        case 'w':
            cam_x -= sinf(rad) * MOVE_SPEED;
            cam_z -= cosf(rad) * MOVE_SPEED;
            break;
        case 's':
            cam_x += sinf(rad) * MOVE_SPEED;
            cam_z += cosf(rad) * MOVE_SPEED;
            break;
        case 27: exit(0);
    }
    glutPostRedisplay();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_FOG); // Убеждаемся, что тумана нет

    move_camera(cam_x, CAM_HEIGHT, cam_z, -cam_pitch, cam_angle);

    draw_floor();
    
    // Рисуем сущности (белый цвет, чтобы не были темными)
    glColor3f(1.0f, 1.0f, 1.0f);
    if(radio_vol) radio_vol->draw(cam_angle, cam_x, CAM_HEIGHT, cam_z);
    if(radio_mus) radio_mus->draw(cam_angle, cam_x, CAM_HEIGHT, cam_z);

    // HUD
    char buf_h[64];
    snprintf(buf_h, sizeof(buf_h), "Angle: %.1f  Pos: %.1f %.1f", cam_angle, cam_x, cam_z);

    begin_2d(win_w, win_h);
    draw_text(buf_h, 10.0f, float(win_h) - 20.0f, GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f, 1.0f);
    end_2d();

    glutSwapBuffers();
}

int main(int argc, char** argv) {
    // 1. Светлый фон для чистоты
    setup_display(&argc, argv, 0.9f, 0.9f, 1.0f, 1.0f, "Bright Music Demo", 800, 600);
    
    // 2. Текстуры с префиксом demo/
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

    radio_vol = new pseudo_3d_entity(-10, 0.5f, -10, 0, 0, textures, 8, verts);
    radio_mus = new pseudo_3d_entity(10, 0.5f, 10, 0, 0, textures, 8, verts);

    setup_camera(60.0f, cam_x, CAM_HEIGHT, cam_z, -cam_pitch, cam_angle);
    play_sound_3d_loop("demo/src/radio/radio.wav", 10.0f, 0.5f, 10.0f, 1.5f);
    play_white_noise_3d(-10.0f, 0.5f, -10.0f, 1.0f);
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutIdleFunc([]{ glutPostRedisplay(); });

    glutMainLoop();

    delete radio_mus;
    delete radio_vol;
    return 0;
}