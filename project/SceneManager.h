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

    // 更新
    void Update();
    // 描画
    void Draw();

    // シーン変更予約
    void ChangeScene(const std::string& sceneName);

    // ファクトリーのセット
    void SetFactory(AbstractSceneFactory* factory) { sceneFactory_ = factory; }

private:
    SceneManager() = default;
    ~SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

private:
    // スマートポインタで管理（自動解放）
    std::unique_ptr<BaseScene> currentScene_ = nullptr;
    std::unique_ptr<BaseScene> nextScene_ = nullptr;

    // シーン生成工場 (所有権は持たない)
    AbstractSceneFactory* sceneFactory_ = nullptr;
};