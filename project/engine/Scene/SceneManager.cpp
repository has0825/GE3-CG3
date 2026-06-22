#include "SceneManager.h"
#include <cassert>

SceneManager* SceneManager::GetInstance() {
    static SceneManager instance;
    return &instance;
}

SceneManager::~SceneManager() {
    ClearPreviousScene();
    if (currentScene_) {
        currentScene_->Finalize();
    }
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    assert(sceneFactory_); // ファクトリーがセットされていないとエラー
    assert(nextScene_ == nullptr); // 同一フレームでの連続呼び出しは想定しない

    // 予約時にシーン名を記録
    previousSceneName_ = currentSceneName_;
    currentSceneName_ = sceneName;

    // 次のシーンを生成して予約 (修正: ファクトリーから直接unique_ptrを受け取る)
    nextScene_ = sceneFactory_->CreateScene(sceneName);
}

void SceneManager::Update() {
    // シーン切り替え処理
    if (nextScene_) {
        // 新しくシーンを切り替える前に、古い退避済みシーンがあれば完全に解放
        ClearPreviousScene();

        if (currentScene_) {
            // 現在のシーンを退避 (Finalizeはまだ呼ばない)
            previousScene_ = std::move(currentScene_);
        }
        // シーン入れ替え
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

void SceneManager::DrawPreviousScene() {
    if (previousScene_) {
        previousScene_->Draw();
    }
}

void SceneManager::ClearPreviousScene() {
    if (previousScene_) {
        previousScene_->Finalize();
        previousScene_.reset();
    }
}