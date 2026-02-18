#include "Game.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "Input.h" // 忘れずに


void Game::Initialize() {
    Framework::Initialize();
    Input::GetInstance()->Initialize(winApp_);

    // 修正: new ではなく std::make_unique を使う
    sceneFactory_ = std::make_unique<SceneFactory>();

    // get() で生ポインタを渡す（所有権は移さない）
    SceneManager::GetInstance()->SetFactory(sceneFactory_.get());
    SceneManager::GetInstance()->ChangeScene("TITLE");
}

void Game::Finalize() {
    // 修正: delete sceneFactory_; は不要になる（自動解放）
    Framework::Finalize();
}

void Game::Update() {
    Framework::Update();

    // 入力情報の更新 (これで全シーンで入力が効くようになります)
    Input::GetInstance()->Update();

    SceneManager::GetInstance()->Update();
}

void Game::Draw() {
    dxCommon_->PreDraw();
    SceneManager::GetInstance()->Draw();
    dxCommon_->PostDraw();
}