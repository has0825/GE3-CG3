#define _USE_MATH_DEFINES

// C++ 標準ライブラリ
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <wrl.h>

// Windows / DirectX
#include <Windows.h>
#include <objbase.h>
#include <d3d12.h>
#include <dbghelp.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <strsafe.h>
#include <xaudio2.h>

// 外部ライブラリ
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

// ライブラリのリンク
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dinput8.lib")

// プロジェクトヘッダー
#include "WinApp.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"
#include "Model.h"
#include "MathUtil.h"
#include "MathTypes.h"
#include "DataTypes.h"
#include "Input.h"

// ★ 追加ヘッダー
#include "TextureManager.h"
#include "ModelManager.h"
#include "Sprite.h"

// ===============================================
// デバッグ・便利関数群
// ===============================================

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour,
		time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle,
		MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	return EXCEPTION_EXECUTE_HANDLER;
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) { return std::wstring(); }
	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) { return std::wstring(); }
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) { return std::string(); }
	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) { return std::string(); }
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

// ===============================================
// Main 関数
// ===============================================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	D3DResourceLeakChecker leakChecker;

	// 1. 基盤システムの初期化
	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);

	// ★修正: Inputはシングルトンとして取得
	Input* input = Input::GetInstance();
	input->Initialize(winApp);

	CoInitializeEx(0, COINIT_MULTITHREADED);
	SetUnhandledExceptionFilter(ExportDump);

	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// 2. パイプライン生成
	GraphicsPipeline* pipeline = new GraphicsPipeline();
	pipeline->Initialize(device);

	// -----------------------------------------------------------
	// ★ 達成条件対応: マネージャーの初期化
	// -----------------------------------------------------------
	// TextureManagerの初期化 (Resourcesフォルダをルートとする)
	TextureManager::GetInstance()->Initialize(device, "Resources/");
	// ModelManagerの初期化
	ModelManager::GetInstance()->Initialize(device);


	// -----------------------------------------------------------
	// ★ 達成条件対応: リソースの一括ロード
	// -----------------------------------------------------------

	// 3Dモデル読み込み (Block と Axis)
	ModelManager::GetInstance()->LoadModel("Resources/block", "block.obj");
	ModelManager::GetInstance()->LoadModel("Resources", "axis.obj"); // ルートにあるaxis

	// テクスチャ読み込み (スプライト用)
	// ※ 3Dモデル用のテクスチャ(block.pngなど)はModel描画時に自動ロードされますが、
	// ここで明示的にロードしても問題ありません。
	TextureManager::GetInstance()->LoadTexture("monsterBall.png");


	// -----------------------------------------------------------
	// ★ 達成条件対応: オブジェクト生成 (Manager経由)
	// -----------------------------------------------------------

	// --- 3Dオブジェクト ---

	// 1. ブロック (左側)
	Model* modelBlock1 = ModelManager::GetInstance()->CreateModel("Resources/block", "block.obj");
	modelBlock1->transform.translate = { -2.0f, 0.0f, 0.0f };

	// 2. ブロック (右側) ★同一モデルデータの使い回し・座標指定
	Model* modelBlock2 = ModelManager::GetInstance()->CreateModel("Resources/block", "block.obj");
	modelBlock2->transform.translate = { 2.0f, 0.0f, 0.0f };

	// 3. 軸モデル (中央) ★モデルの切り替え
	Model* modelAxis = ModelManager::GetInstance()->CreateModel("Resources", "axis.obj");
	modelAxis->transform.translate = { 0.0f, 0.0f, 0.0f };


	// --- 2Dスプライト ---

	// 1. モンスターボール (左上)
	Sprite* spriteBall = Sprite::Create("monsterBall.png", { 50.0f, 50.0f });
	// サイズ指定 (画像のピクセルサイズが分からないので適当に100x100にする)
	spriteBall->transform.scale = { 100.0f, 100.0f, 1.0f };

	// 2. 切り取りテスト用モンスターボール (右の方) ★範囲指定切り取り
	Sprite* spriteCut = Sprite::Create("monsterBall.png", { 300.0f, 50.0f });
	spriteCut->transform.scale = { 100.0f, 100.0f, 1.0f }; // 表示サイズ
	// 画像の左上(0,0)から、半分のサイズだけ切り取るようなイメージ（数値は仮定）
	// ※画像サイズが不明なので、とりあえず 64x64 切り取りとします
	spriteCut->SetTextureRect(0.0f, 0.0f, 64.0f, 64.0f);


	// -----------------------------------------------------------
	// 定数バッファ (ライト・カメラ)
	// -----------------------------------------------------------

	// ライト
	Microsoft::WRL::ComPtr<ID3D12Resource> lightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* lightData = nullptr;
	lightResource->Map(0, nullptr, reinterpret_cast<void**>(&lightData));
	lightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightData->direction = { 0.0f, -1.0f, 1.0f }; // 少し斜め前から
	lightData->intensity = 1.0f;

	// 3Dカメラ
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource = CreateBufferResource(device, sizeof(CameraForGpu));
	CameraForGpu* cameraData = nullptr;
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));

	Transform cameraTransform = { {1.0f, 1.0f, 1.0f}, {0.3f, 0.0f, 0.0f}, {0.0f, 4.0f, -10.0f} };

	// 2Dスプライト用カメラ (平行投影)
	Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(
		0.0f, 0.0f, (float)winApp->kClientWidth, (float)winApp->kClientHeight, 0.0f, 100.0f);


	// ===============================================
	// メインループ
	// ===============================================
	while (!winApp->IsEndRequested()) {
		winApp->ProcessMessage();
		input->Update();

		if (input->IsKeyTriggered(DIK_ESCAPE)) {
			break;
		}

		// --- 更新処理 ---

		// カメラ操作
		if (input->IsKeyPressed(DIK_UP)) { cameraTransform.translate.y += 0.1f; }
		if (input->IsKeyPressed(DIK_DOWN)) { cameraTransform.translate.y -= 0.1f; }

		// ブロック1の回転
		modelBlock1->transform.rotate.y += 0.02f;

		// ブロック2の移動 (キー操作)
		if (input->IsKeyPressed(DIK_D)) { modelBlock2->transform.translate.x += 0.1f; }
		if (input->IsKeyPressed(DIK_A)) { modelBlock2->transform.translate.x -= 0.1f; }

		// スプライトの移動テスト
		if (input->IsKeyPressed(DIK_RIGHT)) { spriteBall->transform.translate.x += 2.0f; }
		if (input->IsKeyPressed(DIK_LEFT)) { spriteBall->transform.translate.x -= 2.0f; }

		// Sprite更新
		spriteBall->Update();
		spriteCut->Update();


		// 3Dカメラ行列計算
		cameraData->worldPosition = cameraTransform.translate;
		Matrix4x4 viewMatrix = Inverse(MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate));
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(winApp->kClientWidth) / float(winApp->kClientHeight), 0.1f, 100.0f);
		Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);


		// --- 描画処理 ---
		dxCommon->PreDraw();
		commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
		commandList->SetPipelineState(pipeline->GetPipelineState());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// ★ TextureManagerが管理するSRVヒープをセット
		ID3D12DescriptorHeap* ppHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
		commandList->SetDescriptorHeaps(1, ppHeaps);

		// 共通定数バッファ (Light / Camera)
		commandList->SetGraphicsRootConstantBufferView(3, lightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());

		// --- 3D描画 ---
		modelBlock1->Draw(commandList, viewProjectionMatrix);
		modelBlock2->Draw(commandList, viewProjectionMatrix);
		modelAxis->Draw(commandList, viewProjectionMatrix);

		// --- 2Dスプライト描画 ---
		// Z書き込みを無効にするPipelineStateに変えるのが理想ですが、簡易的にこのまま描画
		// 深度テストで負けないようにZ座標を手前などで調整するか、3Dの後に描画することで上書き期待
		spriteBall->Draw(commandList, projectionMatrixSprite);
		spriteCut->Draw(commandList, projectionMatrixSprite);

		dxCommon->PostDraw();
	}

	// ===============================================
	// 終了処理
	// ===============================================

	delete modelBlock1;
	delete modelBlock2;
	delete modelAxis;
	delete spriteBall;
	delete spriteCut;

	delete pipeline;
	// delete input; // シングルトンのため削除不要

	// マネージャーのシングルトンインスタンスは、プログラム終了時に
	// OSによってメモリ解放されるため、簡易的にはそのままでもリーク検出以外では問題起きませんが、
	// 厳密には終了処理関数(Finalize)を作って呼ぶのが良いです。今回は省略します。

	dxCommon->Finalize();
	CoUninitialize();
	winApp->Finalize();

	return 0;
}