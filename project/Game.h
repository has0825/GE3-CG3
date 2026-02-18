#pragma once
#include "Framework.h"
#include "SceneFactory.h" // ファクトリーを持つ

class Game : public Framework {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 工場の実体を持つ（SceneManagerにはポインタを渡す）
    std::unique_ptr<SceneFactory> sceneFactory_;
};