#include "AdvancedAnimation.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>

namespace AdvAnim {

    void ReadNodeHierarchy(aiNode* node, Node& outNode) {
        outNode.name = node->mName.C_Str();
        outNode.children.resize(node->mNumChildren);
        for (uint32_t i = 0; i < node->mNumChildren; ++i) {
            ReadNodeHierarchy(node->mChildren[i], outNode.children[i]);
        }
    }

    AnimatedModel LoadModelFile(const std::string& directoryPath, const std::string& filename) {
        AnimatedModel model;
        Assimp::Importer importer;
        std::string filePath = directoryPath + "/" + filename;
        const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        if (!scene || !scene->mRootNode) {
            return model;
        }

        // メッシュの読み込み (簡易実装)
        for (uint32_t i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[i];
            for (uint32_t j = 0; j < mesh->mNumFaces; ++j) {
                aiFace& face = mesh->mFaces[j];
                for (uint32_t k = 0; k < face.mNumIndices; ++k) {
                    uint32_t index = face.mIndices[k];
                    VertexData vertex{};
                    vertex.position = { mesh->mVertices[index].x, mesh->mVertices[index].y, mesh->mVertices[index].z, 1.0f };
                    if (mesh->HasNormals()) {
                        vertex.normal = { mesh->mNormals[index].x, mesh->mNormals[index].y, mesh->mNormals[index].z };
                    } else {
                        vertex.normal = { 0.0f, 0.0f, 1.0f };
                    }
                    if (mesh->mTextureCoords[0]) {
                        vertex.texcoord = { mesh->mTextureCoords[0][index].x, mesh->mTextureCoords[0][index].y };
                    } else {
                        vertex.texcoord = { 0.0f, 0.0f };
                    }
                    model.modelData.vertices.push_back(vertex);
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
        const aiScene* scene = importer.ReadFile(filePath.c_str(), 0);
        
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
                keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z }; // 右手->左手
                nodeAnimation.translate.keyframes.push_back(keyframe);
            }

            for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
                aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
                KeyframeQuaternion keyframe;
                keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
                // 右手->左手変換 (yとzを反転)
                keyframe.value = { keyAssimp.mValue.x, -keyAssimp.mValue.y, -keyAssimp.mValue.z, keyAssimp.mValue.w };
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
