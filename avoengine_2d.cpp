#include <GL/glut.h>
#include <iostream>
#include <map>
#include <cmath>
#include <GL/glu.h>

using namespace std;

bool isFullscreen = 0;
int global_width = 500;
int global_height = 500;
float figure_size = 1.0f;
float center_x = 0.0f;
float center_y = 0.0f;
float global_angle = 0.0f;

map<unsigned char, bool> keyStates;

void limiter() {
    if (figure_size < 0.0f) figure_size = 0.0f;
    if (global_angle < 0.0f) global_angle += 360.0f;
    if (global_angle >= 360.0f) global_angle -= 360.0f;
}

// Функция для поворота точки вокруг центра
void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad) {
    // Сначала перемещаем точку относительно центра
    float translated_x = x - center_x;
    float translated_y = y - center_y;
    
    // Затем поворачиваем
    float rotated_x = translated_x * cos(angle_rad) - translated_y * sin(angle_rad);
    float rotated_y = translated_x * sin(angle_rad) + translated_y * cos(angle_rad);
    
    // Возвращаем обратно
    x = rotated_x + center_x;
    y = rotated_y + center_y;
}

void triangle(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;

    glBegin(GL_TRIANGLES);

    for (int i = 0; i < 3; i++) {
        float point_x = vertices[i*2];     // x координата
        float point_y = vertices[i*2 + 1]; // y координата
        
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        glVertex2f((center_x + point_x) * local_size,
                   (center_y + point_y) * local_size);
    }

    glEnd();
}

void square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices) {
    glColor3f(r, g, b);

    // Преобразуем угол в радианы
    float angle_rad = rotate * M_PI / -180.0f;
    glBegin(GL_TRIANGLE_STRIP);

    // Применяем поворот к каждой вершине относительно центра
    for (int i = 0; i < 4; i++) {
        float point_x = vertices[i*2];
        float point_y = vertices[i*2+1];
        
        // Поворачиваем точку вокруг центра (0,0)
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        // Масштабируем, перемещаем и применяем общий центр
        glVertex2f((center_x + point_x) * local_size,
                   (center_y + point_y) * local_size);
    }

    glEnd();
}

void circle(float local_size, float x, float y, double r, double g, double b, float radius,float in_radius, float rotate, int slices, int loops) {
    glColor3f(r, g, b);
    
    // Сохраняем текущую матрицу
    glPushMatrix();
    
    // Применяем преобразования относительно общего центра
    glTranslatef(center_x * local_size, center_y * local_size, 0.0f);
    glRotatef(rotate*-1, 0.0f, 0.0f, 1.0f);
    glScalef(local_size/2, local_size/2, 1.0f);
    
    // Создаем объект квадрики
    GLUquadric* quadric = gluNewQuadric();
    gluQuadricDrawStyle(quadric, GLU_FILL);
    
    // Рисуем диск (центр теперь в точке (0,0) после трансформаций)
    gluDisk(quadric, in_radius, radius, slices, loops);
    
    // Удаляем объект квадрики
    gluDeleteQuadric(quadric);
    
    // Восстанавливаем матрицу
    glPopMatrix();
}

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    square(figure_size, center_x, center_y, 1.0f, 0.0f, 0.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f});
    circle(figure_size, center_x, center_y, 0.0f, 1.0f, 0.0f, 1.0f, 0.2f, global_angle, 15, 1);
    triangle(figure_size, center_x, center_y, 0.0f, 0.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f});
    
    glutSwapBuffers();
}

void renderScene(void)
{
glClear(GL_COLOR_BUFFER_BIT);

glRectf(-25.0f,25.0f,25.0f,-25.0f);

glFlush();
}

void changeSize(int w, int h)
{
if (h == 0)
    h=1;
glViewport(0,0,w,h);
glMatrixMode(GL_PROJECTION);
glLoadIdentity();
float ratio = w/(float)h;
if (w<=h)
    glOrtho (-1,1,-1/ratio, 1/ratio, 1,-1);
else
    glOrtho (-1*ratio,1*ratio, -1,1,1,-1);
glMatrixMode(GL_MODELVIEW);
glLoadIdentity();
}

void setup_display(int* argc, char** argv, float r, float g, float b, float a) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(100,100);
    glutInitWindowSize(500,500);

    glutCreateWindow("avoengine");
    glutDisplayFunc(renderScene);
    glutReshapeFunc(changeSize);
    glClearColor(r,g,b,a);
}

void processMovement() {
    bool moved = false;

    if (keyStates['w'] || keyStates['W']) {
        figure_size += 0.01f;
        moved = true;
    }
    if (keyStates['s'] || keyStates['S']) {
        figure_size -= 0.01f;
        moved = true;
    }
    if (keyStates['y'] || keyStates['Y']) {
        center_y += 0.01f;
        moved = true;
    }
    if (keyStates['h'] || keyStates['H']) {
        center_y -= 0.01f;
        moved = true;
    }
    if (keyStates['g'] || keyStates['G']) {
        center_x -= 0.01f;
        moved = true;
    }
    if (keyStates['j'] || keyStates['J']) {
        center_x += 0.01f;
        moved = true;
    }
    if (keyStates['a'] || keyStates['A']) {
        global_angle -= 1.0f;
        moved = true;
    }
    if (keyStates['d'] || keyStates['D']) {
        global_angle += 1.0f;
        moved = true;
    }

    if (moved) {
        limiter();
        glutPostRedisplay();
    }
}

void keyboardDown(unsigned char key, int, int) {
    keyStates[key] = true;

    switch(key) {
        case 27: // ESC
            exit(0);
        case 'f': case 'F':
            isFullscreen = !isFullscreen;
            isFullscreen ? glutFullScreen() : glutReshapeWindow(500, 500);
            break;
    }
    processMovement();
}

void keyboardUp(unsigned char key, int, int) {
    keyStates[key] = false;
    cout<<key<<endl;
}

void timer(int value) {
    processMovement();
    glutTimerFunc(16, timer, 0); // ~60 FPS
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    return 0;
}