#include <GL/glut.h>
#include <iostream>
#include <map>
#include <cmath>
#include <GL/glu.h>
#include <vector>
#include <SOIL/SOIL.h>

using namespace std;

bool isFullscreen = 0;
bool triangle_texture_load = 0;

int global_width = 500;
int global_height = 500;
float figure_size = 1.0f;
float center_x = 0.0f;
float center_y = 0.0f;
float global_angle = 0.0f;

// Идентификаторы текстур
GLuint textureSquare, textureCircle, textureTriangle;

map<unsigned char, bool> keyStates;

// Функция для загрузки текстуры из файла с помощью SOIL
GLuint loadTextureFromFile(const char* filename) {
    GLuint textureID;
    
    // Загружаем изображение с помощью SOIL
    int width, height;
    unsigned char* image = SOIL_load_image(filename, &width, &height, 0, SOIL_LOAD_RGB);
    
    if (!image) {
        cout << "Ошибка загрузки текстуры: " << filename << endl;
        cout << "SOIL error: " << SOIL_last_result() << endl;
        return 0;
    }
    
    cout << "Текстура загружена: " << filename << " (" << width << "x" << height << ")" << endl;
    
    // Генерируем текстуру OpenGL
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // Устанавливаем параметры текстуры
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Загружаем данные изображения в текстуру
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
    
    // Освобождаем память изображения
    SOIL_free_image_data(image);
    
    return textureID;
}

// Функция для создания резервной текстуры (если файл не найден)
GLuint createFallbackTexture() {
    GLuint textureID;
    const int size = 64;
    vector<unsigned char> image(size * size * 3);
    
    // Создаем простую текстуру шахматной доски
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int idx = (y * size + x) * 3;
            bool isBlack = ((x / 8) % 2) ^ ((y / 8) % 2);
            
            if (isBlack) {
                image[idx] = 0;      // R
                image[idx+1] = 0;    // G
                image[idx+2] = 0;    // B
            } else {
                image[idx] = 255;    // R
                image[idx+1] = 255;  // G
                image[idx+2] = 255;  // B
            }
        }
    }
    
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data());
    
    return textureID;
}

void limiter() {
    if (figure_size < 0.0f) figure_size = 0.0f;
    if (global_angle < 0.0f) global_angle += 360.0f;
    if (global_angle >= 360.0f) global_angle -= 360.0f;
}

void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad) {
    float translated_x = x - center_x;
    float translated_y = y - center_y;
    
    float rotated_x = translated_x * cos(angle_rad) - translated_y * sin(angle_rad);
    float rotated_y = translated_x * sin(angle_rad) + translated_y * cos(angle_rad);
    
    x = rotated_x + center_x;
    y = rotated_y + center_y;
}

void triangle(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices,bool texture_enable, const char* texture) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;
    if(texture_enable){
        if(!triangle_texture_load){
            textureTriangle = loadTextureFromFile(texture);
            triangle_texture_load = 1;
        }
    }
    

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureTriangle);
    
    glBegin(GL_TRIANGLES);
    
    // Координаты текстуры для треугольника
    float texCoords[6] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f};

    for (int i = 0; i < 3; i++) {
        float point_x = vertices[i*2];
        float point_y = vertices[i*2 + 1];
        
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        glVertex2f((center_x + point_x) * local_size,
                   (center_y + point_y) * local_size);
    }

    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureSquare);
    
    glBegin(GL_QUADS);

    // Координаты текстуры для квадрата
    float texCoords[8] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 4; i++) {
        float point_x = vertices[i*2];
        float point_y = vertices[i*2+1];
        
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        glVertex2f((center_x + point_x) * local_size,
                   (center_y + point_y) * local_size);
    }

    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void circle(float local_size, float x, float y, double r, double g, double b, float radius, float in_radius, float rotate, int slices, int loops) {
    glColor3f(r, g, b);
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureCircle);
    
    glPushMatrix();
    glTranslatef(center_x * local_size, center_y * local_size, 0.0f);
    glRotatef(rotate*-1, 0.0f, 0.0f, 1.0f);
    glScalef(local_size/2, local_size/2, 1.0f);
    
    GLUquadric* quadric = gluNewQuadric();
    gluQuadricTexture(quadric, GL_TRUE);
    gluQuadricDrawStyle(quadric, GLU_FILL);
    
    // Исправление ориентации текстуры
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1.0f, -1.0f, 1.0f); // Отражение по Y
    glRotatef(0.0f, 0.0f, 0.0f, 1.0f); // Поворот на 180 градусов
    
    gluDisk(quadric, in_radius, radius, slices, loops);
    
    // Восстанавливаем матрицу текстуры
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    
    gluDeleteQuadric(quadric);
    glPopMatrix();
    
    glDisable(GL_TEXTURE_2D);
}

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // square(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f});
    //circle(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, global_angle, 6, 1);
    triangle(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},1, "src/penza.png");
    triangle_texture_load = 0;
    triangle(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle-180, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},0, "src/diskriminant.png");
    
    glutSwapBuffers();
}

void changeSize(int w, int h) {
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float ratio = w / (float)h;
    if (w <= h)
        glOrtho(-1, 1, -1/ratio, 1/ratio, 1, -1);
    else
        glOrtho(-1*ratio, 1*ratio, -1, 1, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void setup_display(int* argc, char** argv, float r, float g, float b, float a) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(500, 500);

    glutCreateWindow("avoengine with texture loading");
    glutDisplayFunc(displayWrapper);
    glutReshapeFunc(changeSize);
    glClearColor(r, g, b, a);
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
        case 'r': case 'R': // Перезагрузка текстур
            glDeleteTextures(1, &textureSquare);
            glDeleteTextures(1, &textureCircle);
            glDeleteTextures(1, &textureTriangle);
            glutPostRedisplay();
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
    glutTimerFunc(16, timer, 0);
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.2f, 0.2f, 0.2f, 1.0f);
    
    glEnable(GL_TEXTURE_2D);
    
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    
    // Освобождаем текстуры
    glDeleteTextures(1, &textureSquare);
    glDeleteTextures(1, &textureCircle);
    glDeleteTextures(1, &textureTriangle);
    
    return 0;
}