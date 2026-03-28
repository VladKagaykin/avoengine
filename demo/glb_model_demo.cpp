#include "../avoengine.h"
#include "../avoextension.h" 
#include <GL/glut.h>
#include <cmath>

static float cam_x = 0.0f, cam_y = 1.0f, cam_z = 5.0f; 
static float cam_yaw = 0.0f, cam_pitch = 0.0f; 
static float speed = 0.2f; // Немного ускорим для удобства
static glb_model* my_model = nullptr; 

void update() {
    // Используем системный тик для плавной анимации
    if (my_model) my_model->updateAnimation(absolute_tick * 0.01f);
    glutPostRedisplay();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_FOG); // Убираем темноту/туман

    // Используем стандартную функцию движка вместо ручного gluLookAt
    // Она сама вызовет glViewport и настроит проекцию/вид
    setup_camera(60.0f, cam_x, cam_y, cam_z, cam_pitch, cam_yaw);

    // Сетка пола (светлая)
    glBegin(GL_LINES); 
    glColor3f(0.8f, 0.8f, 0.8f);
    for(int i=-20; i<=20; i++) {
        glVertex3f((float)i, 0, -20); glVertex3f((float)i, 0, 20);
        glVertex3f(-20, 0, (float)i); glVertex3f(20, 0, (float)i);
    }
    glEnd();

    // Рисуем модель в центре
    if (my_model) {
        glColor3f(1.0f, 1.0f, 1.0f); // Сброс цвета для текстур
        my_model->draw(); 
    }

    // HUD (использует системные window_w/h)
    draw_performance_hud(window_w, window_h);

    glutSwapBuffers();
}

void keyboard(unsigned char key, int, int) {
    // Вектор направления взгляда для перемещения
    float ry = cam_yaw * float(M_PI) / 180.0f;
    float rp = cam_pitch * float(M_PI) / 180.0f;
    float lx = cosf(rp) * sinf(ry);
    float ly = sinf(rp);
    float lz = cosf(rp) * -cosf(ry);

    if (key == 'w') { cam_x += lx * speed; cam_y += ly * speed; cam_z += lz * speed; }
    if (key == 's') { cam_x -= lx * speed; cam_y -= ly * speed; cam_z -= lz * speed; }
    // Стрейф (перпендикуляр к взгляду)
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
    // 1. Создаем окно (светло-серый фон)
    setup_display(&argc, argv, 0.8f, 0.8f, 0.85f, 1.0f, "AvoEngine GLB Loader", 800, 600);
    
    // 2. Инициализируем системы времени
    init_tick_system(); 

    // 3. Загружаем модель ПОСЛЕ создания контекста
    my_model = new glb_model(0, 0, 0);
    // Путь к модели теперь с demo/
    if (my_model->load("demo/src/core_fanmade.glb")) {
        my_model->setScale(1.0f);
    }

    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutDisplayFunc(display);
    glutIdleFunc(update); 
    
    glutMainLoop();
    return 0;
}