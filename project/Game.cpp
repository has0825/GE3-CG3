#include "Game.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "Input.h"
#include "TextureManager.h" // ★追加: これを忘れないでください

void Game::Initialize() {
    // 基底クラスの初期化 (ここで dxCommon_ が初期化されます)
    Framework::Initialize();

    // ★追加: TextureManagerの初期化
    // DirectXデバイスを渡し、テクスチャのあるディレクトリを指定します
    // "Resources/" は実際のテクスチャフォルダに合わせて変更してください
    TextureManager::GetInstance()->Initialize(dxCommon_->GetDevice(), "Resources/");

    // Inputシステムの初期化
    Input::GetInstance()->Initialize(winApp_);

    // シーンファクトリーを生成 (make_unique)
    sceneFactory_ = std::make_unique<SceneFactory>();

    // マネージャーにはポインタを渡す
    SceneManager::GetInstance()->SetFactory(sceneFactory_.get());

    // タイトルシーンから開始
    SceneManager::GetInstance()->ChangeScene("TITLE");
}

void Game::Finalize() {
    // unique_ptr が自動解放するため delete 不要
    Framework::Finalize();
}

void Game::Update() {
    Framework::Update();

    // 入力情報の更新
    Input::GetInstance()->Update();

    SceneManager::GetInstance()->Update();
}

void Game::Draw() {
    dxCommon_->PreDraw();
    SceneManager::GetInstance()->Draw();
    dxCommon_->PostDraw();
}