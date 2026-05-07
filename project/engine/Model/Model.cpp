#include "Model.h"
#include <cassert>
#include <cstring>
#include <windows.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

void Model::ReadNodeHierarchy(aiNode* node, Node& outNode) {
    aiMatrix4x4 aiLocalMatrix = node->mTransformation;
    aiLocalMatrix.Transpose();

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            outNode.localMatrix.m[i][j] = aiLocalMatrix[i][j];
        }
    }

    outNode.name = node->mName.C_Str();
    outNode.children.resize(node->mNumChildren);
    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        ReadNodeHierarchy(node->mChildren[i], outNode.children[i]);
    }
}

std::unique_ptr<Model> Model::CreateParticleModel(ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;

    modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

    model->Initialize(modelData, device);
    return model;
}

std::unique_ptr<Model> Model::CreateRingModel(ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;

    const uint32_t kRingDivide = 32;
    const float kOuterRadius = 1.0f;
    const float kInnerRadius = 0.2f;
    const float radianPerDivide = 2.0f * 3.14159265358979323846f / float(kRingDivide);

    for (uint32_t index = 0; index < kRingDivide; ++index) {
        float s = std::sin(index * radianPerDivide);
        float c = std::cos(index * radianPerDivide);
        float sNext = std::sin((index + 1) * radianPerDivide);
        float cNext = std::cos((index + 1) * radianPerDivide);
        float u = float(index) / float(kRingDivide);
        float uNext = float(index + 1) / float(kRingDivide);

        VertexData v1 = { {-s * kOuterRadius, c * kOuterRadius, 0.0f, 1.0f}, {u, 0.0f}, {0.0f, 0.0f, -1.0f} };
        VertexData v2 = { {-sNext * kOuterRadius, cNext * kOuterRadius, 0.0f, 1.0f}, {uNext, 0.0f}, {0.0f, 0.0f, -1.0f} };
        VertexData v3 = { {-s * kInnerRadius, c * kInnerRadius, 0.0f, 1.0f}, {u, 1.0f}, {0.0f, 0.0f, -1.0f} };
        VertexData v4 = { {-sNext * kInnerRadius, cNext * kInnerRadius, 0.0f, 1.0f}, {uNext, 1.0f}, {0.0f, 0.0f, -1.0f} };

        modelData.vertices.push_back(v3);
        modelData.vertices.push_back(v1);
        modelData.vertices.push_back(v2);

        modelData.vertices.push_back(v3);
        modelData.vertices.push_back(v2);
        modelData.vertices.push_back(v4);
    }

    model->Initialize(modelData, device);
    return model;
}

