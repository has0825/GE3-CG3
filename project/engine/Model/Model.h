#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <memory>
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"
#include "Camera.h"

struct Node {
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
};

struct ModelCommonData {
    std::vector<VertexData> vertices;
    MaterialData materialData;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

class Model {
public:
    static std::unique_ptr<Model> CreateParticleModel(ID3D12Device* device);
    static std::unique_ptr<Model> LoadGLTF(const std::string& filename, ID3D12Device* device);

    Model() = default;
    ~Model() = default;

    void Initialize(ID3D12Device* device, ModelCommonData* commonData);
    void Initialize(const ModelData& modelData, ID3D12Device* device);

    void Update();

    void Draw(ID3D12GraphicsCommandList* commandList);

    void Draw(
        ID3D12GraphicsCommandList* commandList,
        UINT instanceCount,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle);

    void DrawModel(
        ID3D12GraphicsCommandList* commandList,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandle);

    void SetEnvironmentCoefficient(float coefficient);

    Camera::Transform transform;
    float environmentCoefficient = 0.0f;
    Node rootNode;

private:
    ModelCommonData* commonData_ = nullptr;
    std::vector<VertexData> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
};