// подключаем движок и карту
#include "../avoengine.h"
#include "../avomap.h"
#include <GL/glu.h>
#include <GL/glut.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <climits>
#include <vector>
#include <string>
using namespace std;

extern int window_w, window_h;

//              параметры камеры
static float cam_x = 0, cam_y = 0, cam_z = 0;
static float cam_pitch = 0, cam_yaw = 0;
static bool keys[256] = {}, skeys[512] = {};
static const float MOVE_SPEED = 20.0f;
static const float TURN_SPEED = 90.0f;

//              текущий сид мира
static int g_seed = 0;

//              путь к папке maps/
static string g_maps_dir;

//              карта
static GameMap g_map;

//              шум перлина
static int perm[512];

static void initPerlin(int seed) {
    srand(seed);
    for(int i = 0; i < 256; ++i) perm[i] = i;
    for(int i = 255; i > 0; --i) {
        int j = rand() % (i + 1);
        int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }
    for(int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
}

static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float lerp(float a, float b, float t) { return a + t * (b - a); }
static float grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}
static float perlin3(float x, float y, float z) {
    int xi = (int)floorf(x) & 255, yi = (int)floorf(y) & 255, zi = (int)floorf(z) & 255;
    float xf = x - floorf(x), yf = y - floorf(y), zf = z - floorf(z);
    float u = fade(xf), v = fade(yf), w = fade(zf);
    int aaa = perm[perm[perm[xi  ] + yi  ] + zi  ];
    int aba = perm[perm[perm[xi  ] + yi+1] + zi  ];
    int aab = perm[perm[perm[xi  ] + yi  ] + zi+1];
    int abb = perm[perm[perm[xi  ] + yi+1] + zi+1];
    int baa = perm[perm[perm[xi+1] + yi  ] + zi  ];
    int bba = perm[perm[perm[xi+1] + yi+1] + zi  ];
    int bab = perm[perm[perm[xi+1] + yi  ] + zi+1];
    int bbb = perm[perm[perm[xi+1] + yi+1] + zi+1];
    return lerp(
        lerp(lerp(grad3(aaa,xf,yf,zf),    grad3(baa,xf-1,yf,zf),    u),
             lerp(grad3(aba,xf,yf-1,zf),   grad3(bba,xf-1,yf-1,zf),   u), v),
        lerp(lerp(grad3(aab,xf,yf,zf-1),   grad3(bab,xf-1,yf,zf-1),   u),
             lerp(grad3(abb,xf,yf-1,zf-1), grad3(bbb,xf-1,yf-1,zf-1), u), v), w);
}
static float perlin2(float x, float y) { return perlin3(x, y, 0.5f); }

//              геометрия куба
static vector<float> g_cubeVerts;
static vector<int>   g_cubeIdx;

static vector<float> makeCubeVerts() {
    return {
        -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,   0.5f,-0.5f,-0.5f,
        -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,   0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  -0.5f,-0.5f, 0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,   0.5f,-0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f,-0.5f
    };
}
static vector<int> makeCubeIdx() {
    vector<int> idx;
    for(int f = 0; f < 6; ++f) {
        int b = f * 4;
        idx.push_back(b);   idx.push_back(b+1); idx.push_back(b+2);
        idx.push_back(b);   idx.push_back(b+2); idx.push_back(b+3);
    }
    return idx;
}

//              кэш кубов
// пересчитывается только когда камера переходит в новую ячейку
// каждый кадр только рендер из кэша — perlin не вызывается
struct CachedCube { float x, y, z, size, r, g, b; };
static vector<CachedCube> g_cubeCache;
static int g_cacheIcx = INT_MIN;
static int g_cacheIcy = INT_MIN;
static int g_cacheIcz = INT_MIN;

static const int   WORLD_R_XZ = 20;
static const int   WORLD_R_Y  = 12;
static const float CELL       = 6.0f;
static const float MAX_DIST   = 280.0f;

