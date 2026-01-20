#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <memory>
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"

// マネージャーとモデルで共有するデータ
struct ModelCommonData {
    std::vector<VertexData> vertices;
    MaterialData materialData;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

class Model {
public:
    // パーティクル用のモデル生成 (unique_ptrを返す)
    static std::unique_ptr<Model> CreateParticleModel(ID3D12Device* device);

    Model() = default;
    ~Model() = default;

    // Manager経由: shared_ptr で受け取る
    void Initialize(ID3D12Device* device, std::shared_ptr<ModelCommonData> commonData);

    // 単独初期化
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
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    // 共有データは shared_ptr で持つ
    std::shared_ptr<ModelCommonData> commonData_;

    // 単独管理用
    std::vector<VertexData> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
};