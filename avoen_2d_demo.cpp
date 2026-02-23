#include "avoengine.h"
#include <cmath>
#include <vector>

// Дополнительные карты для клавиш (не входят в движок)
extern std::map<int, bool> keyStates;
std::map<int, bool> specialKeyStates;

void keyboardFunc(unsigned char key, int, int) {
    keyStates[key] = true;
}

void specialFunc(int key, int, int) {
    specialKeyStates[key] = true;
}

void specialUpFunc(int key, int, int) {
    specialKeyStates[key] = false;
}

// Глобальные переменные для анимации
float speed = 1.0f;
bool paused = false;

// Вершины для фигур (относительно центра)
float squareVerts[8] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
float triVerts[6] = {0.0f, 0.7f, -0.6f, -0.5f, 0.6f, -0.5f};
float smallTriVerts[6] = {0.0f, 0.5f, -0.4f, -0.4f, 0.4f, -0.4f};

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Управление с клавиатуры (пробел – пауза, стрелки – скорость)
    if (keyStates[' ']) paused = !paused;
    if (specialKeyStates[GLUT_KEY_UP]) speed += 0.05f;
    if (specialKeyStates[GLUT_KEY_DOWN]) speed -= 0.05f;
    if (speed < 0.1f) speed = 0.1f;

    // При паузе tick не увеличивается (имитация заморозки)
    static int lastTick = 0;
    if (!paused) lastTick = tick; else tick = lastTick;
    float t = tick * 0.02f * speed;

    // ---- Центральный квадрат (вращается, меняет цвет) ----
    float r = 0.5f + 0.5f * sin(t);
    float g = 0.5f + 0.5f * sin(t + 2.0f);
    float b = 0.5f + 0.5f * sin(t + 4.0f);
    square(1.0f, 0.0f, 0.0f, r, g, b, t * 30, squareVerts, nullptr, {}, false);

    // ---- Треугольник слева (пульсирует размер) ----
    float scaleTri = 0.7f + 0.3f * sin(t * 2.5f);
    triangle(scaleTri, -1.0f, 0.5f, 1.0f, 0.5f, 0.0f, t * 20, triVerts, nullptr);

    // ---- Круг справа (меняет радиус) ----
    float radius = 0.5f + 0.2f * sin(t * 1.8f);
    circle(1.0f, 1.0f, -0.5f, 0.2f, 0.6f, 1.0f, radius, 0.1f, t * 15, 32, 1, nullptr);

    // ---- Маленький квадрат внизу (движется влево-вправо) ----
    float xOffset = 0.8f * sin(t * 1.2f);
    square(0.4f, xOffset, -0.7f, 0.9f, 0.2f, 0.2f, 0, smallTriVerts, nullptr, {}, false);

    glutSwapBuffers();
}

void timerFunc(int) {
    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.0f, 0.0f, 0.0f, 0.0f, "AvoEngine Demo", 800, 600);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboardFunc);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialFunc);
    glutSpecialUpFunc(specialUpFunc);
    glutTimerFunc(16, timerFunc, 0);  // для перерисовки
    glutTimerFunc(16, timer, 0);      // ваш таймер, увеличивает tick

    glutMainLoop();
    return 0;
}