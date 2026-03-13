#include "avoextension.h"

// miniaudio уже реализован в avoengine.cpp — только заголовок
#include "miniaudio.h"

// движок (begin_2d, end_2d, draw_text)
#include "avoengine.h"

// утилиты
#include <GL/glut.h>
#include <chrono>
#include <cstdio>
#include <unistd.h>  // sysconf

// переменные для окна и камеры объявлены в avoengine.cpp и экспортируются оттуда
extern int window_w, window_h;

// определение структуры должно совпадать с тем, что в avoengine.cpp
struct CameraParams {
    float fov   = 60.0f;
    float znear = 0.1f;
    float zfar  = 1000.0f;
    float eye_x = 0, eye_y = 0, eye_z = 0;
    float ctr_x = 0, ctr_y = 0, ctr_z = 1;
    float up_x  = 0, up_y  = 1, up_z  = 0;
};
extern CameraParams camera;

// audio_engine живёт в avoengine.cpp
extern ma_engine audio_engine;

//              белый шум
static ma_noise        noise_src;
static ma_audio_buffer* noise_buf  = nullptr;
static ma_sound        noise_snd;
static bool            noise_init  = false;
static float           noiseData[48000 * 2];

void play_white_noise_3d(float x, float y, float z, float volume) {
    if (!noise_init) {
        ma_noise_config cfg = ma_noise_config_init(
            ma_format_f32, 2, ma_noise_type_white, 0, 0.3f);
        ma_noise_init(&cfg, nullptr, &noise_src);
        ma_noise_read_pcm_frames(&noise_src, noiseData, 48000, nullptr);

        ma_audio_buffer_config bcfg = ma_audio_buffer_config_init(
            ma_format_f32, 2, 48000, noiseData, nullptr);
        ma_audio_buffer_alloc_and_init(&bcfg, &noise_buf);
        ma_sound_init_from_data_source(&audio_engine, noise_buf, 0, nullptr, &noise_snd);
        noise_init = true;
    }
    ma_sound_set_positioning(&noise_snd, ma_positioning_absolute);
    ma_sound_set_position(&noise_snd, x, y, z);
    ma_sound_set_spatialization_enabled(&noise_snd, MA_TRUE);
    ma_sound_set_volume(&noise_snd, volume);
    ma_sound_set_looping(&noise_snd, MA_TRUE);
    ma_sound_start(&noise_snd);
}

void draw_performance_hud(int win_w, int win_h) {
    static long   prev_cpu  = 0;
    static double cpu_pct   = 0.0;
    static long   ram_kb    = 0;
    static int    frame_cnt = 0;
    static double fps       = 0.0;
    static auto   prev_time = std::chrono::steady_clock::now();

    ++frame_cnt;
    auto   now     = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - prev_time).count();

    // раз в секунду пересчитываем статистику
    if (elapsed >= 1.0) {
        fps       = frame_cnt / elapsed;
        frame_cnt = 0;

        // RAM из /proc/self/status
        if (FILE* f = fopen("/proc/self/status", "r")) {
            char line[128];
            while (fgets(line, sizeof(line), f))
                if (sscanf(line, "VmRSS: %ld", &ram_kb) == 1) break;
            fclose(f);
        }

        // CPU-время из /proc/self/stat
        long utime = 0, stime = 0;
        if (FILE* s = fopen("/proc/self/stat", "r")) {
            fscanf(s, "%*d %*s %*c %*d %*d %*d %*d %*d "
                      "%*u %*u %*u %*u %*u %ld %ld",
                   &utime, &stime);
            fclose(s);
        }
        long cur_cpu = utime + stime;
        cpu_pct      = (cur_cpu - prev_cpu)
                     / (double)sysconf(_SC_CLK_TCK)
                     / elapsed * 10.0;
        prev_cpu  = cur_cpu;
        prev_time = now;
    }

    char buf[80];

    begin_2d(win_w, win_h);

    snprintf(buf, sizeof(buf),
             "FPS: %.0f  RAM: %ld MB  CPU: %.1f%%",
             fps, ram_kb / 1024, cpu_pct);
    draw_text(buf, 10.0f, float(win_h) - 20.0f,
              GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);

    snprintf(buf, sizeof(buf),
             "X: %.10f  Y: %.10f  Z: %.10f",
             camera.eye_x, camera.eye_y, camera.eye_z);
    draw_text(buf, 10.0f, float(win_h) - 32.0f,
              GLUT_BITMAP_HELVETICA_12, 1.0f, 1.0f, 1.0f);

    end_2d();
}