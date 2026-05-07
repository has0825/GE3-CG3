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

struct aiNode;

struct Node {
    Matrix4x4 localMatrix = MakeIdentity4x4();
    std::string name;
    std::vector<Node> children;
};

struct Bone {
    std::string name;
    uint32_t index;
    Matrix4x4 offsetMatrix;
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
    MaterialData materialData;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

class Model {
public:
    static std::unique_ptr<Model> CreateParticleModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateRingModel(ID3D12Device* device);
    static std::unique_ptr<Model> CreateCylinderModel(ID3D12Device* device);
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
    void UpdateAnimation(float deltaTime);

    Camera::Transform transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    float environmentCoefficient = 0.0f;
    Node rootNode;

private:
    ModelCommonData* commonData_ = nullptr;
    std::vector<VertexData> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // スキニング関連
    std::map<std::string, Bone> bones_;
    std::unique_ptr<Animation> animation_;
    float animationTime_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningResource_;
    SkinningPalette* skinningData_ = nullptr;

    void ReadNodeHierarchy(struct aiNode* node, Node& outNode);
    void ComputeSkinningMatrices(Node& node, const Matrix4x4& parentMatrix);
};