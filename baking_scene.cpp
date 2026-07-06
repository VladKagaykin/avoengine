#include "baking_scene.h"
#include "avoengine.h"

bool is_scene_changed = 0;
Function current_scene = nullptr;

void fixed_scene(Function scene){
    current_scene=scene;
    is_scene_changed=1;
}

void clean_scene(){
    current_scene=nullptr;
    is_scene_changed=1;
}