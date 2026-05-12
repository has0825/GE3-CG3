#pragma once

#include "MathUtil.h"
#include "DataTypes.h"
#include <string>
#include <vector>
#include <map>
#include <span>
#include <array>
#include <d3d12.h>
#include <wrl.h>
#include <optional>

namespace AdvAnim {

    struct Node {
        QuaternionTransform transform;
        Matrix4x4 localMatrix;
        std::string name;
        std::vector<Node> children;
    };

    struct Joint {
        QuaternionTransform transform;
        Matrix4x4 localMatrix;
        Matrix4x4 skeletonSpaceMatrix;
        std::string name;
        std::vector<int32_t> children;
        int32_t index;
        std::optional<int32_t> parent;
    };

    struct Skeleton {
        int32_t root;
        std::map<std::string, int32_t> jointMap;
        std::vector<Joint> joints;
    };

    const uint32_t kNumMaxInfluence = 4;
    struct VertexInfluence {
        std::array<float, kNumMaxInfluence> weights;
        std::array<int32_t, kNumMaxInfluence> jointIndices;
    };

    struct WellForGPU {
        Matrix4x4 skeletonSpaceMatrix;
        Matrix4x4 skeletonSpaceInverseTransposeMatrix;
    };

    struct SkinningInformation {
        uint32_t numVertices;
    };

    struct SkinCluster {
        std::vector<Matrix4x4> inverseBindPoseMatrices;
        Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
        D3D12_VERTEX_BUFFER_VIEW influenceBufferView;
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> influenceSrvHandle;
        std::span<VertexInfluence> mappedInfluence;
        Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
        std::span<WellForGPU> mappedPalette;
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle;

        // CS用
        Microsoft::WRL::ComPtr<ID3D12Resource> inputVertexResource;
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> inputVertexSrvHandle;

        Microsoft::WRL::ComPtr<ID3D12Resource> outputVertexResource;
        D3D12_VERTEX_BUFFER_VIEW outputVertexBufferView;
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> outputVertexUavHandle;

        Microsoft::WRL::ComPtr<ID3D12Resource> infoResource;
        SkinningInformation* mappedInfo = nullptr;

        uint32_t paletteSrvIndex;
        uint32_t inputVertexSrvIndex;
        uint32_t influenceSrvIndex;
        uint32_t outputVertexUavIndex;
    };

    struct AnimatedModel {
        ModelData modelData;
        Node rootNode;
        std::map<std::string, Bone> bones;
    };

    template <typename tValue>
    struct Keyframe {
        float time;
        tValue value;
    };
    using KeyframeVector3 = Keyframe<Vector3>;
    using KeyframeQuaternion = Keyframe<Quaternion>;

    template<typename tValue>
    struct AnimationCurve {
        std::vector<Keyframe<tValue>> keyframes;
    };

    struct NodeAnimation {
        AnimationCurve<Vector3> translate;
        AnimationCurve<Quaternion> rotate;
        AnimationCurve<Vector3> scale;
    };

    struct Animation {
        float duration; // アニメーション全体の尺（単位は秒）
        std::map<std::string, NodeAnimation> nodeAnimations;
    };

    AnimatedModel LoadModelFile(const std::string& directoryPath, const std::string& filename);
    Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename);

    Skeleton CreateSkeleton(const Node& rootNode);
    void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime);
    void Update(Skeleton& skeleton);

    SkinCluster CreateSkinCluster(
        ID3D12Device* device,
        const Skeleton& skeleton,
        const ModelData& modelData,
        ID3D12DescriptorHeap* descriptorHeap,
        uint32_t descriptorSize);

    void Update(SkinCluster& skinCluster, const Skeleton& skeleton);

    Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

} // namespace AdvAnim
