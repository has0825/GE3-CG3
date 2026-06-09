#include "SceneFactory.h"
#include "TitleScene.h"
#include "GamePlayScene.h"
#include "ClearScene.h"
#include "GameOverScene.h"

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {
    if (sceneName == "TITLE") {
        return std::make_unique<TitleScene>();
    } else if (sceneName == "GAMEPLAY") {
        return std::make_unique<GamePlayScene>();
    } else if (sceneName == "CLEAR") {
        return std::make_unique<ClearScene>();
    } else if (sceneName == "GAMEOVER") {
        return std::make_unique<GameOverScene>();
    }

    return nullptr;
}