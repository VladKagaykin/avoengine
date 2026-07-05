#include "avoengine.h"
#include "textures.h"

#include <iostream>
#include <SOIL/SOIL.h>

// создание таблицы текстур и их id 
//       имя файла текстуры  его id        
unordered_map<string, GLuint> textureCache;
// переменная для хранения того, обрабатывает ли текстуры какой-то поток или нет(вроде бы, не знаю как точно)
mutex textureCacheMutex;
// храним id последней загруженной текстуры
GLuint boundTextureID = 0;

GLuint whiteTex = 0;

void ensureWhiteTex() {
    if (whiteTex == 0) {
        unsigned char white[4] = {255,255,255,255};
        glGenTextures(1, &whiteTex);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

//              текстуры
// функция для загрузки текстуры
GLuint loadTextureFromFile(const char* filename) {
    {
        lock_guard<mutex> lock(textureCacheMutex);
        auto it = textureCache.find(filename);
        if (it != textureCache.end()) return it->second;
    }

    int w, h, channels;
    unsigned char* img = SOIL_load_image(filename, &w, &h, &channels, SOIL_LOAD_AUTO);
    if (!img) {
        cerr << "Cannot load texture: " << filename << " (" << SOIL_last_result() << ")" << endl;
        lock_guard<mutex> lock(textureCacheMutex);
        return textureCache[filename] = 0;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    boundTextureID = id;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

    GLint internalFormat = format;
    if (channels == 4 && glewIsSupported("GL_EXT_texture_compression_s3tc")) {
        internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    } else if (channels == 3 && glewIsSupported("GL_EXT_texture_compression_s3tc")) {
        internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, img);

    glGenerateMipmap(GL_TEXTURE_2D);

    SOIL_free_image_data(img);

    lock_guard<mutex> lock(textureCacheMutex);
    return textureCache[filename] = id;
}
// удаляем все текстуры из памяти
void clearTextureCache(){
    // перебираем все имена и id и удаляем их
    for(auto& [name,id] : textureCache)
        if(id) glDeleteTextures(1, &id);
    textureCache.clear();
    boundTextureID=0;
}

// проверка на то, привязана ли эта текстура или нет
void bindTexture(GLuint id){
    if (id!=boundTextureID){
        glBindTexture(GL_TEXTURE_2D,id);
        boundTextureID=id;
    }
}