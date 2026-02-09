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

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h> // glTFのPBRマテリアル用

// Debug/Release構成に合わせてライブラリ名を自動切り替え
#ifdef _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

// プロトタイプ宣言
Node ReadNode(aiNode* node);
// LoadObjFile -> LoadModelFile に変更し、Node情報も返すように引数を調整
ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename, Node* outRootNode);

Model* Model::Create(const std::string& directoryPath, const std::string& filename, ID3D12Device* device) {
    if (!device) {
        MessageBoxA(nullptr, "Model::Create failed: Device is nullptr.", "Error", MB_OK | MB_ICONERROR);
        assert(false && "Device is nullptr");
        return nullptr;
    }

    Model* model = new Model();

    // Nodeを受け取る変数を渡して読み込み
    ModelData modelData = LoadModelFile(directoryPath, filename, &model->rootNode);

    // データが空の場合は失敗とみなす
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

    // 単位行列で初期化
    model->rootNode.localMatrix = MakeIdentity4x4();

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

    // 単位行列で初期化
    model->rootNode.localMatrix = MakeIdentity4x4();

    uint32_t latDiv = subdivision;
    uint32_t lonDiv = subdivision;
    const float kPi = 3.14159265359f;

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

    std::vector<VertexData> expandedVertices;
    for (uint32_t lat = 0; lat < latDiv; ++lat) {
        for (uint32_t lon = 0; lon < lonDiv; ++lon) {
            uint32_t current = lat * (lonDiv + 1) + lon;
            uint32_t next = current + (lonDiv + 1);

            const VertexData& p0 = modelData.vertices[current];
            const VertexData& p1 = modelData.vertices[current + 1];
            const VertexData& p2 = modelData.vertices[next];
            const VertexData& p3 = modelData.vertices[next + 1];

            expandedVertices.push_back(p0);
            expandedVertices.push_back(p2);
            expandedVertices.push_back(p1);
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

    // マテリアルバッファ作成
    materialResource_ = CreateBufferResource(device, sizeof(Material));

    if (!materialResource_) {
        assert(false && "Failed to create material buffer");
        return;
    }

    // ★修正: Mapしたポインタをメンバ変数に保持し、Unmapしない
    // これにより main.cpp 側から materialData->shininess 等を変更できるようになる
    hr = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    assert(SUCCEEDED(hr));

    // ★修正: 鏡面反射を有効化
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    // 1:Lambert -> 3:Blinn-Phong (鏡面反射あり) に変更
    materialData->enableLighting = 3;
    // Shininessを設定 (値が大きいほどハイライトが鋭くなる)
    materialData->shininess = 50.0f;
    materialData->uvTransform = MakeIdentity4x4();

    // ※ここではUnmapしない
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
    if (!vertexResource_ || !materialResource_) {
        return;
    }

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // Modelが持つMaterialバッファを使用する
    // (ここでInitializeで設定した鏡面反射設定が適用される)
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(4, instancingSrvHandle);

    commandList->DrawInstanced(UINT(modelData_.vertices.size()), instanceCount, 0, 0);
}

// --- 以下ヘルパー関数 ---

// AssimpのNodeから独自のNodeへ変換
Node ReadNode(aiNode* node) {
    Node result;

    // nodeのTransformationを取得
    aiMatrix4x4 aiLocalMatrix = node->mTransformation;

    // 列ベクトル形式を行ベクトル形式に転置
    aiLocalMatrix.Transpose();

    // Assimpの行列(4x4)をコピー
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.localMatrix.m[i][j] = aiLocalMatrix[i][j];
        }
    }

    result.name = node->mName.C_Str(); // Node名を格納
    result.children.resize(node->mNumChildren); // 子供の数だけ確保

    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        // 再帰的に読んで階層構造を作っていく
        result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
    }

    return result;
}

// LoadObjFile -> LoadModelFile (glTF対応, Node対応)
ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename, Node* outRootNode) {
    ModelData modelData;
    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;

    // ファイル読み込み
    const aiScene* scene = importer.ReadFile(filePath.c_str(),
        aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate);

    // 読み込み失敗チェック
    // ★修正: エラー時に詳細メッセージを表示するように変更
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes()) {
        std::string msg = "Assimp failed to load file: " + filePath;
        msg += "\nError: " + std::string(importer.GetErrorString());
        MessageBoxA(nullptr, msg.c_str(), "LoadModelFile Error", MB_OK | MB_ICONERROR);
        assert(false && "Assimp Load Failed");
        return modelData;
    }

    // SceneのRootNodeを読んで階層構造を作り上げる
    if (outRootNode) {
        *outRootNode = ReadNode(scene->mRootNode);
    }

    // meshを解析する
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];

        // 法線がない、TexcoordがないMeshは今回は非対応
        if (!mesh->HasNormals() || !mesh->HasTextureCoords(0)) continue;

        // faceを解析する
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            aiFace& face = mesh->mFaces[faceIndex];

            // 三角形のみサポート
            if (face.mNumIndices != 3) continue;

            // vertexを解析する
            for (uint32_t element = 0; element < face.mNumIndices; ++element) {
                uint32_t vertexIndex = face.mIndices[element];

                aiVector3D& position = mesh->mVertices[vertexIndex];
                aiVector3D& normal = mesh->mNormals[vertexIndex];
                aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

                VertexData vertex;
                vertex.position = { position.x, position.y, position.z, 1.0f };
                vertex.normal = { normal.x, normal.y, normal.z };
                vertex.texcoord = { texcoord.x, texcoord.y };

                // 座標系変換 (右手系 -> 左手系)
                vertex.position.x *= -1.0f;
                vertex.normal.x *= -1.0f;

                modelData.vertices.push_back(vertex);
            }
        }
    }

    // materialを解析する
    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        aiMaterial* material = scene->mMaterials[materialIndex];

        // 変数宣言をifの外に出してスコープ問題を解決
        aiString textureFilePath;

        // Diffuseテクスチャ (OBJ)
        if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
            modelData.material.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
        }
        // BaseColorテクスチャ (glTF)
        else if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &textureFilePath) == AI_SUCCESS) {
            modelData.material.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
        }
    }

    return modelData;
}