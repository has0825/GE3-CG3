#include "Model.h"
#include "ModelManager.h" // CommonDataの定義を知るために必要
#include "TextureManager.h" // テクスチャ描画時に必要

void Model::Initialize(ID3D12Device* device, ModelCommonData* commonData) {
	// 共通データを保持
	commonData_ = commonData;

	// トランスフォーム初期化
	transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// マテリアルリソース (インスタンスごとに色を変えたりできるように個別に持つ)
	materialResource_ = CreateBufferResource(device, sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = true;
	materialData_->uvTransform = MakeIdentity4x4();

	// WVPリソース (インスタンスごとに座標が違うので個別に持つ)
	wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
	wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
	wvpData_->WVP = MakeIdentity4x4();
	wvpData_->World = MakeIdentity4x4();
}

void Model::Update() {
	// 必要であればここで更新処理
}

void Model::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewProjectionMatrix) {

	// ワールド行列・WVP行列の計算更新
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	wvpData_->WVP = Multiply(worldMatrix, viewProjectionMatrix);
	wvpData_->World = worldMatrix;

	// 頂点バッファの設定 (共通データから取得)
	commandList->IASetVertexBuffers(0, 1, &commonData_->vertexBufferView);

	// マテリアル定数バッファ (このインスタンス固有)
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// WVP定数バッファ (このインスタンス固有)
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

	// テクスチャ (TextureManager経由でハンドルを取得)
	// objファイルに記述されていたテクスチャファイル名を使う
	if (!commonData_->materialData.textureFilePath.empty()) {
		// テクスチャマネージャーはパスの一部（ファイル名）だけで管理している場合があるので調整が必要だが、
		// ここではパス全体、またはロード時のルールに従って取得
		// ※LoadTexture時に "resources/player/player.png" のように保存している想定
		// パスからファイル名だけ取り出す処理が必要ならここに書くが、一旦そのまま渡す
		TextureManager* texManager = TextureManager::GetInstance();

		// もしロードされていなければロードを試みる（念のため）
		texManager->LoadTexture(commonData_->materialData.textureFilePath);

		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = texManager->GetSrvHandleGPU(commonData_->materialData.textureFilePath);
		commandList->SetGraphicsRootDescriptorTable(2, srvHandle);
	}

	// 描画
	commandList->DrawInstanced(UINT(commonData_->vertices.size()), 1, 0, 0);
}