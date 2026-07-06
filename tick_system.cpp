#include "tick_system.h"
#include "avoengine.h"
#include <chrono>

// система тиков
int tick=0;
const int max_tick=20;
int absolute_tick = 0;
static std::chrono::steady_clock::time_point last_tick_time;
static std::chrono::microseconds tick_interval(Engine_settings.TICK_SPEED);

void init_tick_system() {
    last_tick_time = std::chrono::steady_clock::now();
    tick_interval = (std::chrono::microseconds)Engine_settings.TICK_SPEED;
}

void update_ticks() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick_time);

    int ticks_to_add = static_cast<int>(elapsed.count() / tick_interval.count());
    if (ticks_to_add > 0) {
        absolute_tick += ticks_to_add;
        tick = (tick + ticks_to_add) % (max_tick + 1);
        last_tick_time += ticks_to_add * tick_interval;
    }
}