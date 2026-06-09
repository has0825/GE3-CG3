#pragma once
#include <string>
#include <memory> // ★追加
#include <wrl.h>
#include <d3d12.h>

// 必要なヘッダーをインクルード
#include "D3D12Util.h"
#include "MathTypes.h"
#include "DataTypes.h"
#include "TextureManager.h"

class Sprite {
public:
    // ★戻り値を unique_ptr に変更
    static std::unique_ptr<Sprite> Create(const std::string& textureName, Vector2 position);

    void Initialize(const std::string& textureName, Vector2 position);
    void Update();
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection);

    // 範囲指定で切り取る (左上XY, 幅, 高さ)
    void SetTextureRect(float left, float top, float width, float height);

    // 色・アルファ値の設定
    void SetColor(const Vector4& color);

    // テクスチャメタデータの取得
    const DirectX::TexMetadata& GetMetadata() const { return metadata_; }

    EulerTransform transform; // 座標、回転、スケール

private:
    std::string textureName_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // テクスチャの元サイズ
    DirectX::TexMetadata metadata_;
};