std::unique_ptr<Model> Model::LoadGLTF(const std::string& filename, ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;

    Assimp::Importer importer;
    // aiProcess_PreTransformVertices を削除し、ボーン構造を維持する
    const aiScene* scene = importer.ReadFile(filename,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_LimitBoneWeights | aiProcess_ConvertToLeftHanded | aiProcess_PopulateArmatureData);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::string errorMsg = "Assimp Error: " + std::string(importer.GetErrorString()) + "\n";
        OutputDebugStringA(errorMsg.c_str());
        return CreateParticleModel(device);
    }

    if (scene->mRootNode) {
        model->ReadNodeHierarchy(scene->mRootNode, model->rootNode);
    }

    // ボーン情報の読み込み
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        
        // 頂点ごとのウェイト情報を一時保存
        struct Weight {
            uint32_t index;
            float weight;
        };
        std::vector<std::vector<Weight>> vertexWeights(mesh->mNumVertices);

        for (unsigned int b = 0; b < mesh->mNumBones; b++) {
            aiBone* aiBone = mesh->mBones[b];
            std::string boneName = aiBone->mName.C_Str();
            
            Bone bone;
            bone.name = boneName;
            bone.index = b;
            
            aiMatrix4x4 aiOffset = aiBone->mOffsetMatrix;
            aiOffset.Transpose();
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    bone.offsetMatrix.m[r][c] = aiOffset[r][c];

            model->bones_[boneName] = bone;

            for (unsigned int w = 0; w < aiBone->mNumWeights; w++) {
                vertexWeights[aiBone->mWeights[w].mVertexId].push_back({ b, aiBone->mWeights[w].mWeight });
            }
        }

        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                unsigned int index = face.mIndices[k];
                VertexData vertex{};
                vertex.position = { mesh->mVertices[index].x, mesh->mVertices[index].y, mesh->mVertices[index].z, 1.0f };

                if (mesh->HasNormals()) {
                    vertex.normal = { mesh->mNormals[index].x, mesh->mNormals[index].y, mesh->mNormals[index].z };
                }
                if (mesh->mTextureCoords[0]) {
                    vertex.texcoord = { mesh->mTextureCoords[0][index].x, mesh->mTextureCoords[0][index].y };
                }

                // ウェイト情報の書き込み
                const auto& weights = vertexWeights[index];
                for (size_t w = 0; w < weights.size() && w < 4; w++) {
                    vertex.jointIndices[w] = weights[w].index;
                    vertex.jointWeights[w] = weights[w].weight;
                }
                modelData.vertices.push_back(vertex);
            }
        }
    }

    // アニメーションの読み込み（最初の1つ）
    if (scene->mNumAnimations > 0) {
        aiAnimation* aiAnim = scene->mAnimations[0];
        model->animation_ = std::make_unique<Animation>();
        model->animation_->name = aiAnim->mName.C_Str();
        model->animation_->duration = (float)aiAnim->mDuration;
        model->animation_->ticksPerSecond = (float)aiAnim->mTicksPerSecond != 0 ? (float)aiAnim->mTicksPerSecond : 24.0f;

        for (unsigned int i = 0; i < aiAnim->mNumChannels; i++) {
            aiNodeAnim* aiChannel = aiAnim->mChannels[i];
            AnimationChannel channel;
            channel.nodeName = aiChannel->mNodeName.C_Str();

            for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; k++) {
                channel.positionKeys.push_back({ (float)aiChannel->mPositionKeys[k].mTime, 
                    { aiChannel->mPositionKeys[k].mValue.x, aiChannel->mPositionKeys[k].mValue.y, aiChannel->mPositionKeys[k].mValue.z } });
            }
            for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; k++) {
                channel.rotationKeys.push_back({ (float)aiChannel->mRotationKeys[k].mTime, 
                    { aiChannel->mRotationKeys[k].mValue.x, aiChannel->mRotationKeys[k].mValue.y, aiChannel->mRotationKeys[k].mValue.z, aiChannel->mRotationKeys[k].mValue.w } });
            }
            for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; k++) {
                channel.scaleKeys.push_back({ (float)aiChannel->mScalingKeys[k].mTime, 
                    { aiChannel->mScalingKeys[k].mValue.x, aiChannel->mScalingKeys[k].mValue.y, aiChannel->mScalingKeys[k].mValue.z } });
            }
            model->animation_->channels.push_back(channel);
        }
    }

    model->Initialize(modelData, device);
    return model;
}

void Model::Initialize(ID3D12Device* device, ModelCommonData* commonData) {
    commonData_ = commonData;

    materialResource_ = CreateBufferResource(device, sizeof(Material));
    Material* materialData = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = 1;
    materialData->uvTransform = MakeIdentity4x4();
    materialData->shininess = 10.0f;
    materialData->environmentCoefficient = environmentCoefficient;
    materialResource_->Unmap(0, nullptr);

    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
}

void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
    vertices_ = modelData.vertices;

    vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertices_.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices_.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices_.data(), sizeof(VertexData) * vertices_.size());
    vertexResource_->Unmap(0, nullptr);

    materialResource_ = CreateBufferResource(device, sizeof(Material));
    Material* materialData = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = 1;
    materialData->uvTransform = MakeIdentity4x4();
    materialData->shininess = 10.0f;
    materialData->environmentCoefficient = environmentCoefficient;
    materialResource_->Unmap(0, nullptr);

    // スキニング定数バッファの生成
    skinningResource_ = CreateBufferResource(device, sizeof(SkinningPalette));
    skinningResource_->Map(0, nullptr, reinterpret_cast<void**>(&skinningData_));
    for (int i = 0; i < kMaxBones; i++)
        skinningData_->boneMatrices[i] = MakeIdentity4x4();

    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 3.1415f, 0.0f}, {0.0f, 0.0f, 0.0f} };
}

