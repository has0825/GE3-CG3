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

// ログ出力
void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

// 文字列変換
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

// D3Dリソースリークチェッカー
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


int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	D3DResourceLeakChecker leakChecker;

	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);

	Input* input = Input::GetInstance();
	input->Initialize(winApp);

	CoInitializeEx(0, COINIT_MULTITHREADED);
	SetUnhandledExceptionFilter(ExportDump);


	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();


	GraphicsPipeline* pipeline = new GraphicsPipeline();
	pipeline->Initialize(device);


	const uint32_t kMaxSRVCount = 2056;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
		CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);



	DirectX::ScratchImage mipImages = LoadTexture("resources/player/player.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);


	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		UploadTextureData(textureResource.Get(), mipImages, device, commandList);

	// SRVを作成 (ヒープの0番目に作成)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	UINT descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0);

	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

	Microsoft::WRL::ComPtr<ID3D12Resource> lightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* lightData = nullptr;
	lightResource->Map(0, nullptr, reinterpret_cast<void**>(&lightData));
	lightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightData->direction = { 0.0f, -1.0f, 0.0f }; // 真上からのライト
	lightData->intensity = 1.0f;


	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource = CreateBufferResource(device, sizeof(CameraForGpu));
	CameraForGpu* cameraData = nullptr;
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));

	Transform cameraTransform = { {1.0f, 1.0f, 1.0f}, {0.1f, 0.0f, 0.0f}, {0.0f, 5.0f, -15.0f} };
	cameraData->worldPosition = cameraTransform.translate; // カメラのワールド座標をシェーダーに渡す

	Matrix4x4 viewMatrix = Inverse(MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate));
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(winApp->kClientWidth) / float(winApp->kClientHeight), 0.1f, 100.0f);
	Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);


	Model* playerModel = Model::Create("resources/player", "player.obj", device);
	playerModel->transform.translate = { 0.0f, 0.0f, 0.0f };
	playerModel->transform.scale = { 1.0f, 1.0f, 1.0f };
	playerModel->transform.rotate = { 0.0f, 0.0f, 0.0f };
	// ------------------------

	// --- メインループ ---
	while (!winApp->IsEndRequested()) {
		winApp->ProcessMessage();
		input->Update();

		if (input->IsKeyTriggered(DIK_ESCAPE)) {
			break; // ループを抜ける
		}


		Vector3 move = { 0.0f, 0.0f, 0.0f };
		const float kMoveSpeed = 0.1f;

		if (input->IsKeyPressed(DIK_W)) {
			move.z += kMoveSpeed;
		}
		if (input->IsKeyPressed(DIK_S)) {
			move.z -= kMoveSpeed;
		}
		if (input->IsKeyPressed(DIK_A)) {
			move.x -= kMoveSpeed;
		}
		if (input->IsKeyPressed(DIK_D)) {
			move.x += kMoveSpeed;
		}

		// transformに移動量を加算
		playerModel->transform.translate.x += move.x;
		playerModel->transform.translate.y += move.y;
		playerModel->transform.translate.z += move.z;

		// --- 描画処理 ---
		dxCommon->PreDraw();
		commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
		commandList->SetPipelineState(pipeline->GetPipelineState());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// SRVヒープの設定
		ID3D12DescriptorHeap* ppHeaps[] = { srvDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, ppHeaps);


		// Model::Draw の中で [0], [1], [2] が設定される
		// [3] ライト
		commandList->SetGraphicsRootConstantBufferView(3, lightResource->GetGPUVirtualAddress());
		// [4] カメラ
		commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());


		playerModel->Draw(commandList, viewProjectionMatrix, textureSrvHandleGPU);

		dxCommon->PostDraw();
	}

	// --- 終了処理 ---

	delete playerModel;

	delete pipeline;
	// (ComPtrで管理されているリソースは自動解放されます)

	input->Finalize();
	dxCommon->Finalize();
	CoUninitialize();
	winApp->Finalize();

	return 0;
}