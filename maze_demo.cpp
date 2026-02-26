#include "avoengine.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <stack>
#include <vector>
#include <map>
#include <utility>

// ─── Параметры ───────────────────────────────────────────────────────────────
static const int   CW      = 21;     // размер чанка в клетках (нечётное)
static const float CELL    = 2.0f;
static const float WALL_H  = 2.5f;
static const int   RENDER_R = 2;     // радиус отрисовки чанков вокруг игрока

static const float MOVE_SPEED = 0.07f;
static const float TURN_SPEED = 2.5f;

// ─── Игрок ───────────────────────────────────────────────────────────────────
static float px    = 1.5f * CELL;
static float pz    = 1.5f * CELL;
static float angle = 0.0f;
static bool  keys[256] = {};

// ─── Чанк ────────────────────────────────────────────────────────────────────
struct Chunk {
    int grid[CW][CW]; // 0=проход 1=стена
};

// Детерминированный генератор по (cx,cz)
static Chunk generateChunk(int cx, int cz) {
    Chunk ch;
    // заполняем стенами
    for (int r = 0; r < CW; ++r)
        for (int c = 0; c < CW; ++c)
            ch.grid[r][c] = 1;

    // простой LCG seed по координатам чанка
    unsigned seed = (unsigned)(cx * 73856093 ^ cz * 19349663 ^ 0xDEADBEEF);
    auto rng = [&]() -> int {
        seed = seed * 1664525u + 1013904223u;
        return (int)(seed >> 16);
    };

    // recursive backtracker
    std::vector<std::vector<bool>> visited(CW, std::vector<bool>(CW, false));
    struct Cell { int r, c; };
    std::stack<Cell> stk;

    ch.grid[1][1] = 0;
    visited[1][1] = true;
    stk.push({1, 1});

    int dr[] = {-2, 2,  0, 0};
    int dc[] = { 0, 0, -2, 2};

    while (!stk.empty()) {
        Cell cur = stk.top();
        std::vector<int> nbrs;
        for (int d = 0; d < 4; ++d) {
            int nr = cur.r + dr[d], nc = cur.c + dc[d];
            if (nr > 0 && nr < CW-1 && nc > 0 && nc < CW-1 && !visited[nr][nc])
                nbrs.push_back(d);
        }
        if (nbrs.empty()) { stk.pop(); continue; }
        int d  = nbrs[rng() % (int)nbrs.size()];
        int nr = cur.r + dr[d], nc = cur.c + dc[d];
        ch.grid[cur.r + dr[d]/2][cur.c + dc[d]/2] = 0;
        ch.grid[nr][nc] = 0;
        visited[nr][nc] = true;
        stk.push({nr, nc});
    }

    // Открываем проходы на границах с каждым соседним чанком
    // Используем seed соседа для выбора той же точки прохода (симметрично)
    // Проход: фиксированная нечётная строка/столбец посередине
    int mid = (CW / 2) % 2 == 0 ? CW/2 - 1 : CW/2; // нечётное число
    if (mid % 2 == 0) mid--;

    // Север (r=0): пробиваем в ch.grid[0][mid] и ch.grid[1][mid]
    ch.grid[0][mid]   = 0;
    ch.grid[1][mid]   = 0;
    // Юг (r=CW-1)
    ch.grid[CW-1][mid] = 0;
    ch.grid[CW-2][mid] = 0;
    // Запад (c=0)
    ch.grid[mid][0]   = 0;
    ch.grid[mid][1]   = 0;
    // Восток (c=CW-1)
    ch.grid[mid][CW-1] = 0;
    ch.grid[mid][CW-2] = 0;

    return ch;
}

static std::map<std::pair<int,int>, Chunk> chunkCache;

static const Chunk& getChunk(int cx, int cz) {
    auto key = std::make_pair(cx, cz);
    auto it = chunkCache.find(key);
    if (it != chunkCache.end()) return it->second;
    chunkCache[key] = generateChunk(cx, cz);
    return chunkCache[key];
}

// ─── Координаты чанка по мировым координатам ─────────────────────────────────
static int chunkX(float wx) { return (int)floorf(wx / (CW * CELL)); }
static int chunkZ(float wz) { return (int)floorf(wz / (CW * CELL)); }

// Локальная клетка внутри чанка
static int localCol(float wx) {
    int cx = chunkX(wx);
    float lx = wx - cx * CW * CELL;
    return (int)(lx / CELL);
}
static int localRow(float wz) {
    int cz = chunkZ(wz);
    float lz = wz - cz * CW * CELL;
    return (int)(lz / CELL);
}

