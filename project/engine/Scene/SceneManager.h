#pragma once
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include <string>
#include <memory>

// シーン管理クラス (Singleton)
class SceneManager {
public:
    // シングルトンインスタンス取得
    static SceneManager* GetInstance();

    // デストラクタ
    ~SceneManager();

    // 更新
    void Update();
    // 描画
    void Draw();

    // シーン変更予約 (次のフレームの頭で切り替わる)
    void ChangeScene(const std::string& sceneName);

    // ファクトリーのセット (初期化時に必須)
    void SetFactory(AbstractSceneFactory* factory) { sceneFactory_ = factory; }

    // 直前のシーン名を取得
    const std::string& GetPreviousSceneName() const { return previousSceneName_; }

    // 直前のシーンを描画
    void DrawPreviousScene();
    // 直前のシーンを完全に解放
    void ClearPreviousScene();

private:
    SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

private:
    // 修正: unique_ptr に変更
    std::unique_ptr<BaseScene> currentScene_;
    std::unique_ptr<BaseScene> nextScene_;
    // 一時退避用の前シーン
    std::unique_ptr<BaseScene> previousScene_;

    // ファクトリーは所有権を持たないので生ポインタ(or weak_ptr)でOK
    AbstractSceneFactory* sceneFactory_ = nullptr;

    // 現在のシーン名と直前のシーン名
    std::string currentSceneName_;
    std::string previousSceneName_;
};