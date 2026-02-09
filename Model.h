#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "D3D12Util.h"
#include "DataTypes.h" 
#include "MathUtil.h"

// ★追加: Node構造体 (資料より)
struct Node {
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
};

class Model {
public:
    // ファイルからモデル生成 (OBJ / glTF対応)
    static Model* Create(const std::string& directoryPath, const std::string& filename, ID3D12Device* device);

    // パーティクル用の四角形モデル生成
    static Model* CreateParticleModel(ID3D12Device* device);

    // 球体モデル生成
    static Model* CreateSphereModel(ID3D12Device* device, uint32_t subdivision);

    // デストラクタ
    ~Model() = default;

    void Update();

    // インスタンシング描画用のDraw関数
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        UINT instanceCount,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle);

public:
    Transform transform;
    Material* materialData = nullptr;

    // ★追加: ルートノード (階層構造の起点)
    Node rootNode;

private:
    // 内部初期化用
    void Initialize(const ModelData& modelData, ID3D12Device* device);

private:
    // 頂点データ
    std::vector<VertexData> vertices_;
    ModelData modelData_;

    // リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
};