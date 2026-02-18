#pragma once
#include "BaseScene.h"
#include <string>

// シーン工場インターフェース
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    // シーン生成メソッド
    virtual BaseScene* CreateScene(const std::string& sceneName) = 0;
};