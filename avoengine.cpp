#include "avoengine.h"
#include <iostream>
#include <cmath>
#include <SOIL/SOIL.h>
#include <GL/glu.h>

using namespace std;

bool isFullscreen = false;
int tick = 0;
const int max_tick = 64;
map<int, bool> keyStates;

static map<string, GLuint> textureCache;

GLuint loadTextureFromFile(const char* filename) {
    if (textureCache.find(filename) != textureCache.end()) {
        return textureCache[filename];
    }
    
    GLuint textureID;
    int width, height;
    unsigned char* image = SOIL_load_image(filename, &width, &height, 0, SOIL_LOAD_RGB);
    
    if (!image) {
        cout << "error to load texture: " << filename << endl;
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

void rotatePoint(float& x, float& y, float center_x, float center_y, float angle_rad) {
    float translated_x = x - center_x;
    float translated_y = y - center_y;
    
    float rotated_x = translated_x * cos(angle_rad) - translated_y * sin(angle_rad);
    float rotated_y = translated_x * sin(angle_rad) + translated_y * cos(angle_rad);
    
    x = rotated_x + center_x;
    y = rotated_y + center_y;
}

void triangle(float scale, float center_x, float center_y, double r, double g, double b, 
              float rotate, float* vertices, const char* texture_file) {
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

void square(float local_size, float x, float y, double r, double g, double b, 
            float rotate, float* vertices, const char* texture_file, 
            vector<const char*> textures, bool condition) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;
    
    if (texture_file != nullptr) {
        glEnable(GL_TEXTURE_2D);
        const char* texture_to_load = texture_file;
        if (!textures.empty() && condition) {
            int count_textures = textures.size();
            int frame_tick = max_tick / count_textures;
            int frame_index = min(tick / frame_tick, count_textures - 1);
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
        glVertex2f(x + point_x * local_size, y + point_y * local_size);
    }
    glEnd();
    
    if (texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void circle(float scale, float center_x, float center_y, double r, double g, double b, 
            float radius, float in_radius, float rotate, int slices, int loops,
            const char* texture_file) {
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
    glTranslatef(center_x, center_y, 0.0f);
    glRotatef(rotate * -1, 0.0f, 0.0f, 1.0f);
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

void setup_display(int* argc, char** argv, float r, float g, float b, float a, 
                   const char* name, int w, int h) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(w, h);
    glutCreateWindow(name);
    glutReshapeFunc(changeSize);
    glClearColor(r, g, b, a);
}

void keyboardUp(unsigned char key, int, int) {
    keyStates[key] = false;
}

void timer(int value) {
    glutTimerFunc(16, timer, 0);
    tick++;
}