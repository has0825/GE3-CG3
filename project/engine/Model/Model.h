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

	~Model() = default;

	// ★追加: テクスチャを強制的に指定する関数
	void SetTexture(const std::string& textureFilePath) {
		textureOverride_ = textureFilePath;
	}

public:
	Transform transform;

private:
	Model() = default;
	void Initialize(ID3D12Device* device, ModelCommonData* commonData);

private:
	ModelCommonData* commonData_ = nullptr;

	// ★追加: 強制指定用のテクスチャパス保存場所
	std::string textureOverride_;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};