#pragma once
#include "BaseScene.h"
#include <string>
#include <memory> // 追加

// シーン工場インターフェース
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    // シーン生成メソッド (戻り値をunique_ptrに変更)
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
};