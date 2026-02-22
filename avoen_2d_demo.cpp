#include "avoengine.h"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>
#include <deque>
#include <vector>

using namespace std;

// ---------- Константы (использую ваши значения) ----------
const float GROUND_Y = -0.6f;                // уровень поверхности земли
const float GROUND_STRIP_HEIGHT = 0.5f;       // высота полосы земли (можете изменить)
const float DINO_SIZE = 0.15f;
const float GRAVITY = 0.02f;
const float JUMP_FORCE = 0.25f;

// Звёзды (медленный фон)
const int STAR_COUNT = 10;
const float STAR_SPEED = 0.005f;              // медленно

// ---------- Игровые переменные ----------
float dino_center_x, dino_center_y;
float dino_velocity = 0;
bool is_jumping = false;
bool game_over = false;
int score = 0;
int high_score = 0;

// Препятствия
struct Obstacle {
    float x, y, height;
};
vector<Obstacle> obstacles;
float obstacle_speed = 0.03f;
int obstacle_timer = 0;
const int OBSTACLE_SPAWN_RATE = 60;

// Звёзды
struct Star { float x, y; };
vector<Star> stars;

// ---------- Геометрия ----------
float square_vertices[] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
     0.5f,  0.5f,
    -0.5f,  0.5f
};

// Текстуры (замените на свои пути)
vector<const char*> dino_textures = {"src/penza.png", "src/penza_low.png"};
const char* cactus_texture = "src/prime.png";
const char* ground_texture = "src/god_png.png";

// ---------- Бесконечная земля (полосы) ----------
struct GroundStrip { float x; };
deque<GroundStrip> ground_strips;
const float GROUND_STRIP_WIDTH = 0.8f;
const int GROUND_STRIP_COUNT = 6;

float ground_vertices[] = {
    -GROUND_STRIP_WIDTH/2, -GROUND_STRIP_HEIGHT/2,
     GROUND_STRIP_WIDTH/2, -GROUND_STRIP_HEIGHT/2,
     GROUND_STRIP_WIDTH/2,  GROUND_STRIP_HEIGHT/2,
    -GROUND_STRIP_WIDTH/2,  GROUND_STRIP_HEIGHT/2
};

void initGround() {
    ground_strips.clear();
    // Начинаем с -2.0, чтобы покрыть экран с запасом
    for (int i = 0; i < GROUND_STRIP_COUNT; ++i)
        ground_strips.push_back({-2.0f + i * GROUND_STRIP_WIDTH});
}

void updateGround() {
    // Движение влево с той же скоростью, что и препятствия
    for (auto &s : ground_strips) s.x -= obstacle_speed;

    // Если полоса ушла далеко влево (за экран), перемещаем её вправо
    while (ground_strips.front().x + GROUND_STRIP_WIDTH/2 < -2.0f) {
        GroundStrip moved = ground_strips.front();
        ground_strips.pop_front();
        moved.x = ground_strips.back().x + GROUND_STRIP_WIDTH; // ставим после последней
        ground_strips.push_back(moved);
    }
}

// ---------- Звёзды (фон) ----------
void initStars() {
    stars.clear();
    for (int i = 0; i < STAR_COUNT; ++i) {
        stars.push_back({
            (rand() % 2000) / 1000.0f - 1.0f,   // случайная x от -1 до 1
            (rand() % 2000) / 1000.0f - 0.5f    // y в верхней части
        });
    }
}

void updateStars() {
    for (auto &star : stars) {
        star.x -= STAR_SPEED;                   // медленно влево
        if (star.x < -2.0f) {                   // ушла за левый край
            // Появляется справа за экраном (от 1.0 до 1.5)
            star.x = 2.0f + (rand() % 50) / 100.0f;
            star.y = (rand() % 2000) / 1000.0f - 0.5f; // новая случайная y
        }
    }
}

// ---------- Инициализация игры ----------
void initGame() {
    srand(time(nullptr));
    obstacles.clear();
    initGround();
    initStars();

    // Центр динозавра для левого нижнего угла (левая граница в -1.0)
    dino_center_x = (-1.0f + DINO_SIZE/2) / DINO_SIZE;
    dino_center_y = (GROUND_Y + DINO_SIZE/2) / DINO_SIZE;
    dino_velocity = 0;
    is_jumping = false;
    game_over = false;
    score = 0;
    obstacle_speed = 0.03f;
    obstacle_timer = 0;
}

// Препятствие появляется справа за экраном (правый край > 1.0)
void spawnObstacle() {
    Obstacle obs;
    float right_edge = 1.2f;                    // за экраном (можно менять)
    obs.height = 0.12f + (rand() % 9) * 0.01f;   // от 0.12 до 0.20
    obs.x = (right_edge - obs.height/2) / obs.height;
    obs.y = (GROUND_Y + obs.height/2) / obs.height;
    obstacles.push_back(obs);
}

