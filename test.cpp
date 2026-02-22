#include <GL/glut.h>
#include <iostream>
#include <map>
#include <cmath>
#include <GL/glu.h>
#include <SOIL/SOIL.h>
#include <vector>

using namespace std;

bool isFullscreen = 0;
bool is_moving = 0;

int tick = 0;
const int max_tick = 64;

int global_width = 1080;
int global_height = 720;
float figure_size = 1.0f;
float center_x = 0.0f;
float center_y = 0.0f;
float global_angle = 0.0f;

map<string, GLuint> textureCache;

map<int, bool> keyStates;

GLuint loadTextureFromFile(const char* filename) {
    if (textureCache.find(filename) != textureCache.end()) {
        return textureCache[filename];
    }
    
    GLuint textureID;
    
    int width, height;
    unsigned char* image = SOIL_load_image(filename, &width, &height, 0, SOIL_LOAD_RGB);
    
    if (!image) {
        cout << "Ошибка загрузки текстуры: " << filename << endl;
        cout << "SOIL error: " << SOIL_last_result() << endl;
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
    if(tick>max_tick) tick = 0;
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
    if (keyStates['t'] || keyStates['T']) {
        moved = true;
    }

    if (moved) {
        limiter();
        glutPostRedisplay();
        is_moving = 1;
    }else{
        is_moving = 0;
    }
}

void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad) {
    float translated_x = x - center_x;
    float translated_y = y - center_y;
    
    float rotated_x = translated_x * cos(angle_rad) - translated_y * sin(angle_rad);
    float rotated_y = translated_x * sin(angle_rad) + translated_y * cos(angle_rad);
    
    x = rotated_x + center_x;
    y = rotated_y + center_y;
}

void triangle(float scale, float center_x, float center_y, double r, double g, double b, float rotate, float* vertices, const char* texture_file = nullptr) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;

    if (texture_file != nullptr) {
        GLuint textureID = loadTextureFromFile(texture_file);
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
        float point_x = vertices[i*2];
        float point_y = vertices[i*2 + 1];
        
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        float scaled_x = point_x * scale;
        float scaled_y = point_y * scale;
        
        if (texture_file != nullptr) {
            glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        }
        glVertex2f(center_x + scaled_x, center_y + scaled_y);
    }

    glEnd();
    
    if (texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices, const char* texture_file, vector<const char*> textures, bool condition) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;
    if (texture_file != nullptr) {
        glEnable(GL_TEXTURE_2D);
        const char* texture_to_load = texture_file;
        if (!textures.empty() && condition) {
            int count_textures = textures.size();
            int frame_tick = max_tick / count_textures;
            int frame_index = std::min(tick / frame_tick, count_textures - 1);
            texture_to_load = textures[frame_index];
        }
        GLuint textureID = loadTextureFromFile(texture_to_load);
        if (textureID != 0) {
            glBindTexture(GL_TEXTURE_2D, textureID);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_QUADS);
    
    float texCoords[8] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 4; i++) {
        float point_x = vertices[i*2];
        float point_y = vertices[i*2+1];
        
        rotatePoint(point_x, point_y, 0.0f, 0.0f, angle_rad);
        
        if (texture_file != nullptr) {
            glTexCoord2f(texCoords[i*2], texCoords[i*2+1]);
        }
        glVertex2f((x + point_x) * local_size,
                   (y + point_y) * local_size);
    }

    glEnd();
    
    if (texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void circle(float scale, float center_x, float center_y, double r, double g, double b, float radius, float in_radius, float rotate, int slices, int loops,const char* texture_file = nullptr) {
    glColor3f(r, g, b);
    
    if (texture_file != nullptr) {
        GLuint textureID = loadTextureFromFile(texture_file);
        if (textureID != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureID);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    glPushMatrix();
    // Перемещаемся в центр (без умножения на scale)
    glTranslatef(center_x, center_y, 0.0f);
    glRotatef(rotate*-1, 0.0f, 0.0f, 1.0f);
    // Масштабируем размеры круга
    glScalef(scale, scale, 1.0f);
    
    GLUquadric* quadric = gluNewQuadric();
    gluQuadricTexture(quadric, GL_TRUE);
    gluQuadricDrawStyle(quadric, GLU_FILL);
    
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1.0f, -1.0f, 1.0f);
    
    gluDisk(quadric, in_radius, radius, slices, loops);
    
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    
    gluDeleteQuadric(quadric);
    glPopMatrix();
    
    if (texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    vector<const char*> anim_textures = {"src/penza.png", "src/penza_low.png","src/prime.png"};

    square(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f,
                                                                                       0.5f, -0.5f, 
                                                                                       0.5f, 0.5f, 
                                                                                       -0.5f, 0.5f}, "src/god_png.png",anim_textures,is_moving);
    // circle(figure_size, center_x*1.5, center_y*1.5, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, global_angle, 7, 1, "src/diskriminant.png");
    // triangle(figure_size, center_x*2, center_y*2, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},"src/penza.png");
    glutPostRedisplay();
    glutSwapBuffers();
}

void renderScene(void)
{
glClear(GL_COLOR_BUFFER_BIT);

glRectf(-25.0f,25.0f,25.0f,-25.0f);

glFlush();
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
    glutInitWindowSize(global_width, global_height);

    glutCreateWindow("avoengine");
    glutDisplayFunc(renderScene);
    glutReshapeFunc(changeSize);
    glClearColor(r, g, b, a);
}

void keyboardDown(unsigned char key, int, int) {
    keyStates[key] = true;

    switch(key) {
        case 27: // ESC
            exit(0);
        case 'f': case 'F':
            isFullscreen = !isFullscreen;
            isFullscreen ? glutFullScreen() : glutReshapeWindow(global_width, global_height);
            break;
        case 'r': case 'R': // Перезагрузка текстур
            for (auto& texture : textureCache) {
                glDeleteTextures(1, &texture.second);
            }
            textureCache.clear();
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
    tick ++;
    limiter();
    // cout<<tick<<endl;
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.2f, 0.2f, 0.2f, 1.0f);
    
    glEnable(GL_TEXTURE_2D);
    
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    
    for (auto& texture : textureCache) {
        glDeleteTextures(1, &texture.second);
    }
    
    return 0;
}