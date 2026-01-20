#include "SceneManager.h"
#include <cassert>

SceneManager* SceneManager::GetInstance() {
    // 静的ローカル変数によるスレッドセーフなシングルトン
    static SceneManager instance;
    return &instance;
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    assert(sceneFactory_);
    assert(nextScene_ == nullptr);

    // ファクトリーから所有権ごと受け取る
    nextScene_ = sceneFactory_->CreateScene(sceneName);
}

void SceneManager::Update() {
    // シーン切り替え処理
    if (nextScene_) {
        // nextScene_の所有権をcurrentScene_へ移動
        // (古いcurrentScene_は自動的に破棄される)
        currentScene_ = std::move(nextScene_);

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