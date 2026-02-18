#include "SceneFactory.h"
#include "TitleScene.h"
#include "GamePlayScene.h"

BaseScene* SceneFactory::CreateScene(const std::string& sceneName) {
    if (sceneName == "TITLE") {
        return new TitleScene();
    } else if (sceneName == "GAMEPLAY") {
        return new GamePlayScene();
    }

    return nullptr;
}