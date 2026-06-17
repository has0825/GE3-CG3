#include "Model.h"
#include "DirectXCommon.h"
#include <cassert>
#include <cstring>
#include <windows.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "SrvManager.h"
#include "GraphicsPipeline.h"

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
    modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
    modelData.indices = { 0, 1, 2, 2, 1, 3 };

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

        uint32_t start = (uint32_t)modelData.vertices.size();
        modelData.vertices.push_back(v1);
        modelData.vertices.push_back(v2);
        modelData.vertices.push_back(v3);
        modelData.vertices.push_back(v4);

        modelData.indices.push_back(start + 2);
        modelData.indices.push_back(start + 0);
        modelData.indices.push_back(start + 1);

        modelData.indices.push_back(start + 2);
        modelData.indices.push_back(start + 1);
        modelData.indices.push_back(start + 3);
    }

    model->Initialize(modelData, device);
    return model;
}

std::unique_ptr<Model> Model::CreateCylinderModel(ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;

    const uint32_t kCylinderDivide = 32;
    const float kTopRadius = 1.0f;
    const float kBottomRadius = 1.0f;
    const float kHeight = 3.0f;
    const float radianPerDivide = 2.0f * 3.14159265358979323846f / float(kCylinderDivide);

    for (uint32_t index = 0; index < kCylinderDivide; ++index) {
        float s = std::sin(index * radianPerDivide);
        float c = std::cos(index * radianPerDivide);
        float sNext = std::sin((index + 1) * radianPerDivide);
        float cNext = std::cos((index + 1) * radianPerDivide);
        float u = float(index) / float(kCylinderDivide);
        float uNext = float(index + 1) / float(kCylinderDivide);

        VertexData v1 = { {-s * kTopRadius, kHeight, c * kTopRadius, 1.0f}, {u, 0.0f}, {-s, 0.0f, c} };
        VertexData v2 = { {-sNext * kTopRadius, kHeight, cNext * kTopRadius, 1.0f}, {uNext, 0.0f}, {-sNext, 0.0f, cNext} };
        VertexData v3 = { {-s * kBottomRadius, 0.0f, c * kBottomRadius, 1.0f}, {u, 1.0f}, {-s, 0.0f, c} };
        VertexData v4 = { {-sNext * kBottomRadius, 0.0f, cNext * kBottomRadius, 1.0f}, {uNext, 1.0f}, {-sNext, 0.0f, cNext} };

        uint32_t start = (uint32_t)modelData.vertices.size();
        modelData.vertices.push_back(v1);
        modelData.vertices.push_back(v2);
        modelData.vertices.push_back(v3);
        modelData.vertices.push_back(v4);

        modelData.indices.push_back(start + 2);
        modelData.indices.push_back(start + 1);
        modelData.indices.push_back(start + 0);

        modelData.indices.push_back(start + 2);
        modelData.indices.push_back(start + 3);
        modelData.indices.push_back(start + 1);
    }

    model->Initialize(modelData, device);
    return model;
}

