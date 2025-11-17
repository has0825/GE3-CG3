#pragma once
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"
#include <string>
#include <vector>

// 前方宣言
struct ModelCommonData;

class Model {
	friend class ModelManager;

public:
	void Update();

	void Draw(
		ID3D12GraphicsCommandList* commandList,
		const Matrix4x4& viewProjectionMatrix);

	// ★修正: デストラクタをpublicにする
	~Model() = default;

public:
	Transform transform;

private:
	// コンストラクタはprivateのまま（生成はManager経由のみ）
	Model() = default;
	void Initialize(ID3D12Device* device, ModelCommonData* commonData);

private:
	ModelCommonData* commonData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};