// Проверка столкновения (точные прямоугольники)
bool checkCollision(const Obstacle& obs) {
    float dl = dino_center_x * DINO_SIZE - DINO_SIZE/2;
    float dr = dino_center_x * DINO_SIZE + DINO_SIZE/2;
    float db = dino_center_y * DINO_SIZE - DINO_SIZE/2;
    float dt = dino_center_y * DINO_SIZE + DINO_SIZE/2;

    float ol = obs.x * obs.height - obs.height/2;
    float or_ = obs.x * obs.height + obs.height/2;
    float ob = obs.y * obs.height - obs.height/2;
    float ot = obs.y * obs.height + obs.height/2;

    return (dl < or_ && dr > ol && db < ot && dt > ob);
}

// ---------- Обновление логики ----------
void updateGame() {
    if (game_over) return;

    score++;
    if (score > high_score) high_score = score;
    if (score % 500 == 0) obstacle_speed += 0.002f;

    // Физика прыжка
    if (is_jumping) {
        dino_velocity -= GRAVITY;
        dino_center_y += dino_velocity / DINO_SIZE;
        if (dino_center_y * DINO_SIZE <= GROUND_Y + DINO_SIZE/2) {
            dino_center_y = (GROUND_Y + DINO_SIZE/2) / DINO_SIZE;
            dino_velocity = 0;
            is_jumping = false;
        }
    }

    // Спавн препятствий
    if (++obstacle_timer >= OBSTACLE_SPAWN_RATE) {
        obstacle_timer = 0;
        if (rand() % 3 < 2) spawnObstacle();
    }

    // Движение препятствий
    for (auto &obs : obstacles) {
        obs.x -= obstacle_speed / obs.height;   // скорость в масштабированных координатах
    }

    // Движение земли и звёзд
    updateGround();
    updateStars();

    // Удаление препятствий, ушедших за левый край
    for (size_t i = 0; i < obstacles.size();) {
        if (obstacles[i].x * obstacles[i].height < -1.5f)
            obstacles.erase(obstacles.begin() + i);
        else ++i;
    }

    // Проверка коллизий
    for (const auto &obs : obstacles) {
        if (checkCollision(obs)) {
            game_over = true;
            break;
        }
    }
}

// ---------- Отрисовка ----------
void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Звёзды (медленный фон)
    for (auto &s : stars) {
    circle(1.0f, s.x, s.y, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 10, 1, "src/diskriminant.png");
}
    glEnd();

    // Земля
    for (auto &s : ground_strips) {
        square(1.0f, s.x, GROUND_Y - GROUND_STRIP_HEIGHT/2,
               1,1,1,0, ground_vertices, ground_texture, {}, false);
    }

    // Счёт
    glColor3f(1,1,1);
    glRasterPos2f(0.7f, 0.9f);
    for (char c : to_string(score)) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
    glRasterPos2f(0.7f, 0.8f);
    string hs = "HI " + to_string(high_score);
    for (char c : hs) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);

    // Препятствия
    for (auto &obs : obstacles) {
        square(obs.height, obs.x, obs.y, 1,1,1,0,
               square_vertices, cactus_texture, {}, false);
    }

    // Динозавр
    if (game_over) {
        square(DINO_SIZE, dino_center_x, dino_center_y, 1,0,0,0,
               square_vertices, nullptr, {}, false);
        glColor3f(1,0,0);
        glRasterPos2f(-0.2f, 0.0f);
        for (char c : "GAME OVER") glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
        glRasterPos2f(-0.3f, -0.2f);
        for (char c : "Press SPACE") glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
    } else {
        square(DINO_SIZE, dino_center_x, dino_center_y, 1,1,1,0,
               square_vertices, dino_textures[0], dino_textures, true);
    }

    glutSwapBuffers();
}

// ---------- Управление ----------
void handleKeyboard(unsigned char key, int, int) {
    if (key == ' ') {
        if (game_over) initGame();
        else if (!is_jumping) {
            is_jumping = true;
            dino_velocity = JUMP_FORCE;
        }
    }
}

void gameTimer(int) {
    updateGame();
    glutPostRedisplay();
    glutTimerFunc(16, gameTimer, 0);
    tick++;
}

// ---------- Главная ----------
int main(int argc, char** argv) {
    setup_display(&argc, argv, 0,0,0,1,
                  "Dinosaur Game - AVO Engine", 800, 400);
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(handleKeyboard);
    glutTimerFunc(0, gameTimer, 0);
    initGame();

    cout << "=== Dinosaur Game ===\n";
    cout << "GROUND_Y = " << GROUND_Y << ", GROUND_STRIP_HEIGHT = " << GROUND_STRIP_HEIGHT << "\n";
    cout << "Obstacles spawn with right edge at 1.2 (behind right side)\n";
    cout << "Stars slowly move left and respawn at x > 1.0\n";
    cout << "Press SPACE to jump.\n";

    glutMainLoop();
    return 0;
}