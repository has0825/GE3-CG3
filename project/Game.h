#pragma once
#include "Framework.h"
#include "SceneFactory.h"
#include <memory>

class Game : public Framework {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 工場の実体を持つ（所有権あり）
    std::unique_ptr<SceneFactory> sceneFactory_ = nullptr;
};