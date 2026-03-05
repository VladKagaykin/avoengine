#define MINIAUDIO_IMPLEMENTATION
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_ALSA
#include "miniaudio.h"
#include "avoengine.h"
#include <iostream>
#include <cmath>
#include <SOIL/SOIL.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <chrono>

using namespace std;

int window_w, window_h, screen_w, screen_h;

static map<string, GLuint> textureCache;
static ma_engine audio_engine;

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

void deleteTexture(const char* filename) {
    auto it = textureCache.find(filename);
    if (it == textureCache.end()) {
        cout << "Texture not found in cache: " << filename << endl;
        return;
    }

    GLuint textureID = it->second;
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }

    textureCache.erase(it);
}

void clearTextureCache() {
    for (auto& pair : textureCache) {
        if (pair.second != 0) {
            glDeleteTextures(1, &pair.second);
        }
    }
    textureCache.clear();
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
            float rotate, float* vertices, const char* texture_file) {
    glColor3f(r, g, b);
    float angle_rad = rotate * M_PI / -180.0f;
    
    if (texture_file != nullptr) {
        glEnable(GL_TEXTURE_2D);
        const char* texture_to_load = texture_file;
        GLuint textureID = loadTextureFromFile(texture_to_load);
        glBindTexture(GL_TEXTURE_2D, textureID);
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
    ma_engine_listener_set_position(&audio_engine, 0, eye_x, eye_y, eye_z);
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
    ma_engine_listener_set_position(&audio_engine, 0, eye_x, eye_y, eye_z);
    ma_engine_listener_set_direction(&audio_engine, 0, dir_x, dir_y, dir_z);
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

void init_audio() {
    ma_result result = ma_engine_init(NULL, &audio_engine);
    if (result != MA_SUCCESS) {
        cout << "Failed to init audio engine: " << result << endl;
        return;
    }
    cout << "Audio engine initialized OK" << endl;
    
    // проверим что engine вообще работает простым тестом
    ma_uint32 channels = ma_engine_get_channels(&audio_engine);
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio_engine);
    cout << "Channels: " << channels << " SampleRate: " << sampleRate << endl;
}

void play_sound(const char* filename, float volume) {
    ma_sound* sound = new ma_sound();
    ma_sound_init_from_file(&audio_engine, filename,
        MA_SOUND_FLAG_ASYNC, NULL, NULL, sound);
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);
    ma_sound_set_end_callback(sound, [](void* userData, ma_sound* pSound) {
        ma_sound_uninit(pSound);
        delete pSound;
    }, nullptr);
}

void play_sound_3d(const char* filename, float x, float y, float z, float volume) {
    ma_sound* sound = new ma_sound();
    ma_sound_init_from_file(&audio_engine, filename,
        MA_SOUND_FLAG_ASYNC, NULL, NULL, sound);
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_position(sound, x, y, z);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);
    ma_sound_set_end_callback(sound, [](void* userData, ma_sound* pSound) {
        ma_sound_uninit(pSound);
        delete pSound;
    }, nullptr);
}

void play_sound_3d_loop(const char* filename, float x, float y, float z, float volume) {
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(&audio_engine, filename,
        0, NULL, NULL, sound);  // убрали MA_SOUND_FLAG_ASYNC
    
    if (result != MA_SUCCESS) {
        cout << "Failed to load sound: " << filename << " error: " << result << endl;
        delete sound;
        return;
    }
    
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_position(sound, x, y, z);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_set_looping(sound, MA_TRUE);
    ma_sound_start(sound);
}

static ma_noise noise_source;
static ma_sound noise_sound;

void play_white_noise_3d(float x, float y, float z, float volume) {
    ma_noise_config noiseCfg = ma_noise_config_init(
        ma_format_f32, 2, ma_noise_type_white, 0, 0.3f);
    ma_noise_init(&noiseCfg, NULL, &noise_source);

    ma_audio_buffer_config bufCfg = ma_audio_buffer_config_init(
        ma_format_f32, 2, 48000, NULL, NULL);
    
    // генерируем 1 секунду шума
    static float noiseData[48000 * 2];
    ma_noise_read_pcm_frames(&noise_source, noiseData, 48000, NULL);

    ma_audio_buffer* pBuffer;
    bufCfg.pData = noiseData;
    ma_audio_buffer_alloc_and_init(&bufCfg, &pBuffer);

    ma_sound_init_from_data_source(&audio_engine, pBuffer, 0, NULL, &noise_sound);
    ma_sound_set_positioning(&noise_sound, ma_positioning_absolute);
    ma_sound_set_position(&noise_sound, x, y, z);
    ma_sound_set_spatialization_enabled(&noise_sound, MA_TRUE);
    ma_sound_set_volume(&noise_sound, volume);
    ma_sound_set_looping(&noise_sound, MA_TRUE);
    ma_sound_start(&noise_sound);
}

void draw_performance_hud(int win_w, int win_h) {
    static long prev_cpu = 0;
    static double cpu = 0;
    static long ram_kb = 0;
    static auto prev_time = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - prev_time).count();

    if (elapsed >= 1.0) {
        FILE* f = fopen("/proc/self/status", "r");
        char line[128];
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, "VmRSS: %ld", &ram_kb) == 1) break;
        fclose(f);

        long utime, stime;
        FILE* s = fopen("/proc/self/stat", "r");
        fscanf(s, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &utime, &stime);
        fclose(s);

        long cur_cpu = utime + stime;
        cpu = (cur_cpu - prev_cpu) / (double)sysconf(_SC_CLK_TCK) / elapsed*10.0;
        prev_cpu  = cur_cpu;
        prev_time = now;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %ld MB  CPU: %.1f%%", ram_kb / 1024, cpu);

    begin_2d(win_w, win_h);
    draw_text(buf, 10.0f, win_h - 20.0f, GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);
    end_2d();
}

void setup_display(int* argc, char** argv, float r, float g, float b, float a,
                   const char* name, int w, int h) {
    init_audio();
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