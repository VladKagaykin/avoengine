#pragma once

// проигрывает белый шум в 3д пространстве (зациклено)
void play_white_noise_3d(float x, float y, float z, float volume);

// рисует оверлей с FPS, RAM и CPU в левом верхнем углу окна
void draw_performance_hud(int win_w, int win_h);