#pragma once
#include <map>
#include <string>
#include <memory>
#include <d3d12.h>
#include "Model.h"

class ModelManager {
public:
    // シングルトンインスタンス取得（静的ローカル変数でnewを排除）
    static ModelManager* GetInstance();

    // 初期化
    void Initialize(ID3D12Device* device);

    // モデル生成（戻り値をunique_ptrにすることで所有権を明確化）
    std::unique_ptr<Model> CreateModel(const std::string& directoryPath, const std::string& filename);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 内部処理（shared_ptrを使用）
    void LoadModel(const std::string& directoryPath, const std::string& filename);

private:
    ID3D12Device* device_ = nullptr;
    // 必須条件：shared_ptr を活用。これでプログラム終了時に全モデルが自動解放される
    std::map<std::string, std::shared_ptr<ModelCommonData>> modelDatas_;
};