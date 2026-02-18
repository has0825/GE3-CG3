#include "SceneManager.h"
#include <cassert>

SceneManager* SceneManager::GetInstance() {
    static SceneManager instance;
    return &instance;
}

SceneManager::~SceneManager() {
    if (currentScene_) {
        currentScene_->Finalize();
    }
    // nextScene_ は予約だけで生成されていないはずだが、念のため
    if (nextScene_) {
    }
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    assert(sceneFactory_); // ファクトリーがセットされていないとエラー
    assert(nextScene_ == nullptr); // 同一フレームでの連続呼び出しは想定しない

    // 次のシーンを生成して予約
    nextScene_ = std::unique_ptr<BaseScene>(sceneFactory_->CreateScene(sceneName));
}

void SceneManager::Update() {
    // シーン切り替え処理
    if (nextScene_) {
        // 現在のシーンがあれば終了処理
        if (currentScene_) {
            currentScene_->Finalize();
        }
        // シーン入れ替え
        currentScene_ = std::move(nextScene_);
        // nextScene_ は nullptr になる

        // 新しいシーンの初期化
        currentScene_->Initialize();
    }

    // 現在のシーンの更新
    if (currentScene_) {
        currentScene_->Update();
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        currentScene_->Draw();
    }
}