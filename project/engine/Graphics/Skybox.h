#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "DataTypes.h" // Vector4, Matrix4x4用 (環境に合わせて変更してください)

class Skybox {
public:
    void Initialize(ID3D12Device* device);
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& wvp, D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle);

private:
    struct VertexPos {
        Vector4 position;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    Matrix4x4* constData_ = nullptr;
};