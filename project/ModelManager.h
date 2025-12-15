#pragma once
#include <map>
#include <string>
#include <d3d12.h>
#include "Model.h" // ModelCommonDataを使うためインクルード

class ModelManager {
public:
	// シングルトンインスタンス取得
	static ModelManager* GetInstance();

	// 初期化
	void Initialize(ID3D12Device* device);

	// モデル生成
	Model* CreateModel(const std::string& directoryPath, const std::string& filename);

private:
	// コンストラクタ隠蔽
	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	// モデル読み込み（内部処理）
	void LoadModel(const std::string& directoryPath, const std::string& filename);

private:
	static ModelManager* instance;
	ID3D12Device* device_ = nullptr;
	std::map<std::string, ModelCommonData*> modelDatas_;
};