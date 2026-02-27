#include "avoengine.h"
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>
#include <vector>

static float cam_angle  = 0.0f;
static float cam_dist   = 3.0f;
static float cam_pitch  = 0.0f;   // вертикальный угол, 0 = на уровне entity
static const float PITCH_MIN = -60.0f;
static const float PITCH_MAX =  60.0f;

static pseudo_3d_entity* entity = nullptr;

float camX() { return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }
float camY() { return sin(cam_pitch * M_PI / 180.0f) * cam_dist; }
float camZ() { return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(camX(), camY(), camZ(),
              0, 0, 0,
              0, 1, 0);

    // Пол
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.3f, 0.3f, 0.3f);
    glBegin(GL_QUADS);
    glVertex3f(-5, -0.5f, -5);
    glVertex3f( 5, -0.5f, -5);
    glVertex3f( 5, -0.5f,  5);
    glVertex3f(-5, -0.5f,  5);
    glEnd();

    entity->draw(cam_angle, camX(), camY(), camZ());

    // HUD
    char buf[128];
    snprintf(buf, sizeof(buf), "Texture idx: %d | Cam: %.0f | Pitch: %.0f | Entity: %.0f",
             entity->getTextureIndex(cam_angle),
             cam_angle, cam_pitch, entity->getGAngle());

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 800, 0, 600, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glColor3f(1, 1, 1);
    glRasterPos2f(10, 10);
    for (char* c = buf; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

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
    changeSize3D(w, h);
}

int main(int argc, char** argv) {
    std::vector<const char*> textures = {
        "tex_front.png",
        "tex_front_left.png",
        "tex_left.png",
        "tex_back_left.png",
        "tex_back.png",
        "tex_back_right.png",
        "tex_right.png",
        "tex_front_right.png",
    };

    entity = new pseudo_3d_entity(0, 0, 0, 0, 0, textures);

    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f, "Pseudo3D Demo", 800, 600);
    setup_camera(60.0f, 800.0f/600.0f, 0.1f, 100.0f,
                 camX(), camY(), camZ(),
                 0, 0, 0,
                 0, 1, 0);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutMainLoop();

    delete entity;
    return 0;
}