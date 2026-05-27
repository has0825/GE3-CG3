#pragma once
#include "engine/Math/MathTypes.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

// 資料に基づいた構造体
struct ParticleCS {
    Vector3 translate;
    Vector3 scale;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    Vector4 color;
};

struct PerView {
    Matrix4x4 viewProjection;
    Matrix4x4 billboardMatrix;
};

// 資料に基づいたEmitter構造体
struct EmitterSphere {
    Vector3 translate; // 位置
    float radius;      // 射出半径
    uint32_t count;    // 射出数
    float frequency;   // 射出間隔
    float frequencyTime; // 射出間隔調整用時間
    uint32_t emit;     // 射出許可
};

// 資料に基づいたPerFrame構造体
struct PerFrame {
    float time;
    float deltaTime;
};

class GpuParticleManager {
public:
    static const uint32_t kMaxParticles = 1024;

    void Initialize(ID3D12Device* device);
    void Update(const Matrix4x4& viewProjection, const Matrix4x4& billboardMatrix, float deltaTime);
    void Emit();
    void UpdateCS();
    void Draw(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle);

    // エミッターの位置を設定
    void SetTranslate(const Vector3& translate) {
        if (emitterData_) {
            emitterData_->translate = translate;
        }
    }

private:
    void CreateResource();
    void CreateSrvUav();

private:
    ID3D12Device* device_ = nullptr;
    
    // パーティクルリソース (DEFAULT heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    
    // FreeListリソース (DEFAULT heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    
    // Emitterリソース (UPLOAD heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource_;
    EmitterSphere* emitterData_ = nullptr;

    // PerFrameリソース (UPLOAD heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_;
    PerFrame* perFrameData_ = nullptr;

    // PerViewリソース (UPLOAD heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
    PerView* perViewData_ = nullptr;

    // デスクリプタ
    uint32_t srvIndex_;
    uint32_t uavIndex_;
    uint32_t freeListIndexUavIndex_;
    uint32_t freeListUavIndex_;
    
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListIndexUavHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListUavHandleGPU_;

    float time_ = 0.0f;
};
