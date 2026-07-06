#ifndef TICK_SYSTEM
#define TICK_SYSTEM

extern int tick;
extern const int max_tick;
extern int absolute_tick;

void init_tick_system();
void update_ticks();

#endif