void Model::UpdateAnimation(float deltaTime) {
    if (!animation_) return;

    animationTime_ += deltaTime * animation_->ticksPerSecond;
    animationTime_ = std::fmod(animationTime_, animation_->duration);

    // ノードのローカル行列を更新
    auto applyAnim = [&](Node& node, const auto& self) -> void {
        for (const auto& channel : animation_->channels) {
            if (channel.nodeName == node.name) {
                Vector3 translate = channel.positionKeys.empty() ? Vector3{0,0,0} : channel.positionKeys[0].value;
                Quaternion rotate = channel.rotationKeys.empty() ? Quaternion{0,0,0,1} : channel.rotationKeys[0].value;
                Vector3 scale = channel.scaleKeys.empty() ? Vector3{1,1,1} : channel.scaleKeys[0].value;

                // Position
                if (channel.positionKeys.size() > 1) {
                    for (size_t i = 0; i < channel.positionKeys.size() - 1; ++i) {
                        if (animationTime_ < channel.positionKeys[i + 1].time) {
                            float t = (animationTime_ - channel.positionKeys[i].time) / (channel.positionKeys[i + 1].time - channel.positionKeys[i].time);
                            translate = Lerp(channel.positionKeys[i].value, channel.positionKeys[i + 1].value, t);
                            break;
                        }
                    }
                    if (animationTime_ >= channel.positionKeys.back().time) translate = channel.positionKeys.back().value;
                }
                
                // Rotation
                if (channel.rotationKeys.size() > 1) {
                    for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i) {
                        if (animationTime_ < channel.rotationKeys[i + 1].time) {
                            float t = (animationTime_ - channel.rotationKeys[i].time) / (channel.rotationKeys[i + 1].time - channel.rotationKeys[i].time);
                            rotate = Slerp(channel.rotationKeys[i].value, channel.rotationKeys[i + 1].value, t);
                            break;
                        }
                    }
                    if (animationTime_ >= channel.rotationKeys.back().time) rotate = channel.rotationKeys.back().value;
                }

                // Scale
                if (channel.scaleKeys.size() > 1) {
                    for (size_t i = 0; i < channel.scaleKeys.size() - 1; ++i) {
                        if (animationTime_ < channel.scaleKeys[i + 1].time) {
                            float t = (animationTime_ - channel.scaleKeys[i].time) / (channel.scaleKeys[i + 1].time - channel.scaleKeys[i].time);
                            scale = Lerp(channel.scaleKeys[i].value, channel.scaleKeys[i + 1].value, t);
                            break;
                        }
                    }
                    if (animationTime_ >= channel.scaleKeys.back().time) scale = channel.scaleKeys.back().value;
                }

                Matrix4x4 scaleMatrix = Matrix4x4MakeScaleMatrix(scale);
                Matrix4x4 rotateMatrix = MakeRotateMatrixFromQuaternion(rotate);
                Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
                node.localMatrix = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
                
                break;
            }
        }
        for (auto& child : node.children) self(child, self);
    };
    applyAnim(rootNode, applyAnim);

    ComputeSkinningMatrices(rootNode, MakeIdentity4x4());
}

void Model::ComputeSkinningMatrices(Node& node, const Matrix4x4& parentMatrix) {
    Matrix4x4 globalMatrix = Multiply(node.localMatrix, parentMatrix);

    if (bones_.count(node.name)) {
        const auto& bone = bones_[node.name];
        skinningData_->boneMatrices[bone.index] = Multiply(bone.offsetMatrix, globalMatrix);
    }

    for (auto& child : node.children) {
        ComputeSkinningMatrices(child, globalMatrix);
    }
}

void Model::Update() {
}

void Model::SetEnvironmentCoefficient(float coefficient) {
    environmentCoefficient = coefficient;
    if (materialResource_) {
        Material* materialData = nullptr;
        materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
        materialData->environmentCoefficient = environmentCoefficient;
        materialResource_->Unmap(0, nullptr);
    }
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) {
    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    UINT vertexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        vertexCount = (UINT)commonData_->vertices.size();
    } else {
        vbView = &vertexBufferView_;
        vertexCount = (UINT)vertices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->DrawInstanced(vertexCount, 1, 0, 0);
}

void Model::Draw(
    ID3D12GraphicsCommandList* commandList,
    UINT instanceCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle) {

    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    UINT vertexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        vertexCount = (UINT)commonData_->vertices.size();
    } else {
        vbView = &vertexBufferView_;
        vertexCount = (UINT)vertices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);
    commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}

void Model::DrawModel(
    ID3D12GraphicsCommandList* commandList,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandle) {
    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    UINT vertexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        vertexCount = (UINT)commonData_->vertices.size();
    } else {
        vbView = &vertexBufferView_;
        vertexCount = (UINT)vertices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(3, environmentSrvHandle);
    
    // スキニング用バッファをセット
    if (skinningResource_) {
        commandList->SetGraphicsRootConstantBufferView(6, skinningResource_->GetGPUVirtualAddress());
    }

    commandList->DrawInstanced(vertexCount, 1, 0, 0);
}