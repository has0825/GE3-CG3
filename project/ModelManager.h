#pragma once
#include <map>
#include <string>
#include <d3d12.h>
#include <memory>
#include "Model.h" 

class ModelManager {
public:
    // シングルトンインスタンス取得
    static ModelManager* GetInstance();

    // 初期化
    void Initialize(ID3D12Device* device);

    // モデル生成 (unique_ptrを返す)
    std::unique_ptr<Model> CreateModel(const std::string& directoryPath, const std::string& filename);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // モデル読み込み（内部処理）
    void LoadModel(const std::string& directoryPath, const std::string& filename);

private:
    ID3D12Device* device_ = nullptr;
    // shared_ptr でデータを管理 (EX条件: 参照カウントによる管理)
    std::map<std::string, std::shared_ptr<ModelCommonData>> modelDatas_;
};