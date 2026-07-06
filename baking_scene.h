#ifndef BAKING_SCENE
#define BAKING_SCENE

extern bool is_scene_changed;
using Function = void(*)();
extern Function current_scene;

void fixed_scene(Function scene);
void clean_scene();

#endif