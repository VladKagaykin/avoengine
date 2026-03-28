#include "../avoengine.h"
#include "../avoextension.h" 
#include <GL/glut.h>
#include <cmath>

static float cam_x = 0.0f, cam_y = 1.0f, cam_z = 5.0f; 
static float cam_yaw = 0.0f, cam_pitch = 0.0f; 
static float speed = 0.1f;
static glb_model* my_model = nullptr; 

void update() {
    if (my_model) my_model->updateAnimation(absolute_tick * 0.01f);
    glutPostRedisplay();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(60.0, 800.0/600.0, 0.1, 1000.0);
    
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float ry = cam_yaw * M_PI / 180.0, rp = cam_pitch * M_PI / 180.0;
    float lx = cos(rp) * sin(ry), ly = sin(rp), lz = cos(rp) * -cos(ry);
    gluLookAt(cam_x, cam_y, cam_z, cam_x + lx, cam_y + ly, cam_z + lz, 0, 1, 0);

    // ОРИГИНАЛЬНЫЙ ПОЛ
    glBegin(GL_LINES); glColor3f(0.5f, 0.5f, 0.5f);
    for(int i=-20; i<=20; i++) {
        glVertex3f(i, 0, -20); glVertex3f(i, 0, 20);
        glVertex3f(-20, 0, i); glVertex3f(20, 0, i);
    }
    glEnd();

    if (my_model) my_model->draw(); 

    begin_2d(window_w, window_h);
    draw_performance_hud(window_w, window_h);
    end_2d();
    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    float ry = cam_yaw * M_PI / 180.0, rp = cam_pitch * M_PI / 180.0;
    float lx = cos(rp) * sin(ry), ly = sin(rp), lz = cos(rp) * -cos(ry);
    if (key == 'w') { cam_x += lx * speed; cam_y += ly * speed; cam_z += lz * speed; }
    if (key == 's') { cam_x -= lx * speed; cam_y -= ly * speed; cam_z -= lz * speed; }
    if (key == 'a') { cam_x += lz * speed; cam_z -= lx * speed; }
    if (key == 'd') { cam_x -= lz * speed; cam_z += lx * speed; }
    if (key == 27) exit(0);
}

void specialKeys(int key, int, int) {
    if(key == GLUT_KEY_LEFT)  cam_yaw -= 3.0f;
    if(key == GLUT_KEY_RIGHT) cam_yaw += 3.0f;
    if(key == GLUT_KEY_UP)    cam_pitch += 3.0f;
    if(key == GLUT_KEY_DOWN)  cam_pitch -= 3.0f;
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.12f, 1.0f, "AvoEngine Fix", 800, 600);
    init_tick_system(); 
    my_model = new glb_model(0, 0, 0);
    if (my_model->load("demo/src/core_fanmade.glb")) my_model->setScale(1.0f);

    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutDisplayFunc(display);
    glutIdleFunc(update); 
    glutMainLoop();
    return 0;
}