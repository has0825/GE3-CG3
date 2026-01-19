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

private:
    SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

private:
    // 現在のシーン
    BaseScene* currentScene_ = nullptr;
    // 次のシーン (予約用)
    BaseScene* nextScene_ = nullptr;

    // シーン生成工場 (依存性注入)
    AbstractSceneFactory* sceneFactory_ = nullptr;
};