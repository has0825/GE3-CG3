#include "Model.h"
#include <cassert>
#include <cstring>
#include <windows.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

Node ReadNode(aiNode* node) {
    Node result;
    aiMatrix4x4 aiLocalMatrix = node->mTransformation;
    aiLocalMatrix.Transpose();

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.localMatrix.m[i][j] = aiLocalMatrix[i][j];
        }
    }

    result.name = node->mName.C_Str();
    result.children.resize(node->mNumChildren);
    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
    }
    return result;
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

std::unique_ptr<Model> Model::LoadGLTF(const std::string& filename, ID3D12Device* device) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    ModelData modelData;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filename,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::string errorMsg = "Assimp Error: " + std::string(importer.GetErrorString()) + "\n";
        OutputDebugStringA(errorMsg.c_str());
        return CreateParticleModel(device);
    }

    if (scene->mRootNode) {
        model->rootNode = ReadNode(scene->mRootNode);
    }

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
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
                } else {
                    vertex.texcoord = { 0.0f, 0.0f };
                }
                modelData.vertices.push_back(vertex);
            }
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

    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
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

    commandList->DrawInstanced(vertexCount, 1, 0, 0);
}