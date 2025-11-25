#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"

class Model {
public:
    // OBJファイルからモデル生成
    static Model* Create(const std::string& directoryPath, const std::string& filename, ID3D12Device* device);

    // パーティクル用の四角形モデル生成
    static Model* CreateParticleModel(ID3D12Device* device);

    // デストラクタ（リソース解放用など必要であれば記述、今回はComPtrなので自動解放）
    ~Model() = default;

    void Update();

    // インスタンシング描画用のDraw関数
    // StructuredBuffer(SRV)とTexture(SRV)のGPUハンドルを受け取る
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        UINT instanceCount,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle);

public:
    Transform transform;
    Material* materialData = nullptr;

private:
    // 内部初期化用
    void Initialize(const ModelData& modelData, ID3D12Device* device);

private:
    // 頂点データ
    std::vector<VertexData> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // マテリアルデータ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // 通常描画用（今回のパーティクルでは使用しないが初期化のため保持）
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};