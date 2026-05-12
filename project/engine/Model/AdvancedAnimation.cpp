#include "AdvancedAnimation.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>
#include "D3D12Util.h"
#include "TextureManager.h"
#include "MathUtil.h"
#include "SrvManager.h"

namespace AdvAnim {

    void ReadNodeHierarchy(aiNode* node, Node& outNode) {
        aiVector3D scale, translate;
        aiQuaternion rotate;
        node->mTransformation.Decompose(scale, rotate, translate);

        outNode.transform.scale = { scale.x, scale.y, scale.z };
        outNode.transform.rotate = { rotate.x, rotate.y, rotate.z, rotate.w };
        outNode.transform.translate = { translate.x, translate.y, translate.z };

        outNode.localMatrix = MakeAffineMatrix(outNode.transform.scale, outNode.transform.rotate, outNode.transform.translate);

        outNode.name = node->mName.C_Str();
        outNode.children.resize(node->mNumChildren);
        for (uint32_t i = 0; i < node->mNumChildren; ++i) {
            ReadNodeHierarchy(node->mChildren[i], outNode.children[i]);
        }
    }

    int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints) {
        Joint joint;
        joint.name = node.name;
        joint.localMatrix = node.localMatrix;
        joint.skeletonSpaceMatrix = MakeIdentity4x4();
        joint.transform = node.transform;
        joint.index = int32_t(joints.size());
        joint.parent = parent;
        joints.push_back(joint);

        for (const Node& child : node.children) {
            int32_t childIndex = CreateJoint(child, joint.index, joints);
            joints[joint.index].children.push_back(childIndex);
        }
        return joint.index;
    }

    Skeleton CreateSkeleton(const Node& rootNode) {
        Skeleton skeleton;
        skeleton.root = CreateJoint(rootNode, {}, skeleton.joints);

        for (const Joint& joint : skeleton.joints) {
            skeleton.jointMap.emplace(joint.name, joint.index);
        }

        Update(skeleton); // 初期行列計算
        return skeleton;
    }

    void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime) {
        for (Joint& joint : skeleton.joints) {
            if (auto it = animation.nodeAnimations.find(joint.name); it != animation.nodeAnimations.end()) {
                const NodeAnimation& rootNodeAnimation = (*it).second;
                joint.transform.translate = CalculateValue(rootNodeAnimation.translate.keyframes, animationTime);
                joint.transform.rotate = CalculateValue(rootNodeAnimation.rotate.keyframes, animationTime);
                joint.transform.scale = CalculateValue(rootNodeAnimation.scale.keyframes, animationTime);
            }
        }
    }

    void Update(Skeleton& skeleton) {
        for (Joint& joint : skeleton.joints) {
            joint.localMatrix = MakeAffineMatrix(joint.transform.scale, joint.transform.rotate, joint.transform.translate);
            if (joint.parent) {
                joint.skeletonSpaceMatrix = Multiply(joint.localMatrix, skeleton.joints[*joint.parent].skeletonSpaceMatrix);
            } else {
                joint.skeletonSpaceMatrix = joint.localMatrix;
            }
        }
    }

    AnimatedModel LoadModelFile(const std::string& directoryPath, const std::string& filename) {
        AnimatedModel model;
        Assimp::Importer importer;
        std::string filePath = directoryPath + "/" + filename;
        const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_LimitBoneWeights | aiProcess_ConvertToLeftHanded | aiProcess_PopulateArmatureData);

        if (!scene || !scene->mRootNode) {
            return model;
        }

        // メッシュの読み込み
        for (uint32_t i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[i];
            
            // 頂点解析
            uint32_t vertexStart = (uint32_t)model.modelData.vertices.size();
            for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {
                VertexData vertex{};
                vertex.position = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f };
                if (mesh->HasNormals()) {
                    vertex.normal = { mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z };
                } else {
                    vertex.normal = { 0.0f, 0.0f, 1.0f };
                }
                if (mesh->mTextureCoords[0]) {
                    vertex.texcoord = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };
                } else {
                    vertex.texcoord = { 0.0f, 0.0f };
                }
                model.modelData.vertices.push_back(vertex);
            }

            // インデックス解析
            for (uint32_t j = 0; j < mesh->mNumFaces; ++j) {
                aiFace& face = mesh->mFaces[j];
                assert(face.mNumIndices == 3);
                for (uint32_t k = 0; k < face.mNumIndices; ++k) {
                    model.modelData.indices.push_back(vertexStart + face.mIndices[k]);
                }
            }

            // スキニングデータの抽出
            for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
                aiBone* bone = mesh->mBones[boneIndex];
                std::string jointName = bone->mName.C_Str();
                JointWeightData& jointWeightData = model.modelData.skinClusterData[jointName];

                aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix.Inverse();
                aiVector3D scale, translate;
                aiQuaternion rotate;
                bindPoseMatrixAssimp.Decompose(scale, rotate, translate);
                Matrix4x4 bindPoseMatrix = MakeAffineMatrix(
                    { scale.x, scale.y, scale.z }, 
                    { rotate.x, -rotate.y, -rotate.z, rotate.w }, 
                    { -translate.x, translate.y, translate.z });
                jointWeightData.inverseBindPoseMatrix = Inverse(bindPoseMatrix);

                for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                    jointWeightData.vertexWeights.push_back({ bone->mWeights[weightIndex].mWeight, vertexStart + bone->mWeights[weightIndex].mVertexId });
                }
            }
        }

        // ノード階層の読み込み
        ReadNodeHierarchy(scene->mRootNode, model.rootNode);

        return model;
    }

    Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename) {
        Animation animation; // 今回作るアニメーション
        Assimp::Importer importer;
        std::string filePath = directoryPath + "/" + filename;
        const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_ConvertToLeftHanded);
        
        if (!scene || scene->mNumAnimations == 0) {
            // アニメーションがない、または読み込み失敗
            return animation;
        }

        aiAnimation* animationAssimp = scene->mAnimations[0]; // 最初のアニメーションだけ採用
        animation.duration = float(animationAssimp->mDuration / animationAssimp->mTicksPerSecond); // 時間の単位を秒に変換

        for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {
            aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
            NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];

            for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
                aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
                KeyframeVector3 keyframe;
                keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond); // 秒に変換
                keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
                nodeAnimation.translate.keyframes.push_back(keyframe);
            }

            for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
                aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
                KeyframeQuaternion keyframe;
                keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
                // 右手->左手変換 (yとzを反転)
                keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z, keyAssimp.mValue.w };
                nodeAnimation.rotate.keyframes.push_back(keyframe);
            }

            for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
                aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
                KeyframeVector3 keyframe;
                keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
                // Scaleはそのまま
                keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
                nodeAnimation.scale.keyframes.push_back(keyframe);
            }
        }

        return animation;
    }

    Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time) {
        assert(!keyframes.empty());
        if (keyframes.size() == 1 || time <= keyframes[0].time) {
            return keyframes[0].value;
        }

        for (size_t index = 0; index < keyframes.size() - 1; ++index) {
            size_t nextIndex = index + 1;
            if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
                float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
                return Lerp(keyframes[index].value, keyframes[nextIndex].value, t);
            }
        }
        return (*keyframes.rbegin()).value;
    }

    Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time) {
        assert(!keyframes.empty());
        if (keyframes.size() == 1 || time <= keyframes[0].time) {
            return keyframes[0].value;
        }

        for (size_t index = 0; index < keyframes.size() - 1; ++index) {
            size_t nextIndex = index + 1;
            if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
                float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
                return Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
            }
        }
        return (*keyframes.rbegin()).value;
    }

    SkinCluster CreateSkinCluster(
        ID3D12Device* device,
        const Skeleton& skeleton,
        const ModelData& modelData,
        ID3D12DescriptorHeap* descriptorHeap,
        uint32_t descriptorSize) {
        
        SkinCluster skinCluster;

        // palette用リソースの確保
        skinCluster.paletteResource = CreateBufferResource(device, sizeof(WellForGPU) * skeleton.joints.size());
        WellForGPU* mappedPalette = nullptr;
        skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
        skinCluster.mappedPalette = { mappedPalette, skeleton.joints.size() };

        // influence用リソースの確保
        skinCluster.influenceResource = CreateBufferResource(device, sizeof(VertexInfluence) * modelData.vertices.size());
        VertexInfluence* mappedInfluence = nullptr;
        skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
        std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * modelData.vertices.size());
        skinCluster.mappedInfluence = { mappedInfluence, modelData.vertices.size() };

        // inputVertex用リソース (モデルの頂点データをSRVとして扱う)
        skinCluster.inputVertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());
        VertexData* mappedInputVertex = nullptr;
        skinCluster.inputVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInputVertex));
        std::memcpy(mappedInputVertex, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
        skinCluster.inputVertexResource->Unmap(0, nullptr);

        // outputVertex用リソース (UAV)
        skinCluster.outputVertexResource = CreateUAVBufferResource(device, sizeof(VertexData) * modelData.vertices.size());

        // --- ディスクリプタの割当 (t0, t1, t2 の順に連続させる) ---
        // t0: MatrixPalette
        uint32_t paletteSrvIndex = SrvManager::GetInstance()->Allocate();
        skinCluster.paletteSrvIndex = paletteSrvIndex;
        skinCluster.paletteSrvHandle.first = SrvManager::GetInstance()->GetCPUDescriptorHandle(paletteSrvIndex);
        skinCluster.paletteSrvHandle.second = SrvManager::GetInstance()->GetGPUDescriptorHandle(paletteSrvIndex);
        SrvManager::GetInstance()->CreateSRVforStructuredBuffer(paletteSrvIndex, skinCluster.paletteResource.Get(), UINT(skeleton.joints.size()), sizeof(WellForGPU));

        // t1: InputVertex
        uint32_t inputVertexSrvIndex = SrvManager::GetInstance()->Allocate();
        skinCluster.inputVertexSrvIndex = inputVertexSrvIndex;
        skinCluster.inputVertexSrvHandle.first = SrvManager::GetInstance()->GetCPUDescriptorHandle(inputVertexSrvIndex);
        skinCluster.inputVertexSrvHandle.second = SrvManager::GetInstance()->GetGPUDescriptorHandle(inputVertexSrvIndex);
        SrvManager::GetInstance()->CreateSRVforStructuredBuffer(inputVertexSrvIndex, skinCluster.inputVertexResource.Get(), UINT(modelData.vertices.size()), sizeof(VertexData));

        // t2: Influence
        uint32_t influenceSrvIndex = SrvManager::GetInstance()->Allocate();
        skinCluster.influenceSrvIndex = influenceSrvIndex;
        skinCluster.influenceSrvHandle.first = SrvManager::GetInstance()->GetCPUDescriptorHandle(influenceSrvIndex);
        skinCluster.influenceSrvHandle.second = SrvManager::GetInstance()->GetGPUDescriptorHandle(influenceSrvIndex);
        SrvManager::GetInstance()->CreateSRVforStructuredBuffer(influenceSrvIndex, skinCluster.influenceResource.Get(), UINT(modelData.vertices.size()), sizeof(VertexInfluence));

        // u0: OutputVertex
        uint32_t outputVertexUavIndex = SrvManager::GetInstance()->Allocate();
        skinCluster.outputVertexUavIndex = outputVertexUavIndex;
        skinCluster.outputVertexUavHandle.first = SrvManager::GetInstance()->GetCPUDescriptorHandle(outputVertexUavIndex);
        skinCluster.outputVertexUavHandle.second = SrvManager::GetInstance()->GetGPUDescriptorHandle(outputVertexUavIndex);
        SrvManager::GetInstance()->CreateUAVforStructuredBuffer(outputVertexUavIndex, skinCluster.outputVertexResource.Get(), UINT(modelData.vertices.size()), sizeof(VertexData));

        // info用リソース (ConstantBuffer)
        skinCluster.infoResource = CreateBufferResource(device, sizeof(SkinningInformation));
        skinCluster.infoResource->Map(0, nullptr, reinterpret_cast<void**>(&skinCluster.mappedInfo));
        skinCluster.mappedInfo->numVertices = UINT(modelData.vertices.size());

        // outputVertex用のVBV作成
        skinCluster.outputVertexBufferView.BufferLocation = skinCluster.outputVertexResource->GetGPUVirtualAddress();
        skinCluster.outputVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
        skinCluster.outputVertexBufferView.StrideInBytes = sizeof(VertexData);

        // InverseBindPoseMatrixの保存領域作成
        skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
        for (auto& matrix : skinCluster.inverseBindPoseMatrices) {
            matrix = MakeIdentity4x4();
        }

        // ModelDataのSkinCluster情報を解析してInfluenceの中身を埋める
        for (const auto& jointWeight : modelData.skinClusterData) {
            auto it = skeleton.jointMap.find(jointWeight.first);
            if (it == skeleton.jointMap.end()) {
                continue;
            }
            
            skinCluster.inverseBindPoseMatrices[it->second] = jointWeight.second.inverseBindPoseMatrix;
            for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
                auto& currentInfluence = skinCluster.mappedInfluence[vertexWeight.vertexIndex];
                for (uint32_t index = 0; index < kNumMaxInfluence; ++index) {
                    if (currentInfluence.weights[index] == 0.0f) {
                        currentInfluence.weights[index] = vertexWeight.weight;
                        currentInfluence.jointIndices[index] = it->second;
                        break;
                    }
                }
            }
        }

        return skinCluster;
    }

    void Update(SkinCluster& skinCluster, const Skeleton& skeleton) {
        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
            assert(jointIndex < skinCluster.inverseBindPoseMatrices.size());
            skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix =
                Multiply(skinCluster.inverseBindPoseMatrices[jointIndex], skeleton.joints[jointIndex].skeletonSpaceMatrix);
            skinCluster.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
                Transpose(Inverse(skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix));
        }
    }

} // namespace AdvAnim
