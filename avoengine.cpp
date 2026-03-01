#include "avoengine.h"
#include <iostream>
#include <cmath>
#include <SOIL/SOIL.h>
#include <GL/glu.h>
#include <GL/glut.h>

using namespace std;

int window_w, window_h, screen_w, screen_h;

static map<string, GLuint> textureCache;

struct CameraParams {
    float fov, near, far;
    float eye_x, eye_y, eye_z;
    float center_x, center_y, center_z;
    float up_x, up_y, up_z;
} camera = {false};

GLuint loadTextureFromFile(const char* filename) {
    if (textureCache.find(filename) != textureCache.end()) {
        return textureCache[filename];
    }

    GLuint textureID;
    int width, height;
    unsigned char* image = SOIL_load_image(filename, &width, &height, 0, SOIL_LOAD_RGBA);

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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

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
            vector<const char*> textures, bool condition, const int max_tick, int tick) {
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

void draw_text(const char* text, float x, float y, void* font, float r, float g, float b) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; ++c) {
        glutBitmapCharacter(font, *c);
    }
}

void setup_camera(float fov, float eye_x, float eye_y, float eye_z,
                  float pitch, float yaw) {
    camera.fov = fov;
    camera.near = 0.1f;
    camera.far = 1000.0f;
    camera.eye_x = eye_x; camera.eye_y = eye_y; camera.eye_z = eye_z;

    // pitch — вертикальный наклон (градусы), yaw — горизонтальный
    float pitch_rad = pitch * M_PI / 180.0f;
    float yaw_rad   = yaw   * M_PI / 180.0f;

    float dir_x = cos(pitch_rad) * sin(yaw_rad);
    float dir_y = sin(pitch_rad);
    float dir_z = cos(pitch_rad) * cos(yaw_rad);

    camera.center_x = eye_x + dir_x;
    camera.center_y = eye_y + dir_y;
    camera.center_z = eye_z + dir_z;

    camera.up_x = 0.0f; camera.up_y = 1.0f; camera.up_z = 0.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // aspect временно 1.0, changeSize3D пересчитает при reshape
    gluPerspective(fov, 1.0f, camera.near, camera.far);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x, eye_y, eye_z,
              camera.center_x, camera.center_y, camera.center_z,
              0.0f, 1.0f, 0.0f);
}

void draw3DObject(float center_x, float center_y, float center_z,
                  double r, double g, double b,
                  const char* texture_file,
                  const vector<float>& vertices,   // x,y,z подряд
                  const vector<int>& indices,       // тройки — треугольники
                  const vector<float>& texcoords) { // s,t подряд

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
    glTranslatef(center_x, center_y, center_z);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertices.data());

    if (!texcoords.empty() && texture_file != nullptr) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords.data());
    }

    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, indices.data());

    glDisableClientState(GL_VERTEX_ARRAY);
    if (!texcoords.empty() && texture_file != nullptr) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    glPopMatrix();

    if (texture_file != nullptr) {
        glDisable(GL_TEXTURE_2D);
    }
}

void cube3D(float scale,
            float center_x, float center_y, float center_z,
            double r, double g, double b,
            float rotate_x, float rotate_y, float rotate_z,
            const char* texture_file) {
    std::vector<float> vertices = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1
    };
    std::vector<int> indices = {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        0,3,7, 0,7,4,
        1,5,6, 1,6,2,
        0,4,5, 0,5,1,
        3,2,6, 3,6,7
    };
    std::vector<float> texcoords = {
        0,0, 1,0, 1,1, 0,1,
        0,0, 1,0, 1,1, 0,1,
        0,0, 1,0, 1,1, 0,1,
        0,0, 1,0, 1,1, 0,1,
        0,0, 1,0, 1,1, 0,1,
        0,0, 1,0, 1,1, 0,1
    };

    glPushMatrix();
    glTranslatef(center_x, center_y, center_z);
    glRotatef(rotate_x, 1,0,0);
    glRotatef(rotate_y, 0,1,0);
    glRotatef(rotate_z, 0,0,1);
    glScalef(scale, scale, scale);

    draw3DObject(0, 0, 0, r, g, b, texture_file, vertices, indices, texcoords);

    glPopMatrix();
}

void sphere3D(float scale,
              float center_x, float center_y, float center_z,
              double r, double g, double b,
              float rotate_x, float rotate_y, float rotate_z,
              float radius, int slices, int stacks,
              const char* texture_file) {
    glColor3f(r, g, b);
    if (texture_file) {
        GLuint tex = loadTextureFromFile(texture_file);
        if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); }
    } else glDisable(GL_TEXTURE_2D);

    glPushMatrix();
    glTranslatef(center_x, center_y, center_z);
    glRotatef(rotate_x, 1,0,0); glRotatef(rotate_y,0,1,0); glRotatef(rotate_z,0,0,1);
    glScalef(scale,scale,scale);

    GLUquadric* q = gluNewQuadric();
    gluQuadricTexture(q, GL_TRUE);
    gluSphere(q, radius, slices, stacks);
    gluDeleteQuadric(q);

    glPopMatrix();
    if (texture_file) glDisable(GL_TEXTURE_2D);
}

void changeSize3D(int w, int h) {
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)w / (float)h;
    gluPerspective(camera.fov, aspect, camera.near, camera.far);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(camera.eye_x, camera.eye_y, camera.eye_z,
              camera.center_x, camera.center_y, camera.center_z,
              camera.up_x, camera.up_y, camera.up_z);
}

void changeSize2D(int w, int h) {
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
    window_w = w;
    window_h = h;
}

void move_camera(float eye_x, float eye_y, float eye_z,
                 float pitch, float yaw) {
    float pitch_rad = pitch * M_PI / 180.0f;
    float yaw_rad   = yaw   * M_PI / 180.0f;

    // смотрим к origin, а не от него
    float dir_x = -cos(pitch_rad) * sin(yaw_rad);
    float dir_y = -sin(pitch_rad);
    float dir_z = -cos(pitch_rad) * cos(yaw_rad);

    camera.eye_x = eye_x; camera.eye_y = eye_y; camera.eye_z = eye_z;
    camera.center_x = eye_x + dir_x;
    camera.center_y = eye_y + dir_y;
    camera.center_z = eye_z + dir_z;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x, eye_y, eye_z,
              camera.center_x, camera.center_y, camera.center_z,
              0.0f, 1.0f, 0.0f);
}

void begin_2d(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
}

void end_2d() {
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void setup_display(int* argc, char** argv, float r, float g, float b, float a,
                   const char* name, int w, int h) {
    glutInit(argc, argv);
    window_w = glutGet(GLUT_WINDOW_WIDTH);
    window_h = glutGet(GLUT_WINDOW_HEIGHT);
    screen_w = glutGet(GLUT_SCREEN_WIDTH);
    screen_h = glutGet(GLUT_SCREEN_HEIGHT);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(screen_w/4, screen_h/8);
    glutInitWindowSize(w, h);
    glutCreateWindow(name);
    glutReshapeFunc(changeSize2D);
    glClearColor(r, g, b, a);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}