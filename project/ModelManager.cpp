#include "ModelManager.h"
#include "D3D12Util.h"
#include <fstream>
#include <sstream>
#include <cassert>

ModelManager* ModelManager::instance = nullptr;

// ==========================================
// ヘルパー関数
// ==========================================
static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
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

static ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;
	std::string line;
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());
	while (std::getline(file, line)) {
		std::string identifiler;
		std::istringstream s(line);
		s >> identifiler;
		if (identifiler == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.x *= -1.0f;
			position.w = 1.0f;
			positions.push_back(position);
		} else if (identifiler == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if (identifiler == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1.0f;
			normals.push_back(normal);
		} else if (identifiler == "f") {
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
	modelDatas_.clear();
}

void ModelManager::LoadModel(const std::string& directoryPath, const std::string& filename) {
	// キーを作成 (フォルダ名 + ファイル名)
	std::string key = directoryPath + "/" + filename;

	// 既に読み込み済みなら何もしない
	if (modelDatas_.find(key) != modelDatas_.end()) {
		return;
	}

	// --- ファイル読み込みとデータ生成 ---
	ModelCommonData* commonData = new ModelCommonData();

	// OBJファイル読み込み
	ModelData rawData = LoadOjFile(directoryPath, filename);
	commonData->vertices = rawData.vertices;
	commonData->materialData = rawData.material;

	// 頂点リソース作成
	commonData->vertexResource = CreateBufferResource(device_, sizeof(VertexData) * commonData->vertices.size());

	// 頂点バッファビュー作成
	commonData->vertexBufferView.BufferLocation = commonData->vertexResource->GetGPUVirtualAddress();
	commonData->vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * commonData->vertices.size());
	commonData->vertexBufferView.StrideInBytes = sizeof(VertexData);

	// 頂点データをマッピングしてコピー
	VertexData* vertexData = nullptr;
	commonData->vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, commonData->vertices.data(), sizeof(VertexData) * commonData->vertices.size());
	// Mapしっぱなしでも良いが、静的メッシュならUnmapしてもOK（D3D12Utilの実装によるが基本的には問題ない）
	// ここでは安全のためMapしっぱなしにしない実装にしておく
	// vertexResource->Unmap(0, nullptr); 

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

	// ★ここがエラーの原因だった場所：Model.cpp/.h を修正したので通るようになります
	newModel->Initialize(device_, commonData);

	return newModel;
}