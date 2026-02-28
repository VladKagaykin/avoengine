#include "avoengine.h"
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>
#include <vector>

static float cam_angle = 0.0f;
static float cam_dist  = 3.0f;
static float cam_pitch = 0.0f;
static const float PITCH_MIN = -60.0f;
static const float PITCH_MAX =  60.0f;
static int win_w = 800, win_h = 600;

static pseudo_3d_entity* entity = nullptr;

float camX() { return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }
float camY() { return sin(cam_pitch * M_PI / 180.0f) * cam_dist; }
float camZ() { return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    // Пол
    std::vector<float> floor_verts = {
        -5, 0, -5,  5, 0, -5,  5, 0, 5,  -5, 0, 5
    };
    std::vector<int> floor_idx = { 0,1,2, 0,2,3 };
    draw3DObject(0, -0.5f, 0, 0.3, 0.3, 0.3, nullptr, floor_verts, floor_idx);

    // entity->draw(cam_angle, camX(), camY(), camZ());
    entity->draw(cam_angle, 0, 0, 0);

    begin_2d(win_w, win_h);
    draw_text("test",0,0,GLUT_BITMAP_HELVETICA_10,1,1,1);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
        case 'a': cam_angle -= 10.0f; break;
        case 'd': cam_angle += 10.0f; break;
        case 'w': cam_pitch = fmin(PITCH_MAX, cam_pitch + 5.0f); break;
        case 's': cam_pitch = fmax(PITCH_MIN, cam_pitch - 5.0f); break;
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
        "src/hand/0.png",
        "src/hand/45.png",
        "src/hand/90.png",
        "src/hand/135.png",
        "src/hand/180.png",
        "src/hand/225.png",
        "src/hand/270.png",
        "src/hand/315.png",
    };

    entity = new pseudo_3d_entity(0, 0, 0, 0, 0, textures);

    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f, "Pseudo3D Demo", 800, 600);
    setup_camera(60.0f, camX(), camY(), camZ(), cam_pitch, cam_angle);
    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutMainLoop();

    delete entity;
    return 0;
}