static void rebuildCache(int icx, int icy, int icz) {
    g_cacheIcx = icx;
    g_cacheIcy = icy;
    g_cacheIcz = icz;
    g_cubeCache.clear();
    g_cubeCache.reserve(2000);
    for(int di = -WORLD_R_XZ; di <= WORLD_R_XZ; ++di)
    for(int dk = -WORLD_R_XZ; dk <= WORLD_R_XZ; ++dk)
    for(int dj = -WORLD_R_Y;  dj <= WORLD_R_Y;  ++dj) {
        const int ix = icx + di, iy = icy + dj, iz = icz + dk;
        const float px = ix * CELL, py = iy * CELL, pz = iz * CELL;
        const float cx = icx * CELL, cy = icy * CELL, cz = icz * CELL;
        const float ddx = px - cx, ddy = py - cy, ddz = pz - cz;
        if(ddx*ddx + ddy*ddy + ddz*ddz > MAX_DIST * MAX_DIST) continue;
        float n = perlin3(ix * 0.22f, iy * 0.22f, iz * 0.22f);
        if(n < 0.2f) continue;
        CachedCube c;
        c.x    = px;
        c.y    = py;
        c.z    = pz;
        c.size = (0.4f + n * 1.8f) * CELL * 0.4f;
        c.r    = 0.3f + 0.7f * (perlin2(ix * 0.5f + 10, iz * 0.5f) * 0.5f + 0.5f);
        c.g    = 0.3f + 0.7f * (perlin2(ix * 0.5f, iz * 0.5f + 20) * 0.5f + 0.5f);
        c.b    = 0.3f + 0.7f * (perlin2(ix * 0.5f + 30, iz * 0.5f + 30) * 0.5f + 0.5f);
        g_cubeCache.push_back(c);
    }
}

//              процедурный слой — рендер из кэша
static void layerWorld(float cx, float cy, float cz) {
    const int icx = (int)floorf(cx / CELL);
    const int icy = (int)floorf(cy / CELL);
    const int icz = (int)floorf(cz / CELL);
    if(icx != g_cacheIcx || icy != g_cacheIcy || icz != g_cacheIcz)
        rebuildCache(icx, icy, icz);

    // вектор взгляда с учётом pitch для фрустум-отсечения
    const float yr = cam_yaw   * float(M_PI) / 180.0f;
    const float pr = cam_pitch * float(M_PI) / 180.0f;
    const float fx = cosf(pr) * sinf(yr);
    const float fy = sinf(pr);
    const float fz = cosf(pr) * cosf(yr);
    // половина угла фрустума с запасом
    const float cos_fov = cosf(60.0f * float(M_PI) / 180.0f);

    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, g_cubeVerts.data());

    for(const auto& c : g_cubeCache) {
        // вектор от камеры до куба
        float dx = c.x - cx, dy = c.y - cy, dz = c.z - cz;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        // куб прямо на камере — всегда рисуем
        if(dist > 0.001f) {
            float dot = (dx*fx + dy*fy + dz*fz) / dist;
            // отсекаем если угол между взглядом и кубом больше fov + запас на размер куба
            float slack = c.size / dist;
            if(dot < cos_fov - slack) continue;
        }
        glColor3f(c.r, c.g, c.b);
        glPushMatrix();
        glTranslatef(c.x, c.y, c.z);
        glScalef(c.size, c.size, c.size);
        glDrawElements(GL_TRIANGLES, (int)g_cubeIdx.size(), GL_UNSIGNED_INT, g_cubeIdx.data());
        glPopMatrix();
    }

    glDisableClientState(GL_VERTEX_ARRAY);
}

//              инициализация мира
static void initWorld(int seed) {
    g_seed = seed;
    g_map.objects.clear();
    g_map.entities.clear();
    g_map.sounds.clear();
    g_map.layers.clear();
    g_map.name = to_string(seed);
    initPerlin(seed);
    // сбрасываем кэш чтобы пересчитался с новым сидом
    g_cacheIcx = INT_MIN;
    g_cacheIcy = INT_MIN;
    g_cacheIcz = INT_MIN;
    g_map.layers.push_back(layerWorld);
}

//              сохранения
static vector<string> listSaves() {
    vector<string> files;
    DIR* d = opendir(g_maps_dir.c_str());
    if(!d) return files;
    struct dirent* e;
    while((e = readdir(d)) != nullptr) {
        string name = e->d_name;
        if(name.size() > 4 && name.substr(name.size() - 4) == ".avm")
            files.push_back(name);
    }
    closedir(d);
    return files;
}
static string nextSaveName() {
    vector<string> existing = listSaves();
    for(int i = 1; i < 1000; ++i) {
        char buf[32]; snprintf(buf, sizeof(buf), "save_%03d.avm", i);
        bool found = false;
        for(const auto& f : existing) if(f == buf) { found = true; break; }
        if(!found) return g_maps_dir + buf;
    }
    return g_maps_dir + "save_overflow.avm";
}