std::unique_ptr<Model> Model::CreateSphereModel(ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;
    const uint32_t kSubdivision = 16;
    const float kLatStep = 3.1415926535f / float(kSubdivision);
    const float kLonStep = 2.0f * 3.1415926535f / float(kSubdivision);

    for (uint32_t lat = 0; lat <= kSubdivision; ++lat) {
        float phi = lat * kLatStep;
        for (uint32_t lon = 0; lon <= kSubdivision; ++lon) {
            float theta = lon * kLonStep;
            Vector3 p = { std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
            modelData.vertices.push_back({ {p.x, p.y, p.z, 1.0f}, {(float)lon / kSubdivision, (float)lat / kSubdivision}, p });
        }
    }
    for (uint32_t lat = 0; lat < kSubdivision; ++lat) {
        for (uint32_t lon = 0; lon < kSubdivision; ++lon) {
            uint32_t v1 = lat * (kSubdivision + 1) + lon;
            uint32_t v2 = v1 + 1;
            uint32_t v3 = (lat + 1) * (kSubdivision + 1) + lon;
            uint32_t v4 = v3 + 1;
            modelData.indices.push_back(v1); modelData.indices.push_back(v2); modelData.indices.push_back(v3);
            modelData.indices.push_back(v3); modelData.indices.push_back(v2); modelData.indices.push_back(v4);
        }
    }
    model->Initialize(modelData, device);
    return model;
}

std::unique_ptr<Model> Model::CreateBoxModel(ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;
    // 0,0,0 から 0,0,1 に向かう 0.1x0.1 のボックス
    Vector3 p[8] = {
        {-0.05f,-0.05f, 0}, {0.05f,-0.05f, 0}, {-0.05f, 0.05f, 0}, {0.05f, 0.05f, 0},
        {-0.05f,-0.05f, 1}, {0.05f,-0.05f, 1}, {-0.05f, 0.05f, 1}, {0.05f, 0.05f, 1}
    };
    uint32_t indices[] = {
        0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4,
        2,6,3, 3,6,7, 0,4,2, 4,6,2, 1,3,5, 3,7,5
    };
    for (int i = 0; i < 8; i++) {
        modelData.vertices.push_back({ {p[i].x, p[i].y, p[i].z, 1.0f}, {0,0}, {0,0,0} });
    }
    for (uint32_t i : indices) {
        modelData.indices.push_back(i);
    }
    model->Initialize(modelData, device);
    return model;
}

struct GLTFCacheData {
    ModelData modelData;
    Node rootNode;
    std::map<std::string, Bone> bones;
    std::shared_ptr<Animation> animation;
};

static std::map<std::string, GLTFCacheData> s_GLTFCache;

std::unique_ptr<Model> Model::LoadGLTF(const std::string& filename, ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();

    // キャッシュを検索
    auto it = s_GLTFCache.find(filename);
    if (it != s_GLTFCache.end()) {
        const auto& cache = it->second;
        model->rootNode = cache.rootNode;
        model->bones_ = cache.bones;
        if (cache.animation) {
            model->animation_ = std::make_unique<Animation>(*cache.animation);
        }
        model->Initialize(cache.modelData, device);
        return model;
    }

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

    // メッシュの解析
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

        // 頂点解析
        modelData.vertices.clear();
        modelData.vertices.resize(mesh->mNumVertices);
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            aiVector3D& position = mesh->mVertices[v];
            
            // 法線とUVの存在チェックとデフォルト値
            aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0.0f, 1.0f, 0.0f);
            aiVector3D texcoord = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(0.0f, 0.0f, 0.0f);
            
            VertexData& vertex = modelData.vertices[v];
            vertex = {}; // ゼロ初期化

            // 元の座標系に戻す（Assimpのフラグに任せる）
            vertex.position = { position.x, position.y, position.z, 1.0f };
            vertex.normal = { normal.x, normal.y, normal.z };
            vertex.texcoord = { texcoord.x, texcoord.y };

            // ウェイト情報の書き込み
            const auto& weights = vertexWeights[v];
            for (size_t w = 0; w < weights.size() && w < 4; w++) {
                vertex.jointIndices[w] = weights[w].index;
                vertex.jointWeights[w] = weights[w].weight;
            }
        }

        // インデックス解析
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace& face = mesh->mFaces[j];
            assert(face.mNumIndices == 3);
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                modelData.indices.push_back(face.mIndices[k]);
            }
        }
    }

    // アニメーションの読み込み（最初の1つ）
    if (scene->mNumAnimations > 0) {
        aiAnimation* aiAnim = scene->mAnimations[0];
        model->animation_ = std::make_unique<Animation>();
        model->animation_->name = aiAnim->mName.C_Str();
        float ticksPerSecond = (float)aiAnim->mTicksPerSecond != 0 ? (float)aiAnim->mTicksPerSecond : 24.0f;
        model->animation_->duration = (float)aiAnim->mDuration / ticksPerSecond;
        model->animation_->ticksPerSecond = ticksPerSecond;

        for (unsigned int i = 0; i < aiAnim->mNumChannels; i++) {
            aiNodeAnim* aiChannel = aiAnim->mChannels[i];
            AnimationChannel channel;
            channel.nodeName = aiChannel->mNodeName.C_Str();

            for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; k++) {
                channel.positionKeys.push_back({ (float)aiChannel->mPositionKeys[k].mTime / ticksPerSecond, 
                    { aiChannel->mPositionKeys[k].mValue.x, aiChannel->mPositionKeys[k].mValue.y, aiChannel->mPositionKeys[k].mValue.z } });
            }
            for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; k++) {
                channel.rotationKeys.push_back({ (float)aiChannel->mRotationKeys[k].mTime / ticksPerSecond, 
                    { aiChannel->mRotationKeys[k].mValue.x, aiChannel->mRotationKeys[k].mValue.y, aiChannel->mRotationKeys[k].mValue.z, aiChannel->mRotationKeys[k].mValue.w } });
            }
            for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; k++) {
                channel.scaleKeys.push_back({ (float)aiChannel->mScalingKeys[k].mTime / ticksPerSecond, 
                    { aiChannel->mScalingKeys[k].mValue.x, aiChannel->mScalingKeys[k].mValue.y, aiChannel->mScalingKeys[k].mValue.z } });
            }
            model->animation_->channels.push_back(channel);
        }
    }

    // 解析データをキャッシュに保存
    GLTFCacheData cache;
    cache.modelData = modelData;
    cache.rootNode = model->rootNode;
    cache.bones = model->bones_;
    if (model->animation_) {
        cache.animation = std::make_shared<Animation>(*model->animation_);
    }
    s_GLTFCache[filename] = std::move(cache);

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

