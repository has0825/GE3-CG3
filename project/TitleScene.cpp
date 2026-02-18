#include "TitleScene.h"
#include "SceneManager.h"

void TitleScene::Initialize() {
    input_ = Input::GetInstance();
}

void TitleScene::Finalize() {
}

void TitleScene::Update() {
    // スペースキーが押されたらゲームプレイシーンへ遷移
    if (input_->IsKeyTriggered(DIK_SPACE)) {
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
    }
}

void TitleScene::Draw() {
    // 背景のみ描画 (Framework側でClearRenderTargetされているため何もしなくてOK)
}