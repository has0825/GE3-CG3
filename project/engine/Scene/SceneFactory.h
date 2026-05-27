#pragma once
#include "AbstractSceneFactory.h"
#include <memory>
#include <string>

// 具体的なシーン工場
class SceneFactory : public AbstractSceneFactory {
public:
    // シーン生成 (戻り値をunique_ptrに変更)
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;
};