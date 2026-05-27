#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <random>
#include "DataTypes.h" // Matrix4x4やVector3などの定義が含まれるヘッダー
#include "Model.h"

class ParticleManager {
public:
    // GPUへの転送用構造体（シェーダー側と一致させる）
    struct ParticleForGPU {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Vector4 color;
        Matrix4x4 uvTransform;
    };

    // CPUでのパーティクル挙動計算用構造体
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        Vector3 rotate;
        Vector3 scale;
        Vector4 color;
        float lifeTime;
        float currentTime;
        Matrix4x4 uvTransform;
        
        enum class Type {
            kBillboard, // ビルボード板ポリゴン
            kRotation,  // 任意の回転板ポリゴン
        } type;
    };

    void Initialize(ID3D12Device* device);
    
    // カメラの各種行列、デルタタイムを受け取って更新（自動オブジェクトプールを含む）
    void Update(
        const Matrix4x4& viewProjectionMatrix, 
        const Matrix4x4& billboardMatrix, 
        float deltaTime,
        float cameraZ,
        const Vector3& fighterWorldPos,
        bool isBoosting,
        bool isFighterMode,
        int currentEffect,
        const Vector3& emitterPos);
        
    // 描画処理（各種モデルと対応するテクスチャSRVハンドルを受け取って一括描画）
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        Model* particleModel,
        Model* ringModel,
        Model* cylinderModel,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE gradationSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandle);

    // エフェクト発生用API
    void EmitHit(const Vector3& emitterPos);
    void EmitRing(const Vector3& emitterPos);
    void EmitCylinder(const Vector3& emitterPos);

private:
    // 個別のパーティクル生成用のヘルパー関数（自律オブジェクトプール用）
    Particle MakeNewParticle(int type, const Vector3& emitterPos, float cameraZ, const Vector3& fighterWorldPos, bool isBoosting);

private:
    ID3D12Device* device_ = nullptr;
    std::mt19937 randomEngine_;

    // --- 通常/回転パーティクル (最大2000インスタンス) ---
    const uint32_t kNumInstances = 2000;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_;
    std::vector<Particle> particles_;

    // --- リングパーティクル (最大100インスタンス) ---
    const uint32_t kRingInstanceCount = 100;
    Microsoft::WRL::ComPtr<ID3D12Resource> ringInstancingResource_;
    ParticleForGPU* ringInstancingData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE ringInstancingSrvHandleGPU_;
    std::vector<Particle> ringParticles_;

    // --- シリンダーパーティクル (最大50インスタンス) ---
    const uint32_t kCylinderInstanceCount = 50;
    Microsoft::WRL::ComPtr<ID3D12Resource> cylinderInstancingResource_;
    ParticleForGPU* cylinderInstancingData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE cylinderInstancingSrvHandleGPU_;
    std::vector<Particle> cylinderParticles_;
};