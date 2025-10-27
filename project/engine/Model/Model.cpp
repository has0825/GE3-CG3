#include "Model.h"
#include <cassert>
#include <fstream>
#include <sstream>

// static メンバー変数の定義
std::map<std::string, std::shared_ptr<MeshData>> Model::meshCache_;
ID3D12Device* Model::device_ = nullptr;

// (LoadMaterialTemplateFile と LoadOjFile の実装は変更なし)
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
ModelData LoadOjFile(const std::string& directoryPath, const std::string& filename);


// ★ 新規: モデルデータ（メッシュ）をロードまたはキャッシュから取得
std::shared_ptr<MeshData> Model::LoadMesh(
	const std::string& directoryPath, const std::string& filename, ID3D12Device* device) {

	// device_ が未設定なら設定
	if (device_ == nullptr) {
		device_ = device;
	}

	std::string filePath = directoryPath + "/" + filename;

	// キャッシュを検索
	auto it = meshCache_.find(filePath);
	if (it != meshCache_.end()) {
		// キャッシュヒット
		return it->second;
	}

	// キャッシュミス。ロードする。
	ModelData modelData = LoadOjFile(directoryPath, filename);

	// MeshData を作成
	std::shared_ptr<MeshData> meshData = std::make_shared<MeshData>();
	meshData->vertices = modelData.vertices;
	meshData->materialInfo = modelData.material; // .mtl の情報

	// 頂点バッファ作成
	meshData->vertexResource = CreateBufferResource(device, sizeof(VertexData) * meshData->vertices.size());
	meshData->vertexBufferView.BufferLocation = meshData->vertexResource->GetGPUVirtualAddress();
	meshData->vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * meshData->vertices.size());
	meshData->vertexBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexData = nullptr;
	meshData->vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, meshData->vertices.data(), sizeof(VertexData) * meshData->vertices.size());
	meshData->vertexResource->Unmap(0, nullptr); // MapしたリソースをUnmap

	// キャッシュに保存
	meshCache_[filePath] = meshData;

	return meshData;
}


Model* Model::Create(
	const std::string& directoryPath, const std::string& filename, ID3D12Device* device) {

	// メッシュデータをロード（またはキャッシュから取得）
	std::shared_ptr<MeshData> meshData = LoadMesh(directoryPath, filename, device);

	Model* model = new Model();
	model->Initialize(device); // インスタンス固有のリソースを初期化
	model->SetMesh(meshData);  // メッシュデータをセット

	return model;
}

void Model::Initialize(ID3D12Device* device) {

	// device_ が未設定なら設定
	if (device_ == nullptr) {
		device_ = device;
	}

	// ★ 頂点バッファ関連の処理は LoadMesh に移動

	// インスタンスごとのマテリアルバッファ作成
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = true;
	materialData->uvTransform = MakeIdentity4x4();
	// (materialDataは実行中に更新される可能性があるためUnmapしない)

	// インスタンスごとのWVPバッファ作成
	wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	wvpData_->WVP = MakeIdentity4x4();
	wvpData_->World = MakeIdentity4x4();
	// (wvpData_は毎フレーム更新されるためUnmapしない)
}

void Model::Update() {
	// 何もしない
}

void Model::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewProjectionMatrix,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle) {

	// ★ メッシュデータがなければ描画しない
	if (!meshData_) {
		return;
	}

	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	wvpData_->WVP = Multiply(worldMatrix, viewProjectionMatrix);
	wvpData_->World = worldMatrix;

	// ★ メッシュデータの頂点バッファを参照
	commandList->IASetVertexBuffers(0, 1, &meshData_->vertexBufferView);

	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

	// ★ メッシュデータの頂点数を参照
	commandList->DrawInstanced(UINT(meshData_->vertices.size()), 1, 0, 0);
}


// === このファイル内でのみ使用するヘルパー関数 ===
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