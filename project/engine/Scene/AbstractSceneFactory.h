#pragma once
#include "BaseScene.h"
#include <string>
#include <memory>

// 抽象シーン工場
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    // シーン生成 (純粋仮想関数の戻り値もunique_ptrに変更)
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
};