#pragma once
#include "BaseScene.h"
#include "Input.h"

// タイトルシーン
class TitleScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 入力クラスへのポインタ
    Input* input_ = nullptr;
};