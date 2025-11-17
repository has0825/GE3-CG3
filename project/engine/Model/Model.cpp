#include "Model.h"
#include "ModelManager.h"
#include "TextureManager.h"

void Model::Initialize(ID3D12Device* device, ModelCommonData* commonData) {
	// 共通データを保持
	commonData_ = commonData;

	// トランスフォーム初期化
	transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// マテリアルリソース
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = true; // ライト有効
	materialData_->uvTransform = MakeIdentity4x4();

	// WVPリソース
	wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	wvpData_->WVP = MakeIdentity4x4();
	wvpData_->World = MakeIdentity4x4();
}

void Model::Update() {
}

void Model::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewProjectionMatrix) {

	// 行列更新
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	wvpData_->WVP = Multiply(worldMatrix, viewProjectionMatrix);
	wvpData_->World = worldMatrix;

	// 頂点バッファ
	commandList->IASetVertexBuffers(0, 1, &commonData_->vertexBufferView);

	// 定数バッファ
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

	// ★修正: テクスチャの適用ロジック
	TextureManager* texManager = TextureManager::GetInstance();
	std::string textureToUse = "";

	// 1. SetTextureで指定されたものがあればそれを使う
	if (!textureOverride_.empty()) {
		textureToUse = textureOverride_;
	}
	// 2. なければ、モデルデータ(.mtl)に入っていたものを使う
	else if (!commonData_->materialData.textureFilePath.empty()) {
		textureToUse = commonData_->materialData.textureFilePath;
	}

	// テクスチャが特定できたら描画に反映
	if (!textureToUse.empty()) {
		// まだロードされてなければロードを試みる
		texManager->LoadTexture(textureToUse);

		// ハンドル取得してセット
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = texManager->GetSrvHandleGPU(textureToUse);
		commandList->SetGraphicsRootDescriptorTable(2, srvHandle);
	}

	// 描画
	commandList->DrawInstanced(UINT(commonData_->vertices.size()), 1, 0, 0);
}