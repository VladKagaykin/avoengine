// avoen_3d_demo.cpp – полная программа с бесконечным полом и редкими жёлтыми сферами
// Управление: W/S – вперёд/назад, A/D – поворот влево/вправо
#include "avoengine.h"
#include <cmath>
using namespace std;

// Глобальные переменные из движка
extern void keyboardUp(unsigned char key, int x, int y);
extern map<int, bool> keyStates;
extern int tick;
extern bool isFullscreen;

// Параметры камеры
float camX = 0.0f, camY = 0.0f, camZ = 5.0f;   // позиция камеры (на уровне пола)
float camYaw = 0.0f;                           // поворот вокруг Y (горизонтальный)
const float camPitch = 0.4f;                  // небольшой наклон вниз, чтобы видеть пол
const float moveSpeed = 0.03f;                  // очень медленное движение
const float rotSpeed = 0.05f;                   // скорость поворота

bool wasFPressed = false;                       // для обработки F

// Структура для хранения экранных координат и признака видимости
struct ScreenPoint {
    float x, y;
    float scale;
    bool valid;   // true, если точка перед камерой
};

// Преобразование мировых координат в экранные с учётом поворота и наклона
ScreenPoint worldToScreen(float wx, float wy, float wz) {
    ScreenPoint sp;
    float dx = wx - camX;
    float dy = wy - camY;
    float dz = wz - camZ;

    // Поворот по горизонтали (yaw)
    float x1 = dx * cos(camYaw) - dz * sin(camYaw);
    float z1 = dx * sin(camYaw) + dz * cos(camYaw);
    float y1 = dy;

    // Поворот по вертикали (pitch) – фиксированный
    float y2 = y1 * cos(camPitch) - z1 * sin(camPitch);
    float z2 = y1 * sin(camPitch) + z1 * cos(camPitch);
    float x2 = x1;

    if (z2 > 0.0f) {    // точка перед камерой
        sp.valid = true;
        float invZ = 1.0f / z2;
        sp.x = x2 * invZ * 0.8f;
        sp.y = y2 * invZ * 0.8f;
        sp.scale = 0.2f * invZ;
    } else {
        sp.valid = false;
    }
    return sp;
}

// Рисование четырёхугольника по четырём точкам (два треугольника)
void drawQuad(ScreenPoint p1, ScreenPoint p2, ScreenPoint p3, ScreenPoint p4, float r, float g, float b) {
    // Даже если некоторые точки невалидны, мы всё равно рисуем – OpenGL отсечёт их.
    // Но для избежания вырожденных треугольников лучше проверять валидность всех.
    if (!p1.valid || !p2.valid || !p3.valid || !p4.valid) return;
    float tri1[6] = { p1.x, p1.y, p2.x, p2.y, p3.x, p3.y };
    float tri2[6] = { p1.x, p1.y, p3.x, p3.y, p4.x, p4.y };
    triangle(1.0f, 0.0f, 0.0f, r, g, b, 0.0f, tri1, nullptr);
    triangle(1.0f, 0.0f, 0.0f, r, g, b, 0.0f, tri2, nullptr);
}

float hash2d(int i, int j) {
    return fabs(sin(i * 12.9898f + j * 78.233f) * 43758.5453f - floor(sin(i * 12.9898f + j * 78.233f) * 43758.5453f));
}

// Отрисовка пола и сфер (бесконечное поле вокруг камеры)
void drawFloorAndSpheres() {
    const int rows = 200;               // сколько рядов вперёд/назад
    const int cols = 200;               // сколько рядов влево/вправо
    float size = 1.0f;                 // размер плитки

    // Диапазон плиток, центрированный относительно камеры (создаёт иллюзию бесконечности)
    int iStart = (int)(camZ / size) - rows/2;
    int iEnd = iStart + rows;
    int jStart = (int)(camX / size) - cols/2;
    int jEnd = jStart + cols;

    for (int i = iStart; i < iEnd; ++i) {
        float wz = i * size;            // мировая координата Z центра плитки
        for (int j = jStart; j < jEnd; ++j) {
            float wx = j * size;        // мировая координата X центра плитки
            float wy = -1.0f;            // уровень пола

            // Проверяем центр плитки – если он не перед камерой, плитку не рисуем
            ScreenPoint center = worldToScreen(wx, wy, wz);
            if (!center.valid) continue;

            // Четыре угла плитки
            float h = size * 0.5f;
            float x1 = wx - h, z1 = wz - h;
            float x2 = wx + h, z2 = wz - h;
            float x3 = wx + h, z3 = wz + h;
            float x4 = wx - h, z4 = wz + h;

            ScreenPoint p1 = worldToScreen(x1, wy, z1);
            ScreenPoint p2 = worldToScreen(x2, wy, z2);
            ScreenPoint p3 = worldToScreen(x3, wy, z3);
            ScreenPoint p4 = worldToScreen(x4, wy, z4);

            // Шахматная раскраска плитки
            bool even = ((i + j) & 1) == 0;
            float r = even ? 0.5f : 0.3f;
            float g = even ? 0.4f : 0.2f;
            float b = even ? 0.3f : 0.2f;
            drawQuad(p1, p2, p3, p4, r, g, b);

            // С вероятностью 3% рисуем жёлтую сферу над этой плиткой
            if (hash2d(i, j) < 0.03f) {
                float sphereY = hash2d(i + 100, j + 200) * 5.0f; // высота от 0 до 5
                ScreenPoint sp = worldToScreen(wx, sphereY, wz);
                if (sp.valid) {
                    circle(sp.scale, sp.x, sp.y, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 16, 8, nullptr);
                }
            }
        }
    }
}

void keyboard(unsigned char key, int, int) {
    keyStates[key] = true;
}

void updateCamera() {
    if (keyStates['a'] || keyStates['A']) camYaw -= rotSpeed;
    if (keyStates['d'] || keyStates['D']) camYaw += rotSpeed;
    float forward = 0.0f;
    if (keyStates['w'] || keyStates['W']) forward += moveSpeed;
    if (keyStates['s'] || keyStates['S']) forward -= moveSpeed;
    if (forward != 0.0f) {
        float dirX = sin(camYaw);
        float dirZ = cos(camYaw);
        camX += forward * dirX;
        camZ += forward * dirZ;
    }
    if (keyStates[27]) exit(0);
    bool fPressed = keyStates['f'] || keyStates['F'];
    if (fPressed && !wasFPressed) {
        isFullscreen = !isFullscreen;
        if (isFullscreen)
            glutFullScreen();
        else {
            glutReshapeWindow(800, 600);
            glutPositionWindow(100, 100);
        }
    }
    wasFPressed = fPressed;
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    updateCamera();
    drawFloorAndSpheres();
    glutSwapBuffers();
}

void idle() {
    glutPostRedisplay();
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.2f, 1.0f, "avoengine_3D_demo", 800, 600);

    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutDisplayFunc(display);
    glutIdleFunc(idle);

    glutMainLoop();
    return 0;
}