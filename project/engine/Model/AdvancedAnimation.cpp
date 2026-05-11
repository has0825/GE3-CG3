#include "AdvancedAnimation.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>

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
            
            // 頂点ごとのウェイトを保持する一時配列
            struct Weight {
                int index;
                float weight;
            };
            std::vector<std::vector<Weight>> vertexWeights(mesh->mNumVertices);

            // 1. ボーン情報の読み込み
            for (unsigned int b = 0; b < mesh->mNumBones; b++) {
                aiBone* aiBone = mesh->mBones[b];
                std::string boneName = aiBone->mName.C_Str();

                // モデル全体で一意なボーンとして管理
                if (model.bones.find(boneName) == model.bones.end()) {
                    Bone bone;
                    bone.name = boneName;
                    bone.index = (uint32_t)model.bones.size();

                    aiMatrix4x4 aiOffset = aiBone->mOffsetMatrix;
                    aiOffset.Transpose();
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++)
                            bone.offsetMatrix.m[r][c] = aiOffset[r][c];

                    model.bones[boneName] = bone;
                }

                uint32_t globalBoneIndex = model.bones[boneName].index;

                for (unsigned int w = 0; w < aiBone->mNumWeights; w++) {
                    Weight weightInfo;
                    weightInfo.index = (int)globalBoneIndex;
                    weightInfo.weight = aiBone->mWeights[w].mWeight;
                    vertexWeights[aiBone->mWeights[w].mVertexId].push_back(weightInfo);
                }
            }

            // 2. 頂点解析
            uint32_t vertexStart = (uint32_t)model.modelData.vertices.size();
            for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {
                VertexData vertex{};
                for (int i = 0; i < 4; i++) {
                    vertex.jointIndices[i] = 0;
                    vertex.jointWeights[i] = 0.0f;
                }

                // 座標変換は Assimp に任せるため、そのまま読み込む
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

                // ウェイト情報の書き込み
                const auto& weights = vertexWeights[v];
                for (size_t w = 0; w < weights.size() && w < 4; w++) {
                    vertex.jointIndices[w] = weights[w].index;
                    vertex.jointWeights[w] = weights[w].weight;
                }

                model.modelData.vertices.push_back(vertex);
            }

            // 3. インデックス解析
            for (uint32_t j = 0; j < mesh->mNumFaces; ++j) {
                aiFace& face = mesh->mFaces[j];
                assert(face.mNumIndices == 3);
                for (uint32_t k = 0; k < face.mNumIndices; ++k) {
                    model.modelData.indices.push_back(vertexStart + face.mIndices[k]);
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

} // namespace AdvAnim
