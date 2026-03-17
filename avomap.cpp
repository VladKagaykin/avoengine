#include "avomap.h"
#include "avoengine.h"
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace std;

// формат файла .avm
//
// магическое число:   4 байта  "AVOM"
// версия:             4 байта  uint32  (сейчас 1)
// длина имени:        4 байта  uint32
// имя карты:          N байт   char[]
//
// кол-во объектов:    4 байта  uint32
// [для каждого объекта]
//   cx,cy,cz:         float[3]
//   r,g,b:            double[3]
//   длина текстуры:   uint32
//   текстура:         N байт
//   кол-во вершин:    uint32 → float[]
//   кол-во индексов:  uint32 → int[]
//   кол-во texcoords: uint32 → float[]
//
// кол-во сущностей:   4 байта  uint32
// [для каждой сущности]
//   x,y,z:            float[3]
//   g_angle,v_angle:  float[2]
//   radius:           float
//   v_angles:         int
//   кол-во вершин:    uint32 → float[]
//   кол-во текстур:   uint32
//   [для каждой текстуры]
//     длина пути:     uint32
//     путь:           N байт
//
// кол-во звуков:      4 байта  uint32
// [для каждого звука]
//   длина пути:       uint32
//   путь:             N байт
//   x,y,z:            float[3]
//   volume:           float
//   looping:          uint8
//   spatial:          uint8

//              утилиты для чтения/записи
static bool write_string(FILE* f, const string& s) {
    uint32_t len = (uint32_t)s.size();
    if(fwrite(&len, 4, 1, f) != 1) return false;
    if(len > 0 && fwrite(s.data(), 1, len, f) != len) return false;
    return true;
}
static bool read_string(FILE* f, string& out) {
    uint32_t len = 0;
    if(fread(&len, 4, 1, f) != 1) return false;
    out.resize(len);
    if(len > 0 && fread(&out[0], 1, len, f) != len) return false;
    return true;
}
static bool write_floats(FILE* f, const vector<float>& v) {
    uint32_t n = (uint32_t)v.size();
    if(fwrite(&n, 4, 1, f) != 1) return false;
    if(n > 0 && fwrite(v.data(), sizeof(float), n, f) != n) return false;
    return true;
}
static bool read_floats(FILE* f, vector<float>& out) {
    uint32_t n = 0;
    if(fread(&n, 4, 1, f) != 1) return false;
    out.resize(n);
    if(n > 0 && fread(out.data(), sizeof(float), n, f) != n) return false;
    return true;
}
static bool write_ints(FILE* f, const vector<int>& v) {
    uint32_t n = (uint32_t)v.size();
    if(fwrite(&n, 4, 1, f) != 1) return false;
    if(n > 0 && fwrite(v.data(), sizeof(int), n, f) != n) return false;
    return true;
}
static bool read_ints(FILE* f, vector<int>& out) {
    uint32_t n = 0;
    if(fread(&n, 4, 1, f) != 1) return false;
    out.resize(n);
    if(n > 0 && fread(out.data(), sizeof(int), n, f) != n) return false;
    return true;
}

//              сохранение
bool save_map(const GameMap& map, const char* filename) {
    FILE* f = fopen(filename, "wb");
    if(!f) { cerr << "[avomap] не удалось открыть файл для записи: " << filename << endl; return false; }

    const char magic[4] = {'A','V','O','M'};
    fwrite(magic, 1, 4, f);
    uint32_t version = 1;
    fwrite(&version, 4, 1, f);

    write_string(f, map.name);

    uint32_t obj_count = (uint32_t)map.objects.size();
    fwrite(&obj_count, 4, 1, f);
    for(const auto& obj : map.objects) {
        fwrite(&obj.cx, sizeof(float), 1, f);
        fwrite(&obj.cy, sizeof(float), 1, f);
        fwrite(&obj.cz, sizeof(float), 1, f);
        fwrite(&obj.r, sizeof(double), 1, f);
        fwrite(&obj.g, sizeof(double), 1, f);
        fwrite(&obj.b, sizeof(double), 1, f);
        write_string(f, obj.texture);
        write_floats(f, obj.vertices);
        write_ints(f, obj.indices);
        write_floats(f, obj.texcoords);
    }

    uint32_t ent_count = (uint32_t)map.entities.size();
    fwrite(&ent_count, 4, 1, f);
    for(const auto& e : map.entities) {
        fwrite(&e.x, sizeof(float), 1, f);
        fwrite(&e.y, sizeof(float), 1, f);
        fwrite(&e.z, sizeof(float), 1, f);
        fwrite(&e.g_angle, sizeof(float), 1, f);
        fwrite(&e.v_angle, sizeof(float), 1, f);
        fwrite(&e.radius, sizeof(float), 1, f);
        fwrite(&e.v_angles, sizeof(int), 1, f);
        write_floats(f, e.vertices);
        uint32_t tex_count = (uint32_t)e.textures.size();
        fwrite(&tex_count, 4, 1, f);
        for(const auto& t : e.textures) write_string(f, t);
    }

    uint32_t snd_count = (uint32_t)map.sounds.size();
    fwrite(&snd_count, 4, 1, f);
    for(const auto& snd : map.sounds) {
        write_string(f, snd.filename);
        fwrite(&snd.x, sizeof(float), 1, f);
        fwrite(&snd.y, sizeof(float), 1, f);
        fwrite(&snd.z, sizeof(float), 1, f);
        fwrite(&snd.volume, sizeof(float), 1, f);
        uint8_t loop = snd.looping ? 1 : 0, spat = snd.spatial ? 1 : 0;
        fwrite(&loop, 1, 1, f);
        fwrite(&spat, 1, 1, f);
    }

    fclose(f);
    cout << "[avomap] карта \"" << map.name << "\" сохранена: " << filename << endl;
    return true;
}

