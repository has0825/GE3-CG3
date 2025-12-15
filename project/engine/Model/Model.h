#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"

// マネージャーとモデルで共有するデータ（頂点バッファなど）
struct ModelCommonData {
	std::vector<VertexData> vertices;
	MaterialData materialData;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

class Model {
public:
	// ★パーティクル用のモデル生成 (Managerを使わない単独生成)
	static Model* CreateParticleModel(ID3D12Device* device);

	Model() = default;
	~Model() = default;

	// ★オーバーロード1: Manager経由で初期化する場合 (今回のエラーを直すためのもの)
	// 引数: デバイス, 共通データへのポインタ
	void Initialize(ID3D12Device* device, ModelCommonData* commonData);

	// ★オーバーロード2: 単独で初期化する場合 (パーティクル等)
	// 引数: モデルデータ, デバイス
	void Initialize(const ModelData& modelData, ID3D12Device* device);

	void Update();

	// 通常描画
	void Draw(ID3D12GraphicsCommandList* commandList);

	// インスタンシング描画
	void Draw(
		ID3D12GraphicsCommandList* commandList,
		UINT instanceCount,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle);

public:
	Transform transform;

	// マテリアルデータ (定数バッファの中身へのアクセス用)
	Material* materialData = nullptr;

private:
	// 共通データへのポインタ (Manager管理の場合に使用)
	ModelCommonData* commonData_ = nullptr;

	// --- 独自管理用 (パーティクルなどManagerを使わない場合に使用) ---
	std::vector<VertexData> vertices_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	// --- 定数バッファ (マテリアル・WVP) ---
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};