// 頂点/インデックスバッファをDEFAULTヒープ（VRAM）に作成してデータをアップロードするヘルパー関数
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device* device,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer) {

    D3D12_HEAP_PROPERTIES defaultHeapProps{};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = byteSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer));
    assert(SUCCEEDED(hr));

    D3D12_HEAP_PROPERTIES uploadHeapProps{};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    assert(SUCCEEDED(hr));

    void* mappedData = nullptr;
    uploadBuffer->Map(0, nullptr, &mappedData);
    std::memcpy(mappedData, initData, byteSize);
    uploadBuffer->Unmap(0, nullptr);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12CommandQueue* commandQueue = dxCommon->GetCommandQueue();
    
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> tempAllocator;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAllocator));
    
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> tempCmdList;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAllocator.Get(), nullptr, IID_PPV_ARGS(&tempCmdList));

    tempCmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0, byteSize);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = defaultBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tempCmdList->ResourceBarrier(1, &barrier);

    tempCmdList->Close();

    ID3D12CommandList* cmdLists[] = { tempCmdList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdLists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    commandQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    return defaultBuffer;
}

void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
    vertices_ = modelData.vertices;
    indices_ = modelData.indices;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexUploadBuffer;
    vertexResource_ = CreateDefaultBuffer(device, vertices_.data(), sizeof(VertexData) * vertices_.size(), vertexUploadBuffer);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices_.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    Microsoft::WRL::ComPtr<ID3D12Resource> indexUploadBuffer;
    if (!indices_.empty()) {
        indexResource_ = CreateDefaultBuffer(device, indices_.data(), sizeof(uint32_t) * indices_.size(), indexUploadBuffer);
        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indices_.size());
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    }

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

    animationTime_ += deltaTime;
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

void Model::SetColor(const Vector4& color) {
    if (materialResource_) {
        Material* materialData = nullptr;
        materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
        materialData->color = color;
        materialResource_->Unmap(0, nullptr);
    }
}


