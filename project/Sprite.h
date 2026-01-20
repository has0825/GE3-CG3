#pragma once
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include <memory>

#include "D3D12Util.h"
#include "MathTypes.h"
#include "DataTypes.h"
#include "TextureManager.h"

class Sprite {
public:
    // テクスチャ名を指定してスプライト生成 (unique_ptr)
    static std::unique_ptr<Sprite> Create(const std::string& textureName, Vector2 position);

    void Initialize(const std::string& textureName, Vector2 position);
    void Update();
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection);

    // 範囲指定で切り取る
    void SetTextureRect(float left, float top, float width, float height);

    Transform transform;

private:
    std::string textureName_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    DirectX::TexMetadata metadata_;
};