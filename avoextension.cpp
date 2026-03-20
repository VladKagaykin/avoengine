#include "avoextension.h"
#include "miniaudio.h"
// движок
#include "avoengine.h"
//              утилиты
// нелоховской вектор
#include <vector>
// строки полукрутые
#include <string>

using namespace std;

int tick=0;
const int max_tick=64;

//              утилиты
// система тиков
void timer(){
    glutTimerFunc(16,[](int){timer();},0);
    tick++;
    if(tick>max_tick)tick=0;
}
void init_tick_system(){
    timer();
}
//          простые 3д примитивы
// плоскость
void plane(float cx,float cy,float cz,double r,double g,double b,const char* tex,const vector<float>& vertices){
    if(vertices.size()<12)return;
    vector<int> indices={
        0,1,2,
        0,2,3};
    vector<float> texcoords={
        0.0f,0.0f,
        1.0f,0.0f,
        1.0f,1.0f,
        0.0f,1.0f};
    draw3DObject(cx,cy,cz,r,g,b,tex,vertices,indices,texcoords);
}
//              hud

//              звук
// audio_engine уже есть в avoengine.cpp
extern ma_engine audio_engine;
// белый шум
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