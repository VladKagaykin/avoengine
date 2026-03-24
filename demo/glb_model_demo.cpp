#include "../avoengine.h"
#include "../avoextension.h"  // для load_glb_model, draw_glb_model, unload_glb_model, clear_embedded_texture_cache
#include <GL/glut.h>
#include <cmath>
#include <cstdio>

static float cam_angle = 0.0f;
static float cam_dist  = 3.0f;
static float cam_pitch = 0.0f;
static const float PITCH_MIN = -89.99f;
static const float PITCH_MAX =  89.99f;
static int win_w = 800, win_h = 600;

static int model_id = -1;            // ID загруженной GLB модели
static float model_angle = 0.0f;     // угол поворота модели вокруг Y

float camX() { return sin(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }
float camY() { return sin(cam_pitch * M_PI / 180.0f) * cam_dist; }
float camZ() { return cos(cam_angle * M_PI / 180.0f) * cam_dist * cos(cam_pitch * M_PI / 180.0f); }

void setup_lights() {
    glEnable(GL_LIGHTING);   // Включаем расчет освещения
    glEnable(GL_LIGHT0);     // Включаем первый источник света

    // Позиция света (x, y, z, w). Если w=1.0 - это точка, если 0.0 - направленный свет (как солнце)
    GLfloat light_pos[] = { 5.0f, 5.0f, 5.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    // Цвет света (белый)
    GLfloat white_light[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white_light);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white_light);

    // Окружающий свет (чтобы тени не были абсолютно черными)
    GLfloat ambient_light[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient_light);
    
    // ВАЖНО для текстур: позволяем материалам принимать цвет текстуры
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    move_camera(camX(), camY(), camZ(), cam_pitch, cam_angle);
    
    glDisable(GL_LIGHTING); // ВЫКЛЮЧИ свет совсем
    glEnable(GL_TEXTURE_2D); // ВКЛЮЧИ текстуры
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // ПРИНУДИТЕЛЬНО белый

    if (model_id >= 0) {
        draw_glb_model(model_id, 0.0f, 0.0f, 0.0f, 10.0f, 0, model_angle, 0);
    }

    // HUD
    char buf_h[64], buf_v[64], buf_rot[64];
    snprintf(buf_h, sizeof(buf_h), "H angle: %.1f", cam_angle);
    snprintf(buf_v, sizeof(buf_v), "V angle: %.1f", cam_pitch);
    snprintf(buf_rot, sizeof(buf_rot), "Model Y: %.1f", model_angle);

    begin_2d(win_w, win_h);
    draw_text(buf_h, 10, win_h - 20, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_v, 10, win_h - 40, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_text(buf_rot, 10, win_h - 60, GLUT_BITMAP_HELVETICA_12, 1, 1, 1);
    draw_performance_hud(window_w, window_h);
    end_2d();

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
        case 'a': cam_angle -= 5.0f; break;
        case 'd': cam_angle += 5.0f; break;
        case 'w': cam_pitch = fmin(PITCH_MAX, cam_pitch + 45.0f); break;
        case 's': cam_pitch = fmax(PITCH_MIN, cam_pitch - 45.0f); break;
        case 'z': model_angle -= 45.0f; break;   // поворот модели влево
        case 'c': model_angle += 45.0f; break;   // поворот модели вправо
        case 27:  exit(0);
    }
    glutPostRedisplay();
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    changeSize3D(w, h);
}

int main(int argc, char** argv) {
    // 1. Создаем окно и контекст OpenGL
    setup_display(&argc, argv, 0.1f, 0.1f, 0.15f, 1.0f, "GLB Model Demo", 800, 600);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    // 2. Инициализируем указатели на функции (GLEW)
    // БЕЗ ЭТОГО ВЫЗОВА БУДЕТ SIGSEGV ПРИ ЗАГРУЗКЕ МОДЕЛИ
    glewExperimental = GL_TRUE; 
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        fprintf(stderr, "GLEW Error: %s\n", glewGetErrorString(err));
        return 1;
    }

    // 3. Теперь можно загружать модель
    model_id = load_glb_model("demo/src/radio/portal_radio.glb");

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);

    glutMainLoop();

    // Освобождаем ресурсы
    if (model_id >= 0) unload_glb_model(model_id);
    clear_embedded_texture_cache();  // удаляем кэш встроенных текстур (опционально)

    return 0;
}