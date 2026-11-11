// Registry of the available worlds (select with --scene NAME).
#include "scene.hpp"

extern const Scene kFlatScene;     // scenes/flat/flat.cpp
extern const Scene kIslandScene;   // scenes/island/island.cpp

const std::vector<const Scene*>& allScenes() {
    static const std::vector<const Scene*> list = {&kFlatScene, &kIslandScene};
    return list;
}

const Scene* findScene(const std::string& name) {
    for (const Scene* s : allScenes())
        if (name == s->name) return s;
    return nullptr;
}
