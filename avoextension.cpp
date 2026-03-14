#include "avoextension.h"
// miniaudio уже реализован в avoengine.cpp — только заголовок
#include "miniaudio.h"
// движок
#include "avoengine.h"
// audio_engine живёт в avoengine.cpp
extern ma_engine audio_engine;

//              белый шум
static ma_noise noise_src;
static ma_audio_buffer* noise_buf=nullptr;
static ma_sound noise_snd;
static bool noise_init=false;
static float noiseData[48000*2];
void play_white_noise_3d(float x,float y,float z,float volume){
    if (!noise_init){
        ma_noise_config cfg=ma_noise_config_init(ma_format_f32,2,ma_noise_type_white,0,0.3f);
        ma_noise_init(&cfg,nullptr,&noise_src);
        ma_noise_read_pcm_frames(&noise_src,noiseData,48000,nullptr);
        ma_audio_buffer_config bcfg=ma_audio_buffer_config_init(ma_format_f32,2,48000,noiseData,nullptr);
        ma_audio_buffer_alloc_and_init(&bcfg,&noise_buf);
        ma_sound_init_from_data_source(&audio_engine,noise_buf,0,nullptr,&noise_snd);
        noise_init=true;
    }
    ma_sound_set_positioning(&noise_snd,ma_positioning_absolute);
    ma_sound_set_position(&noise_snd,x,y,z);
    ma_sound_set_spatialization_enabled(&noise_snd,MA_TRUE);
    ma_sound_set_volume(&noise_snd,volume);
    ma_sound_set_looping(&noise_snd,MA_TRUE);
    ma_sound_start(&noise_snd);
}