//              меню
enum MenuMode { MENU_MAIN, MENU_LOAD };
static bool menu_open = false;
static MenuMode menu_mode = MENU_MAIN;
static int menu_sel = 0;
static string menu_msg = "";
static vector<string> save_files;
static const char* main_items[] = { "save map", "new map", "load map", "continue" };
static const int MAIN_COUNT = 4;

static void openLoadMenu() {
    save_files = listSaves();
    menu_mode = MENU_LOAD;
    menu_sel = 0;
    menu_msg = save_files.empty() ? "no saves found" : "";
}
static void mainMenuAction(int idx) {
    if(idx == 0) {
        string path = nextSaveName();
        GameMap tmp; tmp.name = to_string(g_seed);
        if(save_map(tmp, path.c_str())) menu_msg = "saved: " + path;
        else                             menu_msg = "save failed";
    } else if(idx == 1) {
        int seed = (int)((long long)time(nullptr) * 1000 + clock()) & 0x7fffffff;
        cam_x = 0; cam_y = 0; cam_z = 0; cam_pitch = 0; cam_yaw = 0;
        initWorld(seed);
        char buf[64]; snprintf(buf, sizeof(buf), "new world seed %d", seed);
        menu_msg = buf;
        menu_open = false;
    } else if(idx == 2) {
        openLoadMenu();
    } else if(idx == 3) {
        menu_open = false; menu_msg = "";
    }
}
static void loadMenuAction(int idx) {
    if(save_files.empty()) return;
    string path = g_maps_dir + save_files[idx];
    GameMap loaded;
    if(load_map(path.c_str(), loaded)) {
        int seed = atoi(loaded.name.c_str());
        cam_x = 0; cam_y = 0; cam_z = 0; cam_pitch = 0; cam_yaw = 0;
        initWorld(seed);
        menu_msg = "loaded: " + save_files[idx];
        menu_open = false;
    } else {
        menu_msg = "load failed: " + save_files[idx];
    }
}

static void drawTextLine(const char* t, float x, float y, float r, float g, float b) {
    draw_text(t, x, y, GLUT_BITMAP_HELVETICA_18, r, g, b, 1.0f);
}
static void drawMenu() {
    begin_2d(window_w, window_h);
    glDisable(GL_TEXTURE_2D);
    glColor4f(0, 0, 0, 0.65f);
    glBegin(GL_QUADS);
    glVertex2f(0,0); glVertex2f(window_w,0);
    glVertex2f(window_w,window_h); glVertex2f(0,window_h);
    glEnd();
    const float cx = window_w * 0.38f;
    const float start_y = window_h * 0.62f;
    const float step = 32.0f;
    if(menu_mode == MENU_MAIN) {
        for(int i = 0; i < MAIN_COUNT; ++i) {
            char buf[128];
            if(i == menu_sel) snprintf(buf, sizeof(buf), "> %s", main_items[i]);
            else              snprintf(buf, sizeof(buf), "  %s", main_items[i]);
            if(i == menu_sel) drawTextLine(buf, cx, start_y - i * step, 1.0f, 0.85f, 0.1f);
            else              drawTextLine(buf, cx, start_y - i * step, 0.55f, 0.55f, 0.55f);
        }
    } else {
        drawTextLine("-- load map --", cx, start_y + step, 0.7f, 0.7f, 1.0f);
        if(save_files.empty()) {
            drawTextLine("  (no saves)", cx, start_y, 0.4f, 0.4f, 0.4f);
        } else {
            for(int i = 0; i < (int)save_files.size(); ++i) {
                char buf[256];
                if(i == menu_sel) snprintf(buf, sizeof(buf), "> %s", save_files[i].c_str());
                else              snprintf(buf, sizeof(buf), "  %s", save_files[i].c_str());
                if(i == menu_sel) drawTextLine(buf, cx, start_y - i * step, 1.0f, 0.85f, 0.1f);
                else              drawTextLine(buf, cx, start_y - i * step, 0.55f, 0.55f, 0.55f);
            }
        }
        draw_text("  esc: back", cx, start_y - save_files.size() * step - step,
            GLUT_BITMAP_HELVETICA_12, 0.4f, 0.4f, 0.4f, 1.0f);
    }
    if(!menu_msg.empty())
        draw_text(menu_msg.c_str(), cx, 45.0f, GLUT_BITMAP_HELVETICA_12, 0.3f, 1.0f, 0.4f, 1.0f);
    draw_text("up/down: navigate   enter: select   esc: back/continue",
        cx, 20.0f, GLUT_BITMAP_HELVETICA_12, 0.38f, 0.38f, 0.38f, 1.0f);
    end_2d();
}

