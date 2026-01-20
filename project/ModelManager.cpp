#include "ModelManager.h"
#include "D3D12Util.h"
#include <fstream>
#include <sstream>
#include <cassert>
#include <vector>
#include <cstring>
#include <Windows.h>

// マテリアルファイル (.mtl) の読み込み
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
    if (modelDatas_.contains(key)) {
        return;
    }

    // shared_ptr で新しくデータを生成
    std::shared_ptr<ModelCommonData> commonData = std::make_shared<ModelCommonData>();

    // OBJ読み込み
    ModelData rawData;
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string mtlFilename;

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
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
            normals.push_back(normal);
        } else if (identifier == "f") {
            VertexData triangle[3];
            for (int i = 0; i < 3; i++) {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                std::string indexStr;
                int indices[3] = { 0, 0, 0 };
                int j = 0;

                while (std::getline(v, indexStr, '/')) {
                    if (!indexStr.empty()) {
                        indices[j] = std::stoi(indexStr);
                    }
                    j++;
                }
                triangle[i].position = positions[indices[0] - 1];
                if (indices[1] != 0) triangle[i].texcoord = texcoords[indices[1] - 1];
                if (indices[2] != 0) triangle[i].normal = normals[indices[2] - 1];
            }
            rawData.vertices.push_back(triangle[0]);
            rawData.vertices.push_back(triangle[1]);
            rawData.vertices.push_back(triangle[2]);
        } else if (identifier == "mtllib") {
            s >> mtlFilename;
        }
    }

    if (!mtlFilename.empty()) {
        rawData.material = LoadMaterialTemplateFile(directoryPath, mtlFilename);
    }

    commonData->vertices = rawData.vertices;
    commonData->materialData = rawData.material;

    // リソース作成
    commonData->vertexResource = CreateBufferResource(device_, sizeof(VertexData) * commonData->vertices.size());

    commonData->vertexBufferView.BufferLocation = commonData->vertexResource->GetGPUVirtualAddress();
    commonData->vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexBufferView.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    commonData->vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, commonData->vertices.data(), sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexResource->Unmap(0, nullptr);

    // マップに保存
    modelDatas_[key] = commonData;
}

std::unique_ptr<Model> ModelManager::CreateModel(const std::string& directoryPath, const std::string& filename) {
    std::string key = directoryPath + "/" + filename;

    // 読み込み済みでなければ読み込む
    LoadModel(directoryPath, filename);

    // Modelを生成し、共通データの shared_ptr を渡す
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(device_, modelDatas_[key]); // shared_ptrを渡す

    return model;
}