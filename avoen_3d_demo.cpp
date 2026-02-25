#include "avoengine.h"
#include <iostream>
#include <cstdlib>

// Размеры окна
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

// Идентификаторы текстур (для демо)
GLuint texChecker = 0;  // текстура для куба
GLuint texSmile = 0;    // текстура для 2D квадрата

// Состояние анимации
bool animate = true;

// Обработчик клавиатуры (нажатие)
void keyboard(unsigned char key, int x, int y) {
    keyStates[key] = true;

    switch (key) {
        case 27: // ESC
            exit(0);
            break;
        case 'f':
        case 'F':
            isFullscreen = !isFullscreen;
            if (isFullscreen)
                glutFullScreen();
            else {
                glutReshapeWindow(WINDOW_WIDTH, WINDOW_HEIGHT);
                glutPositionWindow(100, 100);
            }
            break;
        case ' ':
            animate = !animate;
            break;
    }
}

// Обработчик отпускания клавиш
void keyboardUp(unsigned char key, int x, int y) {
    keyStates[key] = false;
}

// Таймер для анимации (обновляет tick)
void timer(int value) {
    if (animate) {
        tick = (tick + 1) % max_tick;
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0); // ~60 FPS
}

// Функция отрисовки
void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ---------- 3D часть ----------
    glEnable(GL_DEPTH_TEST);

    // Устанавливаем перспективную камеру
    float aspect = (float)glutGet(GLUT_WINDOW_WIDTH) / (float)glutGet(GLUT_WINDOW_HEIGHT);
    setup_camera(45.0f, aspect, 0.1f, 100.0f,
                 3.0f, 2.0f, 5.0f,    // позиция камеры
                 0.0f, 0.0f, 0.0f,    // центр сцены
                 0.0f, 1.0f, 0.0f);   // вектор вверх

    // Рисуем куб с текстурой (если текстура загружена)
    if (texChecker) {
        cube3D(0.7f, -1.2f, 0.0f, 0.0f,
               1.0f, 1.0f, 1.0f,
               0.0f, tick * 2.0f, 0.0f,   // вращение по Y зависит от tick
               "checker.jpg");  // предполагается, что файл есть
    } else {
        // Если текстура не загружена, рисуем цветной куб
        cube3D(0.7f, -1.2f, 0.0f, 0.0f,
               1.0f, 0.0f, 0.0f,
               0.0f, tick * 2.0f, 0.0f,
               nullptr);
    }

    // Рисуем сферу без текстуры
    sphere3D(0.5f, 1.2f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f,
             tick * 1.5f, 0.0f, 0.0f,
             1.0f, 30, 30,
             nullptr);

    // ---------- 2D часть (GUI) ----------
    glDisable(GL_DEPTH_TEST);

    // Возвращаемся в ортографическую проекцию (как в исходном changeSize)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float w = glutGet(GLUT_WINDOW_WIDTH);
    float h = glutGet(GLUT_WINDOW_HEIGHT);
    float ratio = w / h;
    if (w <= h)
        glOrtho(-1, 1, -1/ratio, 1/ratio, 1, -1);
    else
        glOrtho(-1*ratio, 1*ratio, -1, 1, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Рисуем квадрат (диалоговое окно) с текстурой смайлика
    float squareVerts[] = {-0.5f, -0.3f, 0.5f, -0.3f, 0.5f, 0.3f, -0.5f, 0.3f};
    if (texSmile) {
        square(0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
               0.0f, squareVerts, "smile.jpg",
               std::vector<const char*>(), false);
    } else {
        // Просто цветной прямоугольник
        glColor3f(0.2f, 0.2f, 0.8f);
        glBegin(GL_QUADS);
            for (int i = 0; i < 4; ++i)
                glVertex2f(squareVerts[i*2], squareVerts[i*2+1]);
        glEnd();
    }

    // Можно добавить текст (но для простоты пропустим)

    glutSwapBuffers();
}

int main(int argc, char** argv) {
    // Инициализация окна
    setup_display(&argc, argv, 0.2f, 0.2f, 0.2f, 1.0f,
                  "AVO Engine Demo", WINDOW_WIDTH, WINDOW_HEIGHT);

    // Регистрация callback-функций
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);

    // Попытка загрузить текстуры (файлы должны существовать)
    texChecker = loadTextureFromFile("checker.jpg");
    texSmile = loadTextureFromFile("smile.jpg");

    // Главный цикл
    glutMainLoop();

    return 0;
}