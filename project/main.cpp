#define _USE_MATH_DEFINES
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

// ★ C2065 (VK_W など) 対策: Windows.h より先に winuser.h をインクルード
#include <winuser.h> 
#include <Windows.h>

#include <objbase.h>
#include <d3d12.h>
#include <dbghelp.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <strsafe.h>
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

#include "WinApp.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h" // ★ D3D12Util.h (LoadTexture など)
#include "Model.h"
#include "MathUtil.h"
#include "DataTypes.h"
#include "Input.h" // ★ 作成した Input.h

// === このファイルに残っているヘルパー関数 ===
// (C2084 エラーを避けるため、D3D12Util.h にない関数だけを残します)

// エラーハンドリング
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
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

// ログ出力
void Log(std::ostream& os, const std::string& message)
{
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

// 文字列変換
std::wstring ConvertString(const std::string& str)
{
	if (str.empty()) { return std::wstring(); }
	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) { return std::wstring(); }
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}
std::string ConvertString(const std::wstring& str)
{
	if (str.empty()) { return std::string(); }
	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) { return std::string(); }
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

// D3Dリソースリークチェッカー
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker()
	{
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

// ===============================================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	D3DResourceLeakChecker leakChecker;

	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize(); // WinAppのInitializeを先に呼ぶ

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);

	CoInitializeEx(0, COINIT_MULTITHREADED);
	SetUnhandledExceptionFilter(ExportDump);

	// --- ★ 1. Inputの初期化 (達成条件: new) ---
	Input* input = new Input();
	input->Initialize();

	// --- ★ 2. player.objを描画・操作するための初期化 ---

	// パイプラインの初期化
	GraphicsPipeline* pipeline = new GraphicsPipeline();
	// ★ DirectXCommon.h の GetDevice() を使用
	pipeline->Initialize(dxCommon->GetDevice());

	// ★ テクスチャのロード (D3D12Util.h の関数を使用)
	// (SRVDescriptorHeap の作成)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(
		dxCommon->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
	// (テクスチャファイルのロード)
	// ★ D3D12Util.h の LoadTexture を使用
	DirectX::ScratchImage textureImage = LoadTexture("resources/white1x1.png");
	// (GPUへのアップロード)
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(
		dxCommon->GetDevice(), textureImage.GetMetadata());
	UploadTextureData(
		textureResource.Get(), textureImage, dxCommon->GetDevice(), dxCommon->GetCommandList());

	// (SRVの作成)
	// ★ D3D12Util.h の GetCPUDescriptorHandle を使用
	UINT srvDescriptorSize = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), srvDescriptorSize, 0);
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), srvDescriptorSize, 0); // Drawで使う

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = textureImage.GetMetadata().format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureImage.GetMetadata().mipLevels);
	// ★ D3D12Util.h に SRV作成ヘルパーが無かったため、デバイスから直接呼び出す
	dxCommon->GetDevice()->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCpuHandle);


	// モデルのロード (Model.h の Create 関数を使用)
	Model* playerModel = Model::Create("resources", "player.obj", dxCommon->GetDevice());

	// プレイヤーのTransform初期化 (Model.h に public transform があるため直接設定)
	playerModel->transform.scale = { 1.0f, 1.0f, 1.0f };
	playerModel->transform.rotate = { 0.0f, 0.0f, 0.0f };
	playerModel->transform.translate = { 0.0f, 0.0f, 0.0f };

	// カメラのTransform初期化
	Transform cameraTransform = {};
	cameraTransform.scale = { 1.0f, 1.0f, 1.0f };
	cameraTransform.rotate = { 0.0f, 0.0f, 0.0f };
	cameraTransform.translate = { 0.0f, 0.0f, -10.0f }; // 少し後ろから見る


	// --- 初期化処理ここまで ---

	while (!winApp->IsEndRequested()) {
		winApp->ProcessMessage();

		// --- ★ 3. 更新処理 ---
		input->Update(); // Inputの毎フレーム更新

		// (Inputで player.obj を動かす)
		{
			const float moveSpeed = 0.1f;

			// ★ C2065 (VK_W など) 対策済み
			if (input->IsKeyDown(VK_A)) {
				playerModel->transform.translate.x -= moveSpeed;
			}
			if (input->IsKeyDown(VK_D)) {
				playerModel->transform.translate.x += moveSpeed;
			}
			if (input->IsKeyDown(VK_W)) {
				playerModel->transform.translate.z += moveSpeed;
			}
			if (input->IsKeyDown(VK_S)) {
				playerModel->transform.translate.z -= moveSpeed;
			}
		}

		// --- ★ 4. 行列の計算 ---

		// カメラのビュー行列
		Matrix4x4 cameraMatrix = MakeAffineMatrix(
			cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
		Matrix4x4 viewMatrix = Inverse(cameraMatrix);

		// ★ C2039 (kWindowWidth) 対策: WinApp.h の kClientWidth/Height を使用
		float aspectRatio = float(WinApp::kClientWidth) / float(WinApp::kClientHeight);

		// プロジェクション行列
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
			0.45f,
			aspectRatio, // ★ 修正
			0.1f,
			100.0f
		);

		// ViewProjection マトリックス (Model::Draw の引数に必要)
		Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);


		// --- 描画処理 ---
		dxCommon->PreDraw();

		// ★ 5. 描画コマンド
		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

		// パイプラインステートのセット
		commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
		commandList->SetPipelineState(pipeline->GetPipelineState());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// SRVヒープをセット
		ID3D12DescriptorHeap* heaps[] = { srvDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, heaps);

		// Model::Draw を呼び出す
		playerModel->Draw(
			commandList,
			viewProjectionMatrix,
			srvGpuHandle // ロードしたテクスチャのハンドル
		);

		dxCommon->PostDraw();
	}

	// --- 終了処理 ---

	// ★ 6. 解放処理 (new した順と逆が望ましい)
	delete playerModel;
	delete pipeline;
	delete input; // (達成条件: delete)

	dxCommon->Finalize();

	CoUninitialize();

	winApp->Finalize();

	return 0;
}