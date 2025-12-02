#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <list>
#include <string>
#include <random>
#include "MathTypes.h"
#include "externals/DirectXTex/DirectXTex.h"

// 最大パーティクル数
const uint32_t kNumMaxInstance = 1024;

// エフェクトの種類
enum class ParticleType {
    kExplosion, // 爆発
    kFountain,  // 噴水
    kSpiral,    // 螺旋
    kRain       // 雨
};

class ParticleManager {
public:
    // 頂点データの構造体 (GraphicsPipelineのInputLayoutと一致させる)
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    // GPUへ送るデータ構造体
    struct ParticleForGPU {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Vector4 color;
    };

    // CPU計算用データ
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        Vector3 rotate;
        Vector3 scale;
        Vector4 color;
        float lifeTime;
        float currentTime;
    };

    void Initialize(ID3D12Device* device, const std::string& texturePath);
    void Update(const Matrix4x4& viewProjectionMatrix);
    void Draw(ID3D12GraphicsCommandList* commandList);
    void Emit(ParticleType type, const Vector3& position, const Vector3& velocity);

private:
    void CreateParticleGeometry(); // 板ポリゴン作成関数
    void LoadParticleTexture(const std::string& texturePath);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

private:
    ID3D12Device* device_ = nullptr;
    std::mt19937 randomEngine_;

    // --- Instancing (StructuredBuffer) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    // --- 板ポリゴン (パーティクルの形状) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // --- Texture & Heap ---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    uint32_t descriptorSizeSRV_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
    DirectX::TexMetadata metadata_;

    // --- パーティクルリスト ---
    std::list<Particle> particles_;
};