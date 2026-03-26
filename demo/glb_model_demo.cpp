#include "../avoengine.h"
#include "../avoextension.h"
#include <GL/glut.h>
#include <cmath>
#include <cstdio>

static GLBModel model;              // объект класса
static float cam_angle = 0.0f;
static float cam_dist  = 5.0f;      // увеличено для лучшего обзора
static float cam_pitch = 0.0f;
static const float PITCH_MIN = -89.99f;
static const float PITCH_MAX =  89.99f;
static int win_w = 800, win_h = 600;
static float model_angle = 0.0f;
static int last_time = 0;

float camX() {
    return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f);
}
float camY() {
    return sin(cam_pitch * M_PI / 180.0f) * cam_dist;
}
float camZ() {
    return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f);
}

void setup_lights() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat light_pos[] = { 5.0f, 5.0f, 5.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    GLfloat white_light[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white_light);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white_light);
    GLfloat ambient_light[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient_light);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    // в display() перед model.draw:
    glDisable(GL_LIGHTING);
    glColor3f(1,1,1);
    model.draw(0.0f, 0.0f, 0.0f, 10.0f, 0.0f, model_angle, 0.0f);

    // HUD
    char buf_h[64], buf_v[64], buf_rot[64], buf_anim[64];
    snprintf(buf_h, sizeof(buf_h), "H angle: %.1f", cam_angle);
    snprintf(buf_v, sizeof(buf_v), "V angle: %.1f", cam_pitch);
    snprintf(buf_rot, sizeof(buf_rot), "Model Y: %.1f", model_angle);

    int anim_cnt = model.getAnimationCount();
    if (anim_cnt > 0) {
        const char* name = model.getAnimationName(0);
        snprintf(buf_anim, sizeof(buf_anim), "Animation: %s (playing)", name);
    } else {
        snprintf(buf_anim, sizeof(buf_anim), "No animations");
    }

    begin_2d(win_w, win_h);
    draw_text(buf_h, 10, win_h - 20, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_v, 10, win_h - 40, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_rot, 10, win_h - 60, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_anim, 10, win_h - 80, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_performance_hud(window_w, window_h);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
        case 'a': cam_angle -= 5.0f; break;
        case 'd': cam_angle += 5.0f; break;
        case 'w': cam_pitch = fmin(PITCH_MAX, cam_pitch + 5.0f); break;
        case 's': cam_pitch = fmax(PITCH_MIN, cam_pitch - 5.0f); break;
        case 'z': model_angle -= 45.0f; break;
        case 'c': model_angle += 45.0f; break;
        case 'r':
            if (model.getAnimationCount() > 0)
                model.playAnimation(0, 1.0f, true);
            break;
        case ' ':
            if (model.getAnimationCount() > 0) {
                static bool paused = false;
                if (paused) {
                    model.playAnimation(0, 1.0f, false);
                    paused = false;
                } else {
                    model.stopAnimation();
                    paused = true;
                }
            }
            break;
        case 27: exit(0);
    }
    glutPostRedisplay();
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    changeSize3D(w, h);
}

void timer(int) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    float delta = (now - last_time) / 1000.0f;
    last_time = now;
    model.update(delta);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f,
                  "GLB Model Demo (using class)", 800, 600);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        fprintf(stderr, "GLEW Error: %s\n", glewGetErrorString(err));
        return 1;
    }

    setup_lights();

    if (!model.load("demo/src/radio/portal_radio.glb")) {
        fprintf(stderr, "Failed to load model\n");
        return 1;
    }

    int anim_cnt = model.getAnimationCount();
    printf("Model loaded. Animations: %d\n", anim_cnt);
    if (anim_cnt > 0) {
        model.playAnimation(0, 1.0f, true);
        printf("Playing: %s\n", model.getAnimationName(0));
    }

    last_time = glutGet(GLUT_ELAPSED_TIME);
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutTimerFunc(0, timer, 0);

    glutMainLoop();

    model.unload();
    return 0;
}