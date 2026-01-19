#include "Model.h"
#include "MathUtil.h" 
#include "DataTypes.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <Windows.h> 

// ヘルパー関数のプロトタイプ宣言
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename);

Model* Model::Create(const std::string& directoryPath, const std::string& filename, ID3D12Device* device) {
    // デバイスチェック
    if (!device) {
        MessageBoxA(nullptr, "Model::Create failed: Device is nullptr.", "Error", MB_OK | MB_ICONERROR);
        assert(false && "Device is nullptr");
        return nullptr;
    }

    Model* model = new Model();
    ModelData modelData = LoadOjFile(directoryPath, filename);

    // ★修正: 読み込みデータが空の場合は失敗とみなす
    if (modelData.vertices.empty()) {
        std::string msg = "Model Data is empty! Check FilePath: " + directoryPath + "/" + filename;
        MessageBoxA(nullptr, msg.c_str(), "File Load Error", MB_OK | MB_ICONERROR);
        delete model;
        return nullptr;
    }

    model->Initialize(modelData, device);
    return model;
}

Model* Model::CreateParticleModel(ID3D12Device* device) {
    if (!device) {
        assert(false && "Device is nullptr in CreateParticleModel");
        return nullptr;
    }

    Model* model = new Model();
    ModelData modelData;

    // 四角形 (左上, 右上, 左下, 右上, 右下, 左下)
    modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });

    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });

    modelData.material.textureFilePath = "";

    model->Initialize(modelData, device);
    return model;
}

Model* Model::CreateSphereModel(ID3D12Device* device, uint32_t subdivision) {
    if (!device) {
        assert(false && "Device is nullptr in CreateSphereModel");
        return nullptr;
    }

    Model* model = new Model();
    ModelData modelData;

    uint32_t latDiv = subdivision;
    uint32_t lonDiv = subdivision;
    const float kPi = 3.14159265359f;

    // 1. 頂点生成
    for (uint32_t lat = 0; lat <= latDiv; ++lat) {
        float latAngle = kPi * lat / latDiv;
        float y = std::cos(latAngle);
        float r = std::sin(latAngle);

        for (uint32_t lon = 0; lon <= lonDiv; ++lon) {
            float lonAngle = 2.0f * kPi * lon / lonDiv;
            float x = r * std::cos(lonAngle);
            float z = r * std::sin(lonAngle);

            float u = float(lon) / lonDiv;
            float v = float(lat) / latDiv;

            VertexData vertex;
            vertex.position = { x, y, z, 1.0f };
            vertex.texcoord = { 1.0f - u, v };
            vertex.normal = { x, y, z };
            modelData.vertices.push_back(vertex);
        }
    }

    // 2. インデックス展開
    std::vector<VertexData> expandedVertices;
    for (uint32_t lat = 0; lat < latDiv; ++lat) {
        for (uint32_t lon = 0; lon < lonDiv; ++lon) {
            uint32_t current = lat * (lonDiv + 1) + lon;
            uint32_t next = current + (lonDiv + 1);

            const VertexData& p0 = modelData.vertices[current];
            const VertexData& p1 = modelData.vertices[current + 1];
            const VertexData& p2 = modelData.vertices[next];
            const VertexData& p3 = modelData.vertices[next + 1];

            // Triangle 1
            expandedVertices.push_back(p0);
            expandedVertices.push_back(p2);
            expandedVertices.push_back(p1);
            // Triangle 2
            expandedVertices.push_back(p1);
            expandedVertices.push_back(p2);
            expandedVertices.push_back(p3);
        }
    }
    modelData.vertices = expandedVertices;
    modelData.material.textureFilePath = "";

    model->Initialize(modelData, device);
    return model;
}

void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
    assert(device && "Device passed to Initialize is null");

    modelData_ = modelData;
    transform = { {1,1,1}, {0,0,0}, {0,0,0} };

    // 頂点が空なら処理を中断
    if (modelData_.vertices.empty()) {
        MessageBoxA(nullptr, "Vertices size is 0. Initialization skipped.", "Warning", MB_OK);
        return;
    }

    size_t sizeInBytes = sizeof(VertexData) * modelData_.vertices.size();
    vertexResource_ = CreateBufferResource(device, sizeInBytes);

    if (!vertexResource_) {
        MessageBoxA(nullptr, "Failed to create vertex buffer.", "Error", MB_OK | MB_ICONERROR);
        assert(false);
        return;
    }

    VertexData* vertexData = nullptr;
    HRESULT hr = vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    if (FAILED(hr)) {
        assert(false && "Failed to map vertex buffer");
        return;
    }

    std::memcpy(vertexData, modelData_.vertices.data(), sizeInBytes);
    vertexResource_->Unmap(0, nullptr);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    materialResource_ = CreateBufferResource(device, sizeof(Material));

    if (!materialResource_) {
        assert(false && "Failed to create material buffer");
        return;
    }

    Material* material = nullptr;
    hr = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material));
    assert(SUCCEEDED(hr));

    material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    material->enableLighting = 1;
    material->uvTransform = MakeIdentity4x4();

    materialResource_->Unmap(0, nullptr);
}

void Model::Update() {
    // 必要ならここで更新処理
}

void Model::Draw(
    ID3D12GraphicsCommandList* commandList,
    UINT instanceCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle)
{
    // リソースがなければ描画しない（安全策）
    if (!vertexResource_ || !materialResource_) {
        return;
    }

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(4, instancingSrvHandle);

    commandList->DrawInstanced(UINT(modelData_.vertices.size()), instanceCount, 0, 0);
}

// --- 以下ヘルパー関数 ---

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::string msg = "Failed to open material file: " + filePath;
        OutputDebugStringA(msg.c_str());
        return materialData;
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

ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    // ★修正: ファイルが開けない場合はエラーメッセージを出す
    if (!file.is_open()) {
        std::string msg = "Failed to open OBJ file: " + filePath;
        MessageBoxA(nullptr, msg.c_str(), "Load Error", MB_OK | MB_ICONERROR);
        assert(false && "OBJ File not found");
        return modelData;
    }

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
                uint32_t elementIndices[3] = { 0, 0, 0 };
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/');
                    if (!index.empty()) {
                        elementIndices[element] = std::stoi(index);
                    }
                }
                Vector4 position = (elementIndices[0] > 0 && elementIndices[0] <= positions.size()) ? positions[elementIndices[0] - 1] : Vector4{ 0,0,0,1 };
                Vector2 texcoord = (elementIndices[1] > 0 && elementIndices[1] <= texcoords.size()) ? texcoords[elementIndices[1] - 1] : Vector2{ 0,0 };
                Vector3 normal = (elementIndices[2] > 0 && elementIndices[2] <= normals.size()) ? normals[elementIndices[2] - 1] : Vector3{ 0,0,0 };

                triangle[faceVertex] = { position, texcoord, normal };
            }
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        } else if (identifier == "mtllib") {
            std::string materialFilename;
            s >> materialFilename;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }
    return modelData;
}