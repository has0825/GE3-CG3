#pragma once
#include <map>
#include <string>
#include <d3d12.h>
#include <wrl.h>
#include "MathTypes.h"
#include "DataTypes.h"

// 前方宣言
class Model;

// モデルの共通データ（頂点バッファやマテリアルなど、重いデータ）
struct ModelCommonData {
	std::vector<VertexData> vertices;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	MaterialData materialData; // テクスチャパスなど
};

class ModelManager {
private:
	// シングルトン用
	static ModelManager* instance;
	ModelManager() = default;
	~ModelManager() = default;

public:
	// シングルトンインスタンス取得
	static ModelManager* GetInstance();

	// 初期化
	void Initialize(ID3D12Device* device);

	// モデル読み込み（すでに読み込み済みなら何もしない）
	void LoadModel(const std::string& directoryPath, const std::string& filename);

	// 読み込んだデータを使ってモデルインスタンスを生成する
	Model* CreateModel(const std::string& directoryPath, const std::string& filename);

private:
	ID3D12Device* device_ = nullptr;

	// モデルデータを名前（ファイル名など）で管理するマップ
	// キー: "directoryPath/filename", 値: 共通データ
	std::map<std::string, ModelCommonData*> modelDatas_;
};