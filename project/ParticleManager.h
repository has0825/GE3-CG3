#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <list>
#include "MathTypes.h" // Matrix4x4やVector3などの定義が含まれるヘッダー
#include "MathTypes.h"

// 最大パーティクル数（スライドでは大きく確保する方針でした）
const uint32_t kNumMaxInstance = 1024;

class ParticleManager {
public:
    // HLSL側の構造体とメモリレイアウトを合わせる
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

    // パーティクル1粒のデータ（CPUでの計算用）
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        Vector3 rotate;
        Vector3 scale;
        float lifeTime;
        float currentTime;
        // 必要に応じて色や速度などを追加
    };

    void Initialize(ID3D12Device* device);
    void Update(const Matrix4x4& viewProjectionMatrix); // カメラ行列を受け取る
    void Draw(ID3D12GraphicsCommandList* commandList);

    // パーティクル発生用関数（例）
    void Emit(const Vector3& position, const Vector3& velocity);

private:
    ID3D12Device* device_ = nullptr;

    // --- Instancing用リソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    TransformationMatrix* instancingData_ = nullptr; // 書き込み用ポインタ

    // --- SRV用デスクリプタヒープ ---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    // --- パーティクル管理 ---
    std::list<Particle> particles_; // 発生・消滅があるのでlistが便利

    // モデルデータ（板ポリゴンなど）のバッファ等は別途必要ですが、
    // ここではInstancing関連に絞っています
};