void Model::Draw(ID3D12GraphicsCommandList* commandList) {
    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    D3D12_INDEX_BUFFER_VIEW* ibView = nullptr;
    UINT indexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        ibView = &commonData_->indexBufferView;
        indexCount = (UINT)commonData_->indices.size();
    } else {
        vbView = &vertexBufferView_;
        ibView = &indexBufferView_;
        indexCount = (UINT)indices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->IASetIndexBuffer(ibView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}

void Model::Draw(
    ID3D12GraphicsCommandList* commandList,
    UINT instanceCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle) {

    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    D3D12_INDEX_BUFFER_VIEW* ibView = nullptr;
    UINT indexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        ibView = &commonData_->indexBufferView;
        indexCount = (UINT)commonData_->indices.size();
    } else {
        vbView = &vertexBufferView_;
        ibView = &indexBufferView_;
        indexCount = (UINT)indices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->IASetIndexBuffer(ibView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);
    commandList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
}

void Model::DrawModel(
    ID3D12GraphicsCommandList* commandList,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandle) {
    D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
    D3D12_INDEX_BUFFER_VIEW* ibView = nullptr;
    UINT indexCount = 0;

    if (commonData_) {
        vbView = &commonData_->vertexBufferView;
        ibView = &commonData_->indexBufferView;
        indexCount = (UINT)commonData_->indices.size();
    } else {
        vbView = &vertexBufferView_;
        ibView = &indexBufferView_;
        indexCount = (UINT)indices_.size();
    }

    commandList->IASetVertexBuffers(0, 1, vbView);
    commandList->IASetIndexBuffer(ibView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(3, environmentSrvHandle);
    
    // スキニング用バッファをセット
    if (skinningResource_) {
        commandList->SetGraphicsRootConstantBufferView(6, skinningResource_->GetGPUVirtualAddress());
    }

    commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}

void Model::DrawSkinningModel(
    ID3D12GraphicsCommandList* commandList,
    const AdvAnim::SkinCluster& skinCluster,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandle,
    D3D12_GPU_VIRTUAL_ADDRESS transformationMatrixAddress,
    D3D12_GPU_VIRTUAL_ADDRESS directionalLightAddress,
    D3D12_GPU_VIRTUAL_ADDRESS cameraAddress) {

    // --- CSによるスキニング実行 ---
    SrvManager* srvManager = SrvManager::GetInstance();
    //srvManager->PreDraw(); // すでにセットされているはず

    // ルートシグネチャとPSOのセット（Compute用）
    GraphicsPipeline* pipeline = GraphicsPipeline::GetInstance();
    commandList->SetComputeRootSignature(pipeline->GetComputeRootSignature());
    commandList->SetPipelineState(pipeline->GetSkinningComputePipelineState());

    // [0] t0, t1, t2 (Descriptor Table)
    srvManager->SetComputeRootDescriptorTable(0, skinCluster.paletteSrvIndex);
    
    // [1] u0 (OutputVertices)
    srvManager->SetComputeRootDescriptorTable(1, skinCluster.outputVertexUavIndex);

    // [2] b0 (SkinningInformation)
    commandList->SetComputeRootConstantBufferView(2, skinCluster.infoResource->GetGPUVirtualAddress());

    // Dispatch
    commandList->Dispatch(UINT(vertices_.size() + 1023) / 1024, 1, 1);

    // バリア (UAV -> Vertex Buffer)
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = skinCluster.outputVertexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // --- 描画処理 ---
    const D3D12_INDEX_BUFFER_VIEW* ibView = nullptr;
    UINT indexCount = 0;

    if (commonData_) {
        ibView = &commonData_->indexBufferView;
        indexCount = (UINT)commonData_->indices.size();
    } else {
        ibView = &indexBufferView_;
        indexCount = (UINT)indices_.size();
    }

    // ★ここでグラフィックス用のパイプラインとルートシグネチャを再セットする
    commandList->SetGraphicsRootSignature(pipeline->GetSkinningRootSignature());
    commandList->SetPipelineState(pipeline->GetSkinningPipelineState());

    commandList->IASetVertexBuffers(0, 1, &skinCluster.outputVertexBufferView);
    commandList->IASetIndexBuffer(ibView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 各種定数バッファとテクスチャの再セット（ルートシグネチャ変更によりクリアされるため）
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixAddress);
    // [2] MatrixPalette (b0, VS) は CSで計算済みのため不要（または空のデスクリプタをセット）
    // [3] Texture
    commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandle);
    // [4] Environment Map
    commandList->SetGraphicsRootDescriptorTable(4, environmentSrvHandle);
    // [5] Directional Light
    commandList->SetGraphicsRootConstantBufferView(5, directionalLightAddress);
    // [6] Camera
    commandList->SetGraphicsRootConstantBufferView(6, cameraAddress);

    commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

    // バリアを戻す (Vertex Buffer -> UAV) 次回のDispatchのため
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &barrier);
}
