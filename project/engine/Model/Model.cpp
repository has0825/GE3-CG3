#include "Model.h"
#include <cassert>
#include <cstring> // memcpy用

// ★パーティクル用の四角形モデル生成の実装
Model* Model::CreateParticleModel(ID3D12Device* device) {
	Model* model = new Model();
	ModelData modelData;

	// 四角形の頂点定義
	modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

	modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f },  { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

	modelData.material.textureFilePath = "resources/uvChecker.png";

	// 単独初期化モードで呼ぶ
	model->Initialize(modelData, device);
	return model;
}

// ============================================================
// パターン1: ModelManager経由の初期化 (エラーが出ていた箇所に対応)
// ============================================================
void Model::Initialize(ID3D12Device* device, ModelCommonData* commonData) {
	assert(commonData);
	this->commonData_ = commonData; // 共通データを保持

	// マテリアルバッファ作成 (インスタンスごとに固有)
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	Material* materialMap = nullptr;
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap));

	materialMap->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialMap->enableLighting = true;
	materialMap->uvTransform = MakeIdentity4x4();
	this->materialData = materialMap;

	// WVPバッファ作成 (インスタンスごとに固有)
	wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	wvpData_->WVP = MakeIdentity4x4();
	wvpData_->World = MakeIdentity4x4();
}

// ============================================================
// パターン2: 単独初期化 (パーティクル等)
// ============================================================
void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
	this->commonData_ = nullptr; // マネージャーは使わない

	// 頂点データをコピーして自己管理
	vertices_ = modelData.vertices;

	// 頂点バッファ作成
	vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertices_.size());
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices_.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	VertexData* vertexData = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices_.data(), sizeof(VertexData) * vertices_.size());

	// マテリアルバッファ作成
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	Material* materialMap = nullptr;
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap));

	materialMap->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialMap->enableLighting = true;
	materialMap->uvTransform = MakeIdentity4x4();
	this->materialData = materialMap;

	// WVPバッファ作成
	wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	wvpData_->WVP = MakeIdentity4x4();
	wvpData_->World = MakeIdentity4x4();
}

void Model::Update() {
	// 更新処理が必要ならここに記述
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) {
	// Manager経由か、自己管理かで頂点バッファを切り替え
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

	// Manager経由か、自己管理かで頂点バッファを切り替え
	D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
	UINT vertexCount = 0;

	if (commonData_) {
		vbView = &commonData_->vertexBufferView;
		vertexCount = (UINT)commonData_->vertices.size();
	} else {
		vbView = &vertexBufferView_;
		vertexCount = (UINT)vertices_.size();
	}

	// 1. 頂点バッファ
	commandList->IASetVertexBuffers(0, 1, vbView);

	// 2. マテリアル (CBV)
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// 3. Instancingデータ (StructuredBuffer SRV)
	commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);

	// 4. テクスチャ (Texture SRV)
	commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

	// 5. Light (CBV) - Lightingが必要なら
	commandList->SetGraphicsRootConstantBufferView(3, materialResource_->GetGPUVirtualAddress());

	// 6. 描画
	commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}