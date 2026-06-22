#pragma once
#include "BaseScene.h"
#include "Input.h"
#include "Model.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"
#include "DataTypes.h"
#include <memory>
#include <wrl.h>

// ゲームオーバーシーン
class GameOverScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    Input* input_ = nullptr;
    GraphicsPipeline* graphicsPipeline_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> gameOverModel_;

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

    // プレイヤーモデル描画用
    std::unique_ptr<Model> playerModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> playerTransformResource_;
    TransformationMatrix* playerTransformData_ = nullptr;

    // 床モデル描画用
    std::unique_ptr<Model> floorModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> floorTransformResource_;
    TransformationMatrix* floorTransformData_ = nullptr;
};
