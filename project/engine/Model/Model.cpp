#include "Model.h"
#include <cassert>
#include <cstring>

std::unique_ptr<Model> Model::CreateParticleModel(ID3D12Device* device) {
    auto model = std::make_unique<Model>();
    ModelData modelData;

    // 四角形の頂点定義
    modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

    modelData.material.textureFilePath = "resources/circle.png";
    model->Initialize(modelData, device);
    return model;
}

void Model::Initialize(ID3D12Device* device, std::shared_ptr<ModelCommonData> commonData) {
    commonData_ = commonData; // shared_ptrを保持（参照カウント+1）

    // マテリアル用リソース作成
    materialResource_ = CreateBufferResource(device, sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->enableLighting = 1;
    materialData_->uvTransform = MakeIdentity4x4();
}

void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
    commonData_ = nullptr;
    vertices_ = modelData.vertices;

    vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertices_.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices_.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices_.data(), sizeof(VertexData) * vertices_.size());
    vertexResource_->Unmap(0, nullptr);

    // マテリアル作成
    materialResource_ = CreateBufferResource(device, sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->enableLighting = 1;
    materialData_->uvTransform = MakeIdentity4x4();
}

void Model::Update() {
    // 処理なし
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) {
    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    UINT vertexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        vertexCount = (UINT)commonData_->vertices.size();
    } else {
        vbView = &vertexBufferView_;
        vertexCount = (UINT)vertices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->DrawInstanced(vertexCount, 1, 0, 0);
}

void Model::Draw(ID3D12GraphicsCommandList* commandList, UINT instanceCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle, D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle) {

    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    UINT vertexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        vertexCount = (UINT)commonData_->vertices.size();
    } else {
        vbView = &vertexBufferView_;
        vertexCount = (UINT)vertices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);
    commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}