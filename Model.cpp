#include "Model.h"
#include "MathUtil.h"
#include "DataTypes.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstring> // memcpy用

// ヘルパー関数のプロトタイプ宣言
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename);

Model* Model::Create(const std::string& directoryPath, const std::string& filename, ID3D12Device* device) {
    Model* model = new Model();
    ModelData modelData = LoadOjFile(directoryPath, filename);
    model->Initialize(modelData, device);
    return model;
}

// パーティクル用の四角形モデル生成
Model* Model::CreateParticleModel(ID3D12Device* device) {
    Model* model = new Model();
    ModelData modelData;

    // 四角形の頂点定義 (三角形2つで構成)
    // 左上、右上、左下
    modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });

    // 左下、右上、右下
    modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } });
    modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f },  { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } });

    // 仮のテクスチャパス (main.cppで別途ロードするのでダミーでも可)
    modelData.material.textureFilePath = "resources/uvChecker.png";

    model->Initialize(modelData, device);
    return model;
}

// 内部初期化用
void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
    vertices_ = modelData.vertices;

    // 頂点バッファ作成
    vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertices_.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices_.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // データ転送
    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices_.data(), sizeof(VertexData) * vertices_.size());

    // マテリアルバッファ作成
    materialResource_ = CreateBufferResource(device, sizeof(Material));
    Material* materialMap = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap));

    // デフォルト値設定
    materialMap->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialMap->enableLighting = true;
    materialMap->uvTransform = MakeIdentity4x4();
    // メンバ変数へのポインタ保持はここでコピーしておく
    // (ConstantBufferはMapしっぱなしで使う想定)
    this->materialData = materialMap;

    // WVPバッファ作成 (Instance描画では使わないが、Initializeの共通処理として残す)
    wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = MakeIdentity4x4();
    wvpData_->World = MakeIdentity4x4();
}

void Model::Update() {
    // 通常のWorldTransform更新処理があればここに記述
}

// インスタンシング描画用のDraw関数
void Model::Draw(
    ID3D12GraphicsCommandList* commandList,
    UINT instanceCount,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandle) {

    // 1. 頂点バッファをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // 2. RootParameter[0]: Material (CBV)
    // マテリアルの設定 (色やUV変換)
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    // 3. RootParameter[1]: TransformationMatrix (StructuredBuffer SRV DescriptorTable)
    // Instancing用の座標データ
    commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);

    // 4. RootParameter[2]: Texture (SRV DescriptorTable)
    // テクスチャ
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

    // 5. RootParameter[3]: Light (CBV)
    // Particle用シェーダーでLightが必要ならセット、不要ならダミーでもOK
    // ここでは念のためMaterialのアドレスを入れているが、Lightingが無効なら影響しない
    commandList->SetGraphicsRootConstantBufferView(3, materialResource_->GetGPUVirtualAddress());

    // 6. インスタンシング描画実行
    // 頂点数, インスタンス数, ...
    commandList->DrawInstanced(UINT(vertices_.size()), instanceCount, 0, 0);
}

// --- ヘルパー関数実装 ---

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
    MaterialData materialData;
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());
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

ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename)
{
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());
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
                    elementIndices[element] = std::stoi(index);
                }
                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = texcoords[elementIndices[1] - 1];
                Vector3 normal = normals[elementIndices[2] - 1];
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