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
	// GPUへ送るデータ構造体 (HLSLと一致させる)
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

	// 初期化 (デバイス、パーティクル用テクスチャパス)
	void Initialize(ID3D12Device* device, const std::string& texturePath);

	// 更新 (カメラ行列を受け取る)
	void Update(const Matrix4x4& viewProjectionMatrix);

	// 描画
	void Draw(ID3D12GraphicsCommandList* commandList);

	// パーティクル発生
	void Emit(ParticleType type, const Vector3& position, const Vector3& velocity);

private:
	// テクスチャロード用ヘルパー
	void LoadParticleTexture(const std::string& texturePath);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

private:
	ID3D12Device* device_ = nullptr;
	std::mt19937 randomEngine_;

	// --- Instancing (StructuredBuffer) ---
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
	ParticleForGPU* instancingData_ = nullptr;

	// --- Texture & Heap ---
	// パーティクル専用のヒープ (Texture SRV と Buffer SRV を同居させるため)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
	uint32_t descriptorSizeSRV_ = 0;

	// テクスチャリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
	DirectX::TexMetadata metadata_;

	// --- パーティクルリスト ---
	std::list<Particle> particles_;
};