#include <GL/glut.h>
#include <iostream>
#include <map>
#include <cmath>
#include <GL/glu.h>
#include <vector>
#include <SOIL/SOIL.h>

using namespace std;

bool isFullscreen = false;

int global_width = 500;
int global_height = 500;
float figure_size = 1.0f;
float center_x = 0.0f;
float center_y = 0.0f;
float global_angle = 0.0f;

map<string, GLuint> textureCache;
map<unsigned char, bool> keyStates;

// Константы физики
const float GRAVITY = -2.0000f;
const float FRICTION = 0.99f;
const float BOUNCE_FACTOR = 0.3f;

// Типы объектов
enum ObjectType { SQUARE_OBJ, CIRCLE_OBJ, TRIANGLE_OBJ };

// Структура для физического объекта
struct PhysicsObject {
    ObjectType type;
    float x, y;               // Позиция
    float velocity_x, velocity_y; // Скорость
    float size;               // Размер
    float angle;              // Угол поворота
    bool has_gravity;         // Подчиняется гравитации
    bool has_collision;       // Имеет коллизии
    bool is_static;           // Статичный объект
    float radius;             // Радиус для коллизий
    
    // Параметры для рисования
    float color[3];           // Цвет
    const char* texture_file; // Текстура
    
    // Параметры формы
    union {
        struct {
            float vertices[8];
        } square;
        struct {
            float outer_radius, inner_radius;
            int slices, loops;
        } circle;
        struct {
            float vertices[6];
        } triangle;
    } params;
    
    // Конструктор
    PhysicsObject() : type(SQUARE_OBJ), x(0), y(0), velocity_x(0), velocity_y(0), 
                     size(1), angle(0), has_gravity(false), has_collision(false),
                     is_static(false), radius(0.5f), texture_file(nullptr) {
        color[0] = color[1] = color[2] = 1.0f;
    }
};

// Глобальный вектор объектов
vector<PhysicsObject> physicsObjects;

// ID управляемого объекта
int controlled_object_id = -1;

GLuint loadTextureFromFile(const char* filename) {
    if (textureCache.find(filename) != textureCache.end()) {
        return textureCache[filename];
    }
    
    GLuint textureID;
    int width, height;
    unsigned char* image = SOIL_load_image(filename, &width, &height, 0, SOIL_LOAD_RGB);
    
    if (!image) {
        cout << "Ошибка загрузки текстуры: " << filename << endl;
        textureCache[filename] = 0;
        return 0;
    }
    
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
    SOIL_free_image_data(image);
    textureCache[filename] = textureID;
    return textureID;
}