// ─── Коллизия ────────────────────────────────────────────────────────────────
static bool inWall(float wx, float wz, float margin = 0.25f) {
    for (int dxi = -1; dxi <= 1; dxi += 2)
    for (int dzi = -1; dzi <= 1; dzi += 2) {
        float tx = wx + dxi * margin;
        float tz = wz + dzi * margin;
        int cx = chunkX(tx), cz = chunkZ(tz);
        float lx = tx - cx * CW * CELL;
        float lz = tz - cz * CW * CELL;
        int col = (int)(lx / CELL);
        int row = (int)(lz / CELL);
        if (col < 0 || col >= CW || row < 0 || row >= CW) return true;
        const Chunk& ch = getChunk(cx, cz);
        if (ch.grid[row][col] == 1) return true;
    }
    return false;
}

// ─── Отрисовка ───────────────────────────────────────────────────────────────
static void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    float aspect = (float)vp[2] / (float)vp[3];
    float rad = angle * M_PI / 180.0f;
    float eye_y = WALL_H * 0.7f;
    setup_camera(
        58.0f, aspect, 0.05f, 300.0f,
        px, eye_y, pz,
        px + sinf(rad), eye_y, pz + cosf(rad),
        0, 1, 0
    );

    int pcx = chunkX(px), pcz = chunkZ(pz);

    for (int dcx = -RENDER_R; dcx <= RENDER_R; ++dcx) {
        for (int dcz = -RENDER_R; dcz <= RENDER_R; ++dcz) {
            int cx = pcx + dcx;
            int cz = pcz + dcz;
            const Chunk& ch = getChunk(cx, cz);

            float base_x = cx * CW * CELL;
            float base_z = cz * CW * CELL;

            for (int r = 0; r < CW; ++r) {
                for (int c = 0; c < CW; ++c) {
                    float wx = base_x + (c + 0.5f) * CELL;
                    float wz = base_z + (r + 0.5f) * CELL;

                    // Пол — шахматный узор
                    if ((r + c) % 2 == 0)
                        cube3D(CELL * 0.5f, wx, -0.05f, wz, 0.38f, 0.33f, 0.27f, 0,0,0, nullptr);
                    else
                        cube3D(CELL * 0.5f, wx, -0.05f, wz, 0.28f, 0.24f, 0.19f, 0,0,0, nullptr);

                    // Стены — чередуются по (r+c)%2
                    if (ch.grid[r][c] == 1) {
                        float wy = WALL_H * 0.5f;
                        if ((r + c) % 2 == 0)
                            cube3D(CELL * 0.5f, wx, wy, wz, 0.50f, 0.45f, 0.65f, 0,0,0, nullptr);
                        else
                            cube3D(CELL * 0.5f, wx, wy, wz, 0.25f, 0.22f, 0.40f, 0,0,0, nullptr);
                    }
                }
            }
        }
    }
    // После всей 3D отрисовки, перед circle:

    int w = vp[2], h = vp[3];
    float ratio = (float)w / h;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-ratio, ratio, -1, 1, -1, 1);  // та же логика что в changeSize для 2D

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);  // чтобы HUD рисовался поверх всего

    // circle(1, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0, 7, 1, "src/diskriminant.png");

    glEnable(GL_DEPTH_TEST);  // восстанавливаем для следующего кадра

    // camera.initialized = 0 убрать — он не нужен
    glutSwapBuffers();
}

// ─── Ввод ────────────────────────────────────────────────────────────────────
static void keyDown(unsigned char key, int, int) {
    keys[key] = true;
    if (key == 27) exit(0);
}
static void keyUp(unsigned char key, int, int) {
    keys[key] = false;
}

// ─── Обновление ──────────────────────────────────────────────────────────────
static void update(int) {
    if (keys['a'] || keys['A']) angle += TURN_SPEED;
    if (keys['d'] || keys['D']) angle -= TURN_SPEED;

    float rad = angle * M_PI / 180.0f;
    float dx = sinf(rad) * MOVE_SPEED;
    float dz = cosf(rad) * MOVE_SPEED;

    if (keys['w'] || keys['W']) {
        if (!inWall(px + dx, pz)) px += dx;
        if (!inWall(px, pz + dz)) pz += dz;
    }
    if (keys['s'] || keys['S']) {
        if (!inWall(px - dx, pz)) px -= dx;
        if (!inWall(px, pz - dz)) pz -= dz;
    }

    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    setup_display(&argc, argv, 0.05f, 0.05f, 0.08f, 1.0f,
                  "AvoEngine 3D Maze", 1024, 600);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutTimerFunc(16, update, 0);

    glutMainLoop();
    return 0;
}