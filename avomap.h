#ifndef AVOMAP_H
#define AVOMAP_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

//              статичные объекты карты
struct MapObject3D {
    float cx, cy, cz;
    double r, g, b;
    std::string texture;
    std::vector<float> vertices;
    std::vector<int> indices;
    std::vector<float> texcoords;
};

//              псевдо-3д сущность (спрайт всегда смотрит на камеру, как в думе)
struct MapEntityData {
    float x, y, z;
    float g_angle, v_angle, radius;
    int v_angles;
    std::vector<std::string> textures;
    std::vector<float> vertices;
};

//              звук на карте
struct MapSound {
    std::string filename;
    float x, y, z, volume;
    bool looping, spatial;
};

//              процедурный слой — бесконечное пространство
// функция вызывается каждый кадр, принимает позицию камеры
// рисует то что надо вокруг — пол, сферы, деревья, что угодно
// не сохраняется в файл — регистрируется прямо в коде
using ProceduralLayer = std::function<void(float cam_x, float cam_y, float cam_z)>;

//              вся карта
struct GameMap {
    std::string name;
    std::vector<MapObject3D> objects;
    std::vector<MapEntityData> entities;
    std::vector<MapSound> sounds;
    // не сохраняются в файл
    std::vector<ProceduralLayer> layers;
};

// сохраняет карту в бинарный файл .avm
// возвращает true если успешно
bool save_map(const GameMap& map, const char* filename);

// загружает карту из бинарного файла .avm
// возвращает true если успешно
bool load_map(const char* filename, GameMap& out_map);

// рисует все 3д объекты карты + вызывает все процедурные слои
// вызывать каждый кадр в displayWrapper
void draw_map(const GameMap& map, float cam_x, float cam_y, float cam_z);

// запускает все looping-звуки карты
// вызывать один раз после load_map
void play_map_sounds(const GameMap& map);

#endif