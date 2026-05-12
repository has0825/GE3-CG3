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

class GpuParticleManager {
public:
    static const uint32_t kMaxParticles = 1024;

    void Initialize(ID3D12Device* device);
    void Update(const Matrix4x4& viewProjection, const Matrix4x4& billboardMatrix);
    void Draw(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle);

private:
    void CreateResource();
    void CreateSrvUav();

private:
    ID3D12Device* device_ = nullptr;
    
    // パーティクルリソース (DEFAULT heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    
    // PerViewリソース (UPLOAD heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
    PerView* perViewData_ = nullptr;

    // デスクリプタ
    uint32_t srvIndex_ = 0;
    uint32_t uavIndex_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandleGPU_;
};
