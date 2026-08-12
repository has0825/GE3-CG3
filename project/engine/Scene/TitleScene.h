#pragma once
#include "BaseScene.h"
#include "Input.h"
#include "Model.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"
#include "DataTypes.h"
#include "Skybox.h"
#include <memory>
#include <wrl.h>
#include "GamePlayScene.h"

// タイトルシーン
class TitleScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    Input* input_ = nullptr;
    GraphicsPipeline* graphicsPipeline_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> titleModel_;
    std::unique_ptr<Skybox> skybox_;
    std::unique_ptr<GamePlayScene> gameplayScene_;
    uint32_t inputDelay_ = 24; // シーン開始時の誤判定防止用のディレイカウンタ

    bool isTransitioningToGame_ = false;       // 遷移演出中フラグ
    float transitionTimer_ = 0.0f;             // 遷移演出タイマー
    const float kTransitionDuration = 2.2f;    // 遷移演出時間 (アングル復旧 1.2s + ブースト 1.0s)

    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformationMatrix* transformData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    struct CameraDataCB {
        Vector3 worldPosition;
        float padding;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraDataCB* cameraDataCB_ = nullptr;
};