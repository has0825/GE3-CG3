#include "Model.h"
#include <cassert>
#include <cstring> // memcpy用

// ★変更: 戻り値と内部生成を std::unique_ptr と std::make_unique に置き換え、newを排除
std::unique_ptr<Model> Model::CreateParticleModel(ID3D12Device* device) {
	std::unique_ptr<Model> model = std::make_unique<Model>();
	ModelData modelData;

	// 四角形の頂点定義
	modelData.vertices.push_back({ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

	modelData.vertices.push_back({ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, 1.0f, 0.0f, 1.0f },  { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } });
	modelData.vertices.push_back({ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } });

	model->Initialize(modelData, device);

	return model;
}

void Model::Initialize(ID3D12Device* device, ModelCommonData* commonData) {
	commonData_ = commonData;

	// マテリアル用のリソース作成
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	Material* materialData = nullptr;
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = 1;
	materialData->uvTransform = MakeIdentity4x4();
	materialResource_->Unmap(0, nullptr);

	transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
}

void Model::Initialize(const ModelData& modelData, ID3D12Device* device) {
	vertices_ = modelData.vertices;
	materialData_ = modelData.material;

	// 頂点バッファの作成
	vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertices_.size());
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices_.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	VertexData* vertexData = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices_.data(), sizeof(VertexData) * vertices_.size());
	vertexResource_->Unmap(0, nullptr);

	// マテリアル用のリソース作成
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	Material* material = nullptr;
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material));
	material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->enableLighting = 1;
	material->uvTransform = MakeIdentity4x4();
	materialResource_->Unmap(0, nullptr);

	transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
}

void Model::Update() {
	// 必要な更新処理があればここに記述
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

	// 3. インスタンシング用 SRV (パーティクルデータ等)
	commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandle);

	// 4. テクスチャ SRV (オブジェクトのテクスチャ等)
	commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

	commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}