#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <list>
#include "MathTypes.h"
#include "MathUtil.h"

// 最大パーティクル数
const uint32_t kNumMaxInstance = 1024;

class ParticleManager {
public:
    // HLSL側の構造体とメモリレイアウトを合わせる
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Vector4 color; // 色情報も送る場合
    };

    // パーティクル1粒のデータ
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        Vector3 rotate;
        Vector3 scale;
        float lifeTime;
        float currentTime;
        Vector4 color; // 色
    };

    void Initialize(ID3D12Device* device);
    void Update(const Matrix4x4& viewProjectionMatrix);
    void Draw(ID3D12GraphicsCommandList* commandList);

    void Emit(const Vector3& position, const Vector3& velocity);

    // ★追加: 外部（main.cpp）からSRVのGPUハンドルを取得するためのゲッター
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandleGPU() {
        return srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    }

private:
    ID3D12Device* device_ = nullptr;

    // Instancing用リソース
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    TransformationMatrix* instancingData_ = nullptr;

    // パーティクル管理用リスト
    std::list<Particle> particles_;
};