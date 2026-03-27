#include "../avoengine.h"
#include "../avoextension.h" 
#include <GL/glut.h>
#include <cmath>
#include <cstdio>

static float cam_angle = 0.0f;
static float cam_dist  = 5.0f;
static float cam_pitch = 20.0f;
static int win_w = 800, win_h = 600;

// Оставляем ТОЛЬКО модель
static glb_model* my_model = nullptr; 

float camX() { return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }
float camY() { return sin(cam_pitch * M_PI / 180.0f) * cam_dist; }
float camZ() { return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    // Безопасная отрисовка
    if (my_model) {
        my_model->draw();
    }

    // Твой HUD из дополнения
    begin_2d(win_w, win_h);
    char buf[128];
    snprintf(buf, sizeof(buf), "Ticks: %d | GLB Model Demo", absolute_tick);
    draw_text(buf, 20, win_h - 30, GLUT_BITMAP_HELVETICA_18, 0, 1, 0, 1);
    
    delay_text("GLB Engine Extension Active", 20, 20, GLUT_BITMAP_8_BY_13, 1, 1, 1, 1, 200, true);
    
    draw_performance_hud(win_w, win_h);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) exit(0);
    if (key == 'a') cam_angle -= 5.0f;
    if (key == 'd') cam_angle += 5.0f;
    glutPostRedisplay();
}

int main(int argc, char** argv) {
    // 1. Инициализация графики (ВАЖНО: ДО загрузки модели)
    setup_display(&argc, argv, 0.05f, 0.05f, 0.1f, 1.0f, "AvoEngine GLB Only", 800, 600);
    init_tick_system(); 

    // 2. Загрузка модели ПОСЛЕ создания окна
    my_model = new glb_model(0, 0, 0);
    if (!my_model->load("demo/src/core_fanmade.glb")) {
        printf("Error: Could not load demo/src/core_fanmade.glb\n");
    }
    my_model->setScale(1.0f);

    setup_camera(60.0f, camX(), camY(), camZ(), cam_pitch, cam_angle);

    glutDisplayFunc(display);
    glutIdleFunc(display); 
    glutKeyboardFunc(keyboard);

    glutMainLoop();
    return 0;
}