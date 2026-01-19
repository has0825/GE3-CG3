#include "ModelManager.h"
#include "D3D12Util.h"
#include <fstream>
#include <sstream>
#include <cassert>
#include <vector>
#include <cstring> // memcpy用
#include <Windows.h> // MessageBox用

ModelManager* ModelManager::instance = nullptr;

// ==========================================
// ヘルパー関数
// ==========================================

// マテリアルファイル (.mtl) の読み込み
static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;

    // パス結合
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    // ★追加: ファイルが開けなかった場合のエラーチェック
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
            // テクスチャは同じディレクトリにあると仮定
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }
    return materialData;
}

// OBJファイルの読み込み
static ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    // パス結合
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);

    // ★追加: ファイルが開けなかった場合のエラーチェック
    // これにより、フォルダ名間違い(Resource vs resources)などが即座に分かります
    if (!file.is_open()) {
        std::string message = "Failed to open OBJ file: " + filePath;
        MessageBoxA(nullptr, message.c_str(), "ModelManager Error", MB_OK | MB_ICONERROR);
        assert(false);
    }

    while (std::getline(file, line)) {
        std::string identifiler;
        std::istringstream s(line);
        s >> identifiler;
        if (identifiler == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            // X軸反転（右手座標系→左手座標系変換のため）
            position.x *= -1.0f;
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifiler == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            // DirectX用にV座標反転
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifiler == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            // X軸反転
            normal.x *= -1.0f;
            normals.push_back(normal);
        } else if (identifiler == "f") {
            VertexData triangle[3];
            // 三角形のみ対応（四角形ポリゴンなどは事前に分割が必要）
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;
                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3];
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/'); // 区切り文字 /
                    if (!index.empty()) {
                        elementIndices[element] = std::stoi(index);
                    } else {
                        elementIndices[element] = 0;
                    }
                }
                // OBJのインデックスは1始まりなので-1する
                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = { 0.0f, 0.0f };
                if (elementIndices[1] != 0) {
                    texcoord = texcoords[elementIndices[1] - 1];
                }
                Vector3 normal = { 0.0f, 0.0f, 0.0f };
                if (elementIndices[2] != 0) {
                    normal = normals[elementIndices[2] - 1];
                }
                triangle[faceVertex] = { position, texcoord, normal };
            }
            // 頂点の巻き順を逆にする（左手座標系対応）
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        } else if (identifiler == "mtllib") {
            std::string materialFilename;
            s >> materialFilename;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }
    return modelData;
}


// ==========================================
// ModelManager 実装
// ==========================================

ModelManager* ModelManager::GetInstance() {
    if (instance == nullptr) {
        instance = new ModelManager();
    }
    return instance;
}

void ModelManager::Initialize(ID3D12Device* device) {
    device_ = device;
    // マップをクリア
    modelDatas_.clear();
}

void ModelManager::LoadModel(const std::string& directoryPath, const std::string& filename) {
    // キーを作成 (フォルダ名 + "/" + ファイル名)
    std::string key = directoryPath + "/" + filename;

    // 既に読み込み済みなら何もしない（キャッシュ機構）
    if (modelDatas_.find(key) != modelDatas_.end()) {
        return;
    }

    // --- ファイル読み込みとデータ生成 ---
    // ヒープ領域に共通データを確保
    ModelCommonData* commonData = new ModelCommonData();

    // OBJファイル読み込み
    ModelData rawData = LoadOjFile(directoryPath, filename);
    commonData->vertices = rawData.vertices;
    commonData->materialData = rawData.material;

    // 頂点リソース作成
    // D3D12UtilのCreateBufferResourceを使用
    commonData->vertexResource = CreateBufferResource(device_, sizeof(VertexData) * commonData->vertices.size());

    // 頂点バッファビュー作成
    commonData->vertexBufferView.BufferLocation = commonData->vertexResource->GetGPUVirtualAddress();
    commonData->vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * commonData->vertices.size());
    commonData->vertexBufferView.StrideInBytes = sizeof(VertexData);

    // 頂点データをマッピングしてGPUへコピー
    VertexData* vertexData = nullptr;
    commonData->vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, commonData->vertices.data(), sizeof(VertexData) * commonData->vertices.size());

    // 静的メッシュならUnmapしてもOKだが、頻繁に書き換えないならMapしっぱなしでも動作上の問題はない
    // ここではコピーが終わったらUnmapする
    commonData->vertexResource->Unmap(0, nullptr);

    // マップに保存
    modelDatas_[key] = commonData;
}

Model* ModelManager::CreateModel(const std::string& directoryPath, const std::string& filename) {
    std::string key = directoryPath + "/" + filename;

    // まだ読み込まれていなければロードする
    LoadModel(directoryPath, filename);

    // データを取得
    ModelCommonData* commonData = modelDatas_[key];

    // 新しいModelインスタンスを作成し、共通データを渡して初期化
    Model* newModel = new Model();

    // Manager管理下のモデルとして初期化
    newModel->Initialize(device_, commonData);

    return newModel;
}