#ifndef TEXTURES
#define TEXTURES

using namespace std; 
extern unordered_map<string, GLuint> textureCache;

extern mutex textureCacheMutex;

extern GLuint boundTextureID;

extern GLuint whiteTex;

GLuint loadTextureFromFile(const char* filename);
void clearTextureCache();
void ensureWhiteTex();

#endif