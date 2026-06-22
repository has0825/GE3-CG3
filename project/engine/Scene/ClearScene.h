#pragma once
#include "BaseScene.h"
#include "Input.h"
#include "Model.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"
#include "DataTypes.h"
#include "ParticleManager.h"
#include <memory>
#include <wrl.h>
#include <vector>

// ゲームクリアシーン
class ClearScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    Input* input_ = nullptr;
    GraphicsPipeline* graphicsPipeline_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> clearModel_;

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

    // パーティクル管理用
    std::unique_ptr<ParticleManager> particleManager_;
    std::unique_ptr<Model> particleModel_;
    std::unique_ptr<Model> ringModel_;
    std::unique_ptr<Model> cylinderModel_;
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gradationSrvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandle_{};

    // 打ち上げ花火管理用
    struct ActiveFirework {
        Vector3 position;
        Vector3 velocity;
        float timer;
        float maxTime;
        Vector3 color;
    };
    std::vector<ActiveFirework> activeFireworks_;
    float fireworkSpawnTimer_ = 0.0f;
};
