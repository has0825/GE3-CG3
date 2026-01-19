#include "Game.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "Input.h" // 忘れずに
#include "D3D12Util.h"

void Game::Initialize() {
    // 基底クラスの初期化
    Framework::Initialize();

    // Inputシステムの初期化
    Input::GetInstance()->Initialize(winApp_);

    // シーンファクトリーを生成し、マネージャーにセット
    sceneFactory_ = new SceneFactory();
    SceneManager::GetInstance()->SetFactory(sceneFactory_);

    // ★ここを変更: スタート地点をタイトルシーンに戻す
    SceneManager::GetInstance()->ChangeScene("TITLE");
}

void Game::Finalize() {
    delete sceneFactory_;
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