//              колбэки glut
static void onDisplay() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setup_camera(90.0f, cam_x, cam_y, cam_z, cam_pitch, cam_yaw);
    draw_map(g_map, cam_x, cam_y, cam_z);
    draw_performance_hud(window_w, window_h);
    if(menu_open) drawMenu();
    glutSwapBuffers();
}
static void onReshape(int w, int h) { window_w = w; window_h = h; changeSize3D(w, h); }
static void onKeyDown(unsigned char k, int, int) {
    if(menu_open) {
        if(k == 13) {
            if(menu_mode == MENU_MAIN) mainMenuAction(menu_sel);
            else if(!save_files.empty()) loadMenuAction(menu_sel);
        } else if(k == 27) {
            if(menu_mode == MENU_LOAD) { menu_mode = MENU_MAIN; menu_sel = 0; menu_msg = ""; }
            else { menu_open = false; menu_msg = ""; }
        }
        return;
    }
    keys[k] = true;
    if(k == 27) { menu_open = true; menu_mode = MENU_MAIN; menu_sel = 0; }
}
static void onKeyUp(unsigned char k, int, int) { keys[k] = false; }
static void onSpecialDown(int k, int, int) {
    if(menu_open) {
        int count = (menu_mode == MENU_MAIN) ? MAIN_COUNT : (int)save_files.size();
        if(count == 0) return;
        if(k == GLUT_KEY_UP)   menu_sel = (menu_sel - 1 + count) % count;
        if(k == GLUT_KEY_DOWN) menu_sel = (menu_sel + 1) % count;
        glutPostRedisplay();
        return;
    }
    skeys[k] = true;
}
static void onSpecialUp(int k, int, int) { skeys[k] = false; }

static void onIdle() {
    static float pt = 0;
    const float t = glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    const float dt = fminf(t - pt, 0.05f); pt = t;
    if(!menu_open) {
        if(keys['q'] || keys['Q'] || skeys[GLUT_KEY_LEFT])  cam_yaw += TURN_SPEED * dt;
        if(keys['e'] || keys['E'] || skeys[GLUT_KEY_RIGHT]) cam_yaw -= TURN_SPEED * dt;
        if(skeys[GLUT_KEY_UP])   cam_pitch += TURN_SPEED * dt;
        if(skeys[GLUT_KEY_DOWN]) cam_pitch -= TURN_SPEED * dt;
        if(cam_pitch >  89) cam_pitch =  89;
        if(cam_pitch < -89) cam_pitch = -89;
        const float yr = cam_yaw   * float(M_PI) / 180.0f;
        const float pr = cam_pitch * float(M_PI) / 180.0f;
        const float mv = MOVE_SPEED * dt;
        const float fx =  cosf(pr) * sinf(yr);
        const float fy =  sinf(pr);
        const float fz =  cosf(pr) * cosf(yr);
        const float rx =  cosf(yr);
        const float rz = -sinf(yr);
        if(keys['w'] || keys['W']) { cam_x += fx*mv; cam_y += fy*mv; cam_z += fz*mv; }
        if(keys['s'] || keys['S']) { cam_x -= fx*mv; cam_y -= fy*mv; cam_z -= fz*mv; }
        if(keys['a'] || keys['A']) { cam_x += rx*mv; cam_z += rz*mv; }
        if(keys['d'] || keys['D']) { cam_x -= rx*mv; cam_z -= rz*mv; }
    }
    glutPostRedisplay();
}

//              точка входа
int main(int argc, char** argv) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", argv[0]);
    g_maps_dir = string(dirname(tmp)) + "/maps/";
    mkdir(g_maps_dir.c_str(), 0755);

    setup_display(&argc, argv, 0.02f, 0.02f, 0.05f, 1.0f, "avomap demo", 1280, 720);

    g_cubeVerts = makeCubeVerts();
    g_cubeIdx = makeCubeIdx();

    int seed = (int)((long long)time(nullptr) * 1000 + clock()) & 0x7fffffff;
    initWorld(seed);

    glutDisplayFunc(onDisplay);
    glutReshapeFunc(onReshape);
    glutKeyboardFunc(onKeyDown);
    glutKeyboardUpFunc(onKeyUp);
    glutSpecialFunc(onSpecialDown);
    glutSpecialUpFunc(onSpecialUp);
    glutIdleFunc(onIdle);
    glutMainLoop();
    return 0;
}