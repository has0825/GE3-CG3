#include "ModelManager.h"
#include "D3D12Util.h"
#include <fstream>
#include <sstream>
#include <cassert>
#include <vector>
#include <cstring>
#include <Windows.h>

// ヘルパー関数: マテリアル読み込み
static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::string message = "Failed to open MTL file: " + filePath;
        MessageBoxA(nullptr, message.c_str(), "ModelManager Error", MB_OK | MB_ICONERROR);
        assert(false);
    }

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;
        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }
    return materialData;
}

// ヘルパー関数: OBJ読み込み
static ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::string message = "Failed to open OBJ file: " + filePath;
        MessageBoxA(nullptr, message.c_str(), "ModelManager Error", MB_OK | MB_ICONERROR);
        assert(false);
    }

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;
        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f;
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifier == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f;
            normals.push_back(normal);
        } else if (identifier == "f") {
            VertexData triangle[3];
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;
                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3];
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/');
                    elementIndices[element] = !index.empty() ? std::stoi(index) : 0;
                }
                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = (elementIndices[1] != 0) ? texcoords[elementIndices[1] - 1] : Vector2{ 0,0 };
                Vector3 normal = (elementIndices[2] != 0) ? normals[elementIndices[2] - 1] : Vector3{ 0,0,0 };
                triangle[faceVertex] = { position, texcoord, normal };
            }
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
            modelData.indices.push_back((uint32_t)modelData.indices.size());
            modelData.indices.push_back((uint32_t)modelData.indices.size());
            modelData.indices.push_back((uint32_t)modelData.indices.size());
        } else if (identifier == "mtllib") {
            std::string materialFilename;
            s >> materialFilename;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }
    return modelData;
}

// ModelManager 実装
ModelManager* ModelManager::GetInstance() {
    static ModelManager instance;
    return &instance;
}

void ModelManager::Initialize(ID3D12Device* device) {
    device_ = device;
    modelDatas_.clear();
}

void ModelManager::LoadModel(const std::string& directoryPath, const std::string& filename) {
    std::string key = directoryPath + "/" + filename;
    if (modelDatas_.find(key) != modelDatas_.end()) return;

    // 共通データを shared_ptr で生成 (newの排除)
    auto commonData = std::make_shared<ModelCommonData>();

    ModelData rawData = LoadOjFile(directoryPath, filename);
    commonData->vertices = rawData.vertices;
    commonData->indices = rawData.indices;
    commonData->materialData = rawData.material;

    commonData->vertexResource = CreateBufferResource(device_, sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexBufferView.BufferLocation = commonData->vertexResource->GetGPUVirtualAddress();
    commonData->vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexBufferView.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    commonData->vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, commonData->vertices.data(), sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexResource->Unmap(0, nullptr);

    if (!commonData->indices.empty()) {
        commonData->indexResource = CreateBufferResource(device_, sizeof(uint32_t) * commonData->indices.size());
        commonData->indexBufferView.BufferLocation = commonData->indexResource->GetGPUVirtualAddress();
        commonData->indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * commonData->indices.size());
        commonData->indexBufferView.Format = DXGI_FORMAT_R32_UINT;

        uint32_t* indexData = nullptr;
        commonData->indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        std::memcpy(indexData, commonData->indices.data(), sizeof(uint32_t) * commonData->indices.size());
        commonData->indexResource->Unmap(0, nullptr);
    }

    modelDatas_[key] = commonData;
}

std::unique_ptr<Model> ModelManager::CreateModel(const std::string& directoryPath, const std::string& filename) {
    std::string key = directoryPath + "/" + filename;
    LoadModel(directoryPath, filename);

    std::shared_ptr<ModelCommonData> commonData = modelDatas_[key];

    // Model自体を unique_ptr で生成 (newの排除)
    auto newModel = std::make_unique<Model>();
    newModel->Initialize(device_, commonData.get());

    return newModel;
}