void limiter() {
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

// Функции для создания объектов (возвращают ID объекта)
int createSquare(float x, float y, float size, float angle, 
                float r, float g, float b,
                float* vertices, bool gravity, bool collision, 
                bool is_static = false, const char* texture_file = nullptr) {
    
    PhysicsObject obj;
    obj.type = SQUARE_OBJ;
    obj.x = x;
    obj.y = y;
    obj.size = size;
    obj.angle = angle;
    obj.color[0] = r;
    obj.color[1] = g;
    obj.color[2] = b;
    obj.has_gravity = gravity;
    obj.has_collision = collision;
    obj.is_static = is_static;
    obj.texture_file = texture_file;
    
    // Копируем вершины
    for(int i = 0; i < 8; i++) {
        obj.params.square.vertices[i] = vertices[i];
    }
    
    // Рассчитываем радиус для коллизий
    float half_width = fabs(vertices[0]) * size;
    float half_height = fabs(vertices[1]) * size;
    obj.radius = sqrt(half_width*half_width + half_height*half_height) * 0.5f;
    
    physicsObjects.push_back(obj);
    return physicsObjects.size() - 1;
}

int createCircle(float x, float y, float size, float angle,
                float r, float g, float b,
                float outer_radius, float inner_radius, 
                int slices, int loops, bool gravity, bool collision,
                const char* texture_file = nullptr) {
    
    PhysicsObject obj;
    obj.type = CIRCLE_OBJ;
    obj.x = x;
    obj.y = y;
    obj.size = size;
    obj.angle = angle;
    obj.color[0] = r;
    obj.color[1] = g;
    obj.color[2] = b;
    obj.has_gravity = gravity;
    obj.has_collision = collision;
    obj.is_static = false;
    obj.texture_file = texture_file;
    
    obj.params.circle.outer_radius = outer_radius;
    obj.params.circle.inner_radius = inner_radius;
    obj.params.circle.slices = slices;
    obj.params.circle.loops = loops;
    
    // Радиус для коллизий
    obj.radius = outer_radius * size;
    
    physicsObjects.push_back(obj);
    return physicsObjects.size() - 1;
}

int createTriangle(float x, float y, float size, float angle,
                  float r, float g, float b,
                  float* vertices, bool gravity, bool collision,
                  const char* texture_file = nullptr) {
    
    PhysicsObject obj;
    obj.type = TRIANGLE_OBJ;
    obj.x = x;
    obj.y = y;
    obj.size = size;
    obj.angle = angle;
    obj.color[0] = r;
    obj.color[1] = g;
    obj.color[2] = b;
    obj.has_gravity = gravity;
    obj.has_collision = collision;
    obj.is_static = false;
    obj.texture_file = texture_file;
    
    for(int i = 0; i < 6; i++) {
        obj.params.triangle.vertices[i] = vertices[i];
    }
    
    // Рассчитываем радиус для коллизий
    float max_dist = 0;
    for(int i = 0; i < 3; i++) {
        float dx = vertices[i*2];
        float dy = vertices[i*2+1];
        float dist = sqrt(dx*dx + dy*dy) * size;
        if(dist > max_dist) max_dist = dist;
    }
    obj.radius = max_dist * 0.5f;
    
    physicsObjects.push_back(obj);
    return physicsObjects.size() - 1;
}

// Функции отрисовки объектов
void drawSquare(PhysicsObject& obj) {
    glColor3f(obj.color[0], obj.color[1], obj.color[2]);
    float angle_rad = obj.angle * M_PI / -180.0f;
    
    if (obj.texture_file != nullptr) {
        GLuint textureID = loadTextureFromFile(obj.texture_file);
        if (textureID != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureID);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    glBegin(GL_QUADS);
    float texCoords[8] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    
    for (int i = 0; i < 4; i++) {
        float point_x = obj.params.square.vertices[i*2] * obj.size;
        float point_y = obj.params.square.vertices[i*2+1] * obj.size;
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        if (obj.texture_file != nullptr) {
            glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        }
        glVertex2f(obj.x + point_x, obj.y + point_y);
    }
    glEnd();
    
    if (obj.texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void drawCircle(PhysicsObject& obj) {
    glColor3f(obj.color[0], obj.color[1], obj.color[2]);
    
    if (obj.texture_file != nullptr) {
        GLuint textureID = loadTextureFromFile(obj.texture_file);
        if (textureID != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureID);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    glPushMatrix();
    glTranslatef(obj.x, obj.y, 0.0f);
    glRotatef(-obj.angle, 0.0f, 0.0f, 1.0f);
    
    GLUquadric* quadric = gluNewQuadric();
    gluQuadricTexture(quadric, GL_TRUE);
    gluQuadricDrawStyle(quadric, GLU_FILL);
    
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1.0f, -1.0f, 1.0f);
    
    gluDisk(quadric, 
            obj.params.circle.inner_radius * obj.size,
            obj.params.circle.outer_radius * obj.size,
            obj.params.circle.slices,
            obj.params.circle.loops);
    
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    gluDeleteQuadric(quadric);
    glPopMatrix();
    
    if (obj.texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void drawTriangle(PhysicsObject& obj) {
    glColor3f(obj.color[0], obj.color[1], obj.color[2]);
    float angle_rad = obj.angle * M_PI / -180.0f;
    
    if (obj.texture_file != nullptr) {
        GLuint textureID = loadTextureFromFile(obj.texture_file);
        if (textureID != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureID);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    glBegin(GL_TRIANGLES);
    float texCoords[6] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    
    for (int i = 0; i < 3; i++) {
        float point_x = obj.params.triangle.vertices[i*2] * obj.size;
        float point_y = obj.params.triangle.vertices[i*2+1] * obj.size;
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        if (obj.texture_file != nullptr) {
            glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        }
        glVertex2f(obj.x + point_x, obj.y + point_y);
    }
    glEnd();
    
    if (obj.texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

// Функции для создания объектов с физикой (старый стиль)
void square(float local_size, float x, float y, double r, double g, double b, 
            float rotate, float* vertices, bool gravity, bool collision, 
            bool is_static = false, const char* texture_file = nullptr) {
    
    createSquare(x, y, local_size, rotate, 
                r, g, b,
                vertices, gravity, collision, 
                is_static, texture_file);
}

void circle(float local_size, float x, float y, double r, double g, double b, 
            float outer_radius, float inner_radius, float rotate, 
            int slices, int loops, bool gravity, bool collision, 
            const char* texture_file = nullptr) {
    
    createCircle(x, y, local_size, rotate,
                r, g, b,
                outer_radius, inner_radius, 
                slices, loops, gravity, collision,
                texture_file);
}

void triangle(float local_size, float x, float y, double r, double g, double b, 
              float rotate, float* vertices, bool gravity, bool collision, 
              const char* texture_file = nullptr) {
    
    createTriangle(x, y, local_size, rotate,
                  r, g, b,
                  vertices, gravity, collision,
                  texture_file);
}

// Обновление физики
void updatePhysics(float dt) {
    // Обновляем все объекты
    for (auto& obj : physicsObjects) {
        if (obj.is_static) continue;
        
        // Гравитация
        if (obj.has_gravity) {
            obj.velocity_y += GRAVITY * dt;
        }
        
        // Трение
        obj.velocity_x *= FRICTION;
        obj.velocity_y *= FRICTION;
        
        // Обновляем позицию
        obj.x += obj.velocity_x * dt;
        obj.y += obj.velocity_y * dt;
        
        // Проверка границ экрана
        const float boundary = 4.0f;
        if (obj.has_collision) {
            if (obj.x - obj.radius < -boundary) {
                obj.x = -boundary + obj.radius;
                obj.velocity_x = -obj.velocity_x * BOUNCE_FACTOR;
            }
            if (obj.x + obj.radius > boundary) {
                obj.x = boundary - obj.radius;
                obj.velocity_x = -obj.velocity_x * BOUNCE_FACTOR;
            }
            if (obj.y - obj.radius < -boundary) {
                obj.y = -boundary + obj.radius;
                obj.velocity_y = -obj.velocity_y * BOUNCE_FACTOR;
            }
            if (obj.y + obj.radius > boundary) {
                obj.y = boundary - obj.radius;
                obj.velocity_y = -obj.velocity_y * BOUNCE_FACTOR;
            }
        }
    }
    
    // Проверка коллизий между объектами
    for (size_t i = 0; i < physicsObjects.size(); i++) {
        PhysicsObject& obj1 = physicsObjects[i];
        if (!obj1.has_collision || obj1.is_static) continue;
        
        for (size_t j = i + 1; j < physicsObjects.size(); j++) {
            PhysicsObject& obj2 = physicsObjects[j];
            if (!obj2.has_collision) continue;
            
            float dx = obj1.x - obj2.x;
            float dy = obj1.y - obj2.y;
            float distance = sqrt(dx*dx + dy*dy);
            float minDistance = obj1.radius + obj2.radius;
            
            if (distance < minDistance && distance > 0.001f) {
                // Коллизия обнаружена
                float overlap = minDistance - distance;
                float nx = dx / distance;
                float ny = dy / distance;
                
                // Разделяем объекты
                if (!obj1.is_static) {
                    obj1.x += nx * overlap * 0.5f;
                    obj1.y += ny * overlap * 0.5f;
                }
                if (!obj2.is_static) {
                    obj2.x -= nx * overlap * 0.5f;
                    obj2.y -= ny * overlap * 0.5f;
                }
                
                // Отскок (только если оба не статичны)
                if (!obj1.is_static && !obj2.is_static) {
                    float relativeVelocityX = obj1.velocity_x - obj2.velocity_x;
                    float relativeVelocityY = obj1.velocity_y - obj2.velocity_y;
                    float velocityAlongNormal = relativeVelocityX * nx + relativeVelocityY * ny;
                    
                    if (velocityAlongNormal < 0) {
                        float impulse = -(1 + BOUNCE_FACTOR) * velocityAlongNormal;
                        impulse /= 2.0f;
                        
                        obj1.velocity_x += impulse * nx;
                        obj1.velocity_y += impulse * ny;
                        obj2.velocity_x -= impulse * nx;
                        obj2.velocity_y -= impulse * ny;
                    }
                } else if (!obj1.is_static && obj2.is_static) {
                    // Объект сталкивается со статичным объектом
                    obj1.velocity_x = -obj1.velocity_x * BOUNCE_FACTOR;
                    obj1.velocity_y = -obj1.velocity_y * BOUNCE_FACTOR;
                }
            }
        }
    }
}

// Обновление управляемого объекта
void updateControlledObject() {
    if (controlled_object_id >= 0 && controlled_object_id < physicsObjects.size()) {
        PhysicsObject& obj = physicsObjects[controlled_object_id];
        obj.x = center_x;
        obj.y = center_y;
        obj.size = figure_size;
        obj.angle = global_angle;
    }
}

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Обновляем управляемый объект
    updateControlledObject();
    
    // Рисуем все объекты
    for (auto& obj : physicsObjects) {
        switch (obj.type) {
            case SQUARE_OBJ:
                drawSquare(obj);
                break;
            case CIRCLE_OBJ:
                drawCircle(obj);
                break;
            case TRIANGLE_OBJ:
                drawTriangle(obj);
                break;
        }
    }
    
    glutSwapBuffers();
}

void changeSize(int w, int h) {
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float ratio = w / (float)h;
    if (w <= h)
        glOrtho(-5, 5, -5/ratio, 5/ratio, 1, -1);
    else
        glOrtho(-5*ratio, 5*ratio, -5, 5, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void setup_display(int* argc, char** argv, float r, float g, float b, float a) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(500, 500);

    glutCreateWindow("avoengine");
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
        case 'r': case 'R':
            for (auto& texture : textureCache) {
                glDeleteTextures(1, &texture.second);
            }
            textureCache.clear();
            glutPostRedisplay();
            break;
        case ' ': // Пробел - прыжок
            if (controlled_object_id >= 0 && controlled_object_id < physicsObjects.size()) {
                physicsObjects[controlled_object_id].velocity_y = 0.1f;
            }
            break;
        case '1': // Телепортация управляемого объекта
            if (controlled_object_id >= 0 && controlled_object_id < physicsObjects.size()) {
                physicsObjects[controlled_object_id].x = 0;
                physicsObjects[controlled_object_id].y = 3;
                center_x = 0;
                center_y = 3;
                physicsObjects[controlled_object_id].velocity_x = 0;
                physicsObjects[controlled_object_id].velocity_y = 0;
            }
            break;
    }
    processMovement();
}

void keyboardUp(unsigned char key, int, int) {
    keyStates[key] = false;
}

void timer(int value) {
    static int last_time = glutGet(GLUT_ELAPSED_TIME);
    int current_time = glutGet(GLUT_ELAPSED_TIME);
    float dt = (current_time - last_time) / 1000.0f; // В секундах
    last_time = current_time;
    
    processMovement();
    updatePhysics(dt);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

// Инициализация объектов (вызывается один раз)
void initObjects() {
    physicsObjects.clear();
    
    // Управляемый квадрат (первый объект)
    controlled_object_id = createSquare(center_x, center_y, figure_size, global_angle,
                1.0f, 1.0f, 1.0f,
                (float[]){-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f},
                false, true, false, "src/god_png.png");
    
    // Круг с гравитацией и коллизией
    createCircle(1.5f, 2.0f, 1.0f, 0.0f,
                1.0f, 1.0f, 1.0f,
                1.0f, 0.2f, 7, 1,
                true, true, "src/diskriminant.png");
    
    // Треугольник с гравитацией и коллизией
    createTriangle(-1.5f, 2.0f, 1.0f, 0.0f,
                  1.0f, 1.0f, 1.0f,
                  (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},
                  true, true, "src/penza.png");
    
    // Статичная платформа
    createSquare(0.0f, -2.0f, 3.0f, 0.0f,
                0.5f, 0.5f, 0.5f,
                (float[]){-0.5f, -0.1f, 0.5f, -0.1f, 0.5f, 0.1f, -0.5f, 0.1f},
                false, true, true, "src/prime.png");
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.2f, 0.2f, 0.2f, 1.0f);
    
    glEnable(GL_TEXTURE_2D);
    
    // Инициализация объектов один раз
    initObjects();
    
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    
    // Очистка
    for (auto& texture : textureCache) {
        glDeleteTextures(1, &texture.second);
    }
    
    return 0;
}