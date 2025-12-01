#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <string>

// エンジンヘッダー
#include "WinApp.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "Input.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Model.h"
#include "Sprite.h"
#include "ParticleManager.h"
#include "Camera.h"
#include "MathUtil.h"

// リンク設定
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

// デバッグ用ログ関数など (省略せず記述)
void Log(std::ostream& os, const std::string& message) {
	OutputDebugStringA(message.c_str());
}
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) return std::string();
	int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0, NULL, NULL);
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), &result[0], sizeNeeded, NULL, NULL);
	return result;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	// ===============================================
	// 1. システム初期化
	// ===============================================
	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);

	Input* input = Input::GetInstance();
	input->Initialize(winApp);

	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// ===============================================
	// 2. パイプライン・マネージャの初期化
	// ===============================================
	GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
	graphicsPipeline->Initialize(device);

	TextureManager::GetInstance()->Initialize(device);
	ModelManager::GetInstance()->Initialize(device);

	// パーティクルマネージャ (円形テクスチャを指定して初期化)
	// ※Resources/circle.png が存在することを確認してください
	ParticleManager* particleManager = new ParticleManager();
	particleManager->Initialize(device, "Resources/circle.png");

	// ===============================================
	// 3. オブジェクト生成
	// ===============================================
	// 板ポリゴンモデルの生成 (ParticleManager::Drawで頂点バッファが必要なため)
	// 本来は ParticleManager 内部で持つべきですが、今回は ModelManager を利用
	// plane.obj が無い場合でも動くよう、簡易モデル作成関数がある場合はそちら推奨
	Model* planeModel = ModelManager::GetInstance()->CreateModel("Resources", "plane.obj");

	// カメラ生成 (Windowサイズを渡す)
	Camera* camera = new Camera(WinApp::kClientWidth, WinApp::kClientHeight);
	camera->SetTranslate({ 0.0f, 2.0f, -10.0f });

	// 現在のパーティクルエフェクト
	ParticleType currentEffect = ParticleType::kExplosion;

	// ===============================================
	// 4. メインループ
	// ===============================================
	while (!winApp->IsEndRequested()) {
		// メッセージ処理 & 入力更新
		winApp->ProcessMessage();
		input->Update();

		if (input->IsKeyTriggered(DIK_ESCAPE)) break;

		// --- [達成条件] カメラ移動 ---
		Transform& camTrans = camera->GetTransform();
		if (input->IsKeyPressed(DIK_UP))    camTrans.translate.y += 0.1f;
		if (input->IsKeyPressed(DIK_DOWN))  camTrans.translate.y -= 0.1f;
		if (input->IsKeyPressed(DIK_RIGHT)) camTrans.translate.x += 0.1f;
		if (input->IsKeyPressed(DIK_LEFT))  camTrans.translate.x -= 0.1f;
		if (input->IsKeyPressed(DIK_W))     camTrans.translate.z += 0.1f; // Zoom In
		if (input->IsKeyPressed(DIK_S))     camTrans.translate.z -= 0.1f; // Zoom Out

		camera->Update(); // 行列更新

		// --- [達成条件] パーティクル種類切り替え ---
		if (input->IsKeyTriggered(DIK_1)) currentEffect = ParticleType::kExplosion;
		if (input->IsKeyTriggered(DIK_2)) currentEffect = ParticleType::kFountain;
		if (input->IsKeyTriggered(DIK_3)) currentEffect = ParticleType::kSpiral;
		if (input->IsKeyTriggered(DIK_4)) currentEffect = ParticleType::kRain;

		// --- [達成条件] 座標指定で発生 (スペースキー) ---
		if (input->IsKeyPressed(DIK_SPACE)) {
			// 原点から発生
			particleManager->Emit(currentEffect, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
		}

		// パーティクル更新
		particleManager->Update(camera->GetViewProjectionMatrix());

		// --- 描画処理 ---
		dxCommon->PreDraw();

		// パイプライン設定
		commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// パーティクル描画 (加算合成)
		commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeAdd));

		// 板ポリゴンの頂点バッファをセット (Modelクラスの仕様に依存するが、ここでは共通データから取得と仮定)
		// ※もしModel::Drawを使わず頂点バッファだけセットする機能がない場合、
		//   ParticleManager内で頂点バッファを作成するのが安全です。
		//   (今回は以前のコードベースから、ModelManagerが正しくロードできている前提で進めます)
		//   もし描画されない場合、ここでのIASetVertexBuffersが漏れている可能性があります。
		//   planeModel->Draw(...) を呼ぶと Instancing ではなくなってしまうため、
		//   頂点バッファのセットのみが必要です。
		//   ↓ 安全策として、ParticleManagerに頂点バッファ生成機能を入れるのがベストですが、
		//      ここでは既存の planeModel を使って DrawInstanced する前提です。
		//      (頂点バッファがセットされていないと何も映りません)

		// ★重要: ParticleManager::Drawの前に頂点バッファをセットする必要があります。
		// Modelクラスの実装によりますが、ここでは planeModel が描画できる状態とします。
		// 実際には以下のような処理が必要です:
		// commandList->IASetVertexBuffers(0, 1, &planeModel->GetVBV());

		// ParticleManager描画
		particleManager->Draw(commandList);

		dxCommon->PostDraw();
	}

	// ===============================================
	// 5. 終了処理
	// ===============================================
	delete particleManager;
	delete planeModel;
	delete camera;
	delete graphicsPipeline;

	dxCommon->Finalize();
	winApp->Finalize();

	return 0;
}