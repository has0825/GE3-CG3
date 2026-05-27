#include "Skybox.h"
#include "D3D12Util.h"
#include "MathUtil.h"

void Skybox::Initialize(ID3D12Device* device) {
    // 頂点データ (内側を向いた箱・36頂点)
    VertexPos vertices[36] = {
        // 右面
        {{1.0f, 1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, -1.0f, 1.0f}}, {{1.0f, -1.0f, 1.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, -1.0f, 1.0f}}, {{1.0f, -1.0f, -1.0f, 1.0f}},
        // 左面
        {{-1.0f, 1.0f, -1.0f, 1.0f}}, {{-1.0f, 1.0f, 1.0f, 1.0f}}, {{-1.0f, -1.0f, -1.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f, 1.0f}}, {{-1.0f, 1.0f, 1.0f, 1.0f}}, {{-1.0f, -1.0f, 1.0f, 1.0f}},
        // 前面
        {{-1.0f, 1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f, 1.0f}}, {{-1.0f, -1.0f, 1.0f, 1.0f}},
        {{-1.0f, -1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f, 1.0f}}, {{1.0f, -1.0f, 1.0f, 1.0f}},
        // 後面
        {{1.0f, 1.0f, -1.0f, 1.0f}}, {{-1.0f, 1.0f, -1.0f, 1.0f}}, {{1.0f, -1.0f, -1.0f, 1.0f}},
        {{1.0f, -1.0f, -1.0f, 1.0f}}, {{-1.0f, 1.0f, -1.0f, 1.0f}}, {{-1.0f, -1.0f, -1.0f, 1.0f}},
        // 上面
        {{-1.0f, 1.0f, 1.0f, 1.0f}}, {{-1.0f, 1.0f, -1.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f, 1.0f}}, {{-1.0f, 1.0f, -1.0f, 1.0f}}, {{1.0f, 1.0f, -1.0f, 1.0f}},
        // 下面
        {{-1.0f, -1.0f, -1.0f, 1.0f}}, {{-1.0f, -1.0f, 1.0f, 1.0f}}, {{1.0f, -1.0f, -1.0f, 1.0f}},
        {{1.0f, -1.0f, -1.0f, 1.0f}}, {{-1.0f, -1.0f, 1.0f, 1.0f}}, {{1.0f, -1.0f, 1.0f, 1.0f}}
    };

    vertexResource_ = CreateBufferResource(device, sizeof(vertices));
    VertexPos* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    memcpy(vertexData, vertices, sizeof(vertices));
    vertexResource_->Unmap(0, nullptr);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(VertexPos);

    // WVP用の定数バッファ作成
    constBuffer_ = CreateBufferResource(device, sizeof(Matrix4x4));
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constData_));
    *constData_ = MakeIdentity4x4();
}

void Skybox::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& wvp, D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle) {
    *constData_ = wvp;

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // param[0]: WVP (定数バッファ)
    commandList->SetGraphicsRootConstantBufferView(0, constBuffer_->GetGPUVirtualAddress());
    // param[2]: TextureCube (SRV) ※既存のRootSignatureのパラメータ2番がテクスチャである前提
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

    commandList->DrawInstanced(36, 1, 0, 0);
}