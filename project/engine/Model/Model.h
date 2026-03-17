#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <memory> // ★追加: unique_ptrを使うために必要
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
	// ★変更: 戻り値を std::unique_ptr<Model> に変更
	static std::unique_ptr<Model> CreateParticleModel(ID3D12Device* device);

	Model() = default;
	~Model() = default;

	// ★オーバーロード1: Manager経由で初期化する場合
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

private:
	// 自己管理用データ
	std::vector<VertexData> vertices_;
	MaterialData materialData_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	// 共通データ参照用
	ModelCommonData* commonData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
};