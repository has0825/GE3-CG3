#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"
#include "Camera.h"
#include "AdvancedAnimation.h"

struct aiNode;

struct Node {
    Matrix4x4 localMatrix = MakeIdentity4x4();
    std::string name;
    std::vector<Node> children;
};



struct AnimationKey {
    float time;
    Vector3 value;
};

struct AnimationRotationKey {
    float time;
    Quaternion value;
};

struct AnimationChannel {
    std::string nodeName;
    std::vector<AnimationKey> positionKeys;
    std::vector<AnimationRotationKey> rotationKeys;
    std::vector<AnimationKey> scaleKeys;
};

struct Animation {
    std::string name;
    float duration;
    float ticksPerSecond;
    std::vector<AnimationChannel> channels;
};

const uint32_t kMaxBones = 128;
struct SkinningPalette {
    Matrix4x4 boneMatrices[kMaxBones];
};

struct ModelCommonData {
    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
    MaterialData materialData;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource;
    D3D12_INDEX_BUFFER_VIEW indexBufferView{};
};

class Model {
public:
    static std::unique_ptr<Model> CreateParticleModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateRingModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateCylinderModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateSphereModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateBoxModel(ID3D12Device* device);
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

    void DrawSkinningModel(
        ID3D12GraphicsCommandList* commandList,
        const AdvAnim::SkinCluster& skinCluster,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandle,
        D3D12_GPU_VIRTUAL_ADDRESS transformationMatrixAddress);

    void SetEnvironmentCoefficient(float coefficient);
    void SetColor(const Vector4& color);
    void UpdateAnimation(float deltaTime);

    EulerTransform transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    float environmentCoefficient = 0.0f;
    Node rootNode;

    // スキニング関連
    std::map<std::string, Bone> bones_;
    std::unique_ptr<Animation> animation_;
    float animationTime_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningResource_;
    SkinningPalette* skinningData_ = nullptr;

private:
    ModelCommonData* commonData_ = nullptr;
    std::vector<VertexData> vertices_;
    std::vector<uint32_t> indices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    void ReadNodeHierarchy(struct aiNode* node, Node& outNode);
    void ComputeSkinningMatrices(Node& node, const Matrix4x4& parentMatrix);
};