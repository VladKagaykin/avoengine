#include "audio_not_mini.h"
#include "avoengine.h"
#include <iostream>
#include <vector>

using namespace std;

// инициализация звукового движка(ma_engine тип данных, а audio_engine название)
ma_engine audio_engine;
// вектор в котором хранятся звуки, которые играют на постоянке
std::vector<ma_sound*> loopingSounds;

//              звук
// указываем что здесь реализация библиотеки, т.к. miniaudio это только заголовочный файл и даём понять что
// это главная программа
#define MINIAUDIO_IMPLEMENTATION
#include "src/miniaudio.h"

//              аудио
// инициализация аудио
void init_audio(){
    if(ma_engine_init(nullptr,&audio_engine)!=MA_SUCCESS){
        cerr<<"Failed to init audio engine\n";
        return;
    }
    cout<<"Audio: "<<ma_engine_get_channels(&audio_engine)<<" ch, "<<ma_engine_get_sample_rate(&audio_engine)<<" Hz"<<endl;
}
// проигрывание звука(просто)
void play_sound(const char* filename,float volume){
    // проверка на существование звука
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,MA_SOUND_FLAG_ASYNC,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // указываем что звук вне пространства
    ma_sound_set_spatialization_enabled(sound, MA_FALSE);
    // звук
    ma_sound_set_volume(sound, volume);
    
    // проигрывание
    ma_sound_start(sound);
}
void play_sound_loop(const char* filename,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    // отключаем пространственную обработку
    ma_sound_set_spatialization_enabled(sound,MA_FALSE);
    // устанавливаем громкость
    ma_sound_set_volume(sound,volume);
    // включаем зацикливание
    ma_sound_set_looping(sound,MA_TRUE);
    // проигрываем звук
    ma_sound_start(sound);
    // добавляем в вектор для последующей очистки
    loopingSounds.push_back(sound);
}
// проигрываем звук в 3д(сложно)
void play_sound_3d(const char* filename,float x,float y,float z,float volume){
    // проверка
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // задаём где слушатель и звук и другие параметры
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    
    // проигрываем звук
    ma_sound_start(sound);
}
// проигрываем звук в 3д бесконечно(всё то же самое, только бесконечно)
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    ma_sound_set_looping(sound,MA_TRUE);
    ma_sound_start(sound);
    loopingSounds.push_back(sound);
}
// останавливаем все бесконечные звуки(тут из названий всё понятно)
void stop_all_looping_sounds(){
    for(auto* s:loopingSounds){
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    loopingSounds.clear();
}