//              загрузка
bool load_map(const char* filename, GameMap& out_map) {
    FILE* f = fopen(filename, "rb");
    if(!f) { cerr << "[avomap] файл не найден: " << filename << endl; return false; }

    char magic[4] = {};
    fread(magic, 1, 4, f);
    if(magic[0] != 'A' || magic[1] != 'V' || magic[2] != 'O' || magic[3] != 'M') {
        cerr << "[avomap] неверный формат файла: " << filename << endl;
        fclose(f); return false;
    }

    uint32_t version = 0;
    fread(&version, 4, 1, f);
    if(version != 1) {
        cerr << "[avomap] неподдерживаемая версия: " << version << endl;
        fclose(f); return false;
    }

    read_string(f, out_map.name);

    uint32_t obj_count = 0;
    fread(&obj_count, 4, 1, f);
    out_map.objects.resize(obj_count);
    for(auto& obj : out_map.objects) {
        fread(&obj.cx, sizeof(float), 1, f);
        fread(&obj.cy, sizeof(float), 1, f);
        fread(&obj.cz, sizeof(float), 1, f);
        fread(&obj.r, sizeof(double), 1, f);
        fread(&obj.g, sizeof(double), 1, f);
        fread(&obj.b, sizeof(double), 1, f);
        read_string(f, obj.texture);
        read_floats(f, obj.vertices);
        read_ints(f, obj.indices);
        read_floats(f, obj.texcoords);
    }

    uint32_t ent_count = 0;
    fread(&ent_count, 4, 1, f);
    out_map.entities.resize(ent_count);
    for(auto& e : out_map.entities) {
        fread(&e.x, sizeof(float), 1, f);
        fread(&e.y, sizeof(float), 1, f);
        fread(&e.z, sizeof(float), 1, f);
        fread(&e.g_angle, sizeof(float), 1, f);
        fread(&e.v_angle, sizeof(float), 1, f);
        fread(&e.radius, sizeof(float), 1, f);
        fread(&e.v_angles, sizeof(int), 1, f);
        read_floats(f, e.vertices);
        uint32_t tex_count = 0;
        fread(&tex_count, 4, 1, f);
        e.textures.resize(tex_count);
        for(auto& t : e.textures) read_string(f, t);
    }

    uint32_t snd_count = 0;
    fread(&snd_count, 4, 1, f);
    out_map.sounds.resize(snd_count);
    for(auto& snd : out_map.sounds) {
        read_string(f, snd.filename);
        fread(&snd.x, sizeof(float), 1, f);
        fread(&snd.y, sizeof(float), 1, f);
        fread(&snd.z, sizeof(float), 1, f);
        fread(&snd.volume, sizeof(float), 1, f);
        uint8_t loop = 0, spat = 0;
        fread(&loop, 1, 1, f);
        fread(&spat, 1, 1, f);
        snd.looping = (loop != 0);
        snd.spatial = (spat != 0);
    }

    fclose(f);
    cout << "[avomap] карта \"" << out_map.name << "\" загружена из: " << filename
         << "  (объекты: " << obj_count
         << ", сущности: " << ent_count
         << ", звуки: " << snd_count << ")" << endl;
    return true;
}

//              рисовка — статика + все процедурные слои
void draw_map(const GameMap& map, float cam_x, float cam_y, float cam_z) {
    // статичные объекты из файла
    for(const auto& obj : map.objects)
        draw3DObject(obj.cx, obj.cy, obj.cz, obj.r, obj.g, obj.b,
            obj.texture.empty() ? nullptr : obj.texture.c_str(),
            obj.vertices, obj.indices, obj.texcoords);
    // бесконечные процедурные слои
    for(const auto& layer : map.layers)
        layer(cam_x, cam_y, cam_z);
}

//              звуки
void play_map_sounds(const GameMap& map) {
    for(const auto& snd : map.sounds) {
        if(snd.filename.empty()) continue;
        const char* p = snd.filename.c_str();
        if(snd.spatial) {
            if(snd.looping) play_sound_3d_loop(p, snd.x, snd.y, snd.z, snd.volume);
            else            play_sound_3d(p, snd.x, snd.y, snd.z, snd.volume);
        } else {
            if(snd.looping) play_sound_loop(p, snd.volume);
            else            play_sound(p, snd.volume);
        }
    }
}