// ★ここ重要: Windowsのマクロ(min/max)を無効化
#define NOMINMAX 

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
#include <random>
#include <cmath>
#include <algorithm> // min, max用
#include <wrl/client.h>
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
#include "D3D12Util.h"
#include "Model.h"
#include "MathUtil.h"
#include "DataTypes.h"

// ★ここ重要: 確実にマクロを無効化する
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// --- 関数定義の修正 ---

// ※ MakeRotateYMatrix と Normalize は MathUtil.obj に存在するため、ここには書きません。
//    ヘッダー(MathUtil.h)から読み込まれます。

// 以下の関数は MathUtil に無いため、ここで定義します (LNK2001エラー対策)

// ベクトル計算のヘルパー: 加算
Vector3 Add(const Vector3& v1, const Vector3& v2) {
	return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

// ベクトル計算のヘルパー: スケーリング
Vector3 Scale(const Vector3& v, float s) {
	return { v.x * s, v.y * s, v.z * s };
}

// ベクトルを回転（平行移動成分を無視して変換）
Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
	Vector3 result;
	result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
	result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
	result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
	return result;
}

// ---------------------------------------------

struct Particle {
	Transform transform;
	Vector3 velocity;
	Vector4 color;
	float lifeTime;
	float currentTime;
};

struct ParticleForGPU {
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

enum ParticleType {
	kTypeExplosion,
	kTypeFountain,
	kTypeSpiral,
	kTypeRain
};

Particle MakeNewParticle(std::mt19937& randomEngine, int type, const Vector3& emitterPos) {
	Particle particle;
	particle.transform.scale = { 1.0f, 1.0f, 1.0f };
	particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
	particle.currentTime = 0.0f;

	std::uniform_real_distribution<float> distPos(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
	std::uniform_real_distribution<float> distLife(1.0f, 3.0f);

	switch (type) {
	case kTypeExplosion:
	default:
		particle.transform.translate = {
			emitterPos.x + distPos(randomEngine) * 0.5f,
			emitterPos.y + distPos(randomEngine) * 0.5f,
			emitterPos.z + distPos(randomEngine) * 0.5f
		};
		particle.velocity = { distVel(randomEngine), distVel(randomEngine), distVel(randomEngine) };
		particle.lifeTime = distLife(randomEngine);
		particle.color = { distColor(randomEngine), distColor(randomEngine), distColor(randomEngine), 1.0f };
		break;
	case kTypeFountain:
		particle.transform.translate = {
			emitterPos.x + distPos(randomEngine) * 0.2f,
			emitterPos.y,
			emitterPos.z + distPos(randomEngine) * 0.2f
		};
		particle.velocity = { distVel(randomEngine) * 0.5f, 2.0f + std::abs(distVel(randomEngine)), distVel(randomEngine) * 0.5f };
		particle.lifeTime = 2.0f;
		particle.color = { 0.2f, 0.5f, 1.0f, 1.0f };
		break;
	case kTypeSpiral:
	{
		float angle = distPos(randomEngine) * (float)M_PI;
		float radius = 1.5f;
		particle.transform.translate = {
			emitterPos.x + std::cos(angle) * radius,
			emitterPos.y,
			emitterPos.z + std::sin(angle) * radius
		};
		particle.velocity = { 0.0f, 1.0f, 0.0f };
		particle.lifeTime = 3.0f;
		particle.color = { distColor(randomEngine), distColor(randomEngine), distColor(randomEngine), 1.0f };
	}
	break;
	case kTypeRain:
		particle.transform.translate = {
			emitterPos.x + distPos(randomEngine) * 5.0f,
			emitterPos.y + 5.0f,
			emitterPos.z + distPos(randomEngine) * 5.0f
		};
		particle.velocity = { 0.0f, -3.0f, 0.0f };
		particle.lifeTime = 3.0f;
		particle.color = { 0.8f, 0.8f, 1.0f, 1.0f };
		particle.transform.scale = { 0.2f, 1.0f, 0.2f };
		break;
	}
	return particle;
}

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
void Log(std::ostream& os, const std::string& message)
{
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}
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

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	D3DResourceLeakChecker leakChecker;

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}

	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);

	CoInitializeEx(0, COINIT_MULTITHREADED);
	SetUnhandledExceptionFilter(ExportDump);

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;
	XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	xAudio2->CreateMasteringVoice(&masterVoice);

	ID3D12Device* device = dxCommon->GetDevice();
	GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
	graphicsPipeline->Initialize(device);

	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	Model* particleModel = Model::CreateParticleModel(device);

	const UINT kNumInstances = 1000;

	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
		CreateBufferResource(device, sizeof(ParticleForGPU) * kNumInstances);

	ParticleForGPU* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

	std::random_device seedGenerator;
	std::mt19937 randomEngine(seedGenerator());

	int currentEffect = kTypeExplosion;
	bool useGravity = false;
	bool useAdditiveBlend = true;
	Vector3 emitterPos = { 0.0f, 0.0f, 0.0f };

	std::vector<Particle> particles(kNumInstances);
	for (UINT i = 0; i < kNumInstances; ++i) {
		particles[i] = MakeNewParticle(randomEngine, currentEffect, emitterPos);
		std::uniform_real_distribution<float> distTime(0.0f, 3.0f);
		particles[i].currentTime = distTime(randomEngine);
	}

	for (UINT i = 0; i < kNumInstances; ++i) {
		instancingData[i].WVP = MakeIdentity4x4();
		instancingData[i].World = MakeIdentity4x4();
		instancingData[i].color = particles[i].color;
	}

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
		CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 画像読み込み
	DirectX::ScratchImage mipImages = LoadTexture("resources/circle.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		UploadTextureData(textureResource.Get(), mipImages, device, commandList);

	commandList->Close();
	ID3D12CommandQueue* commandQueue = dxCommon->GetCommandQueue();
	ID3D12CommandList* ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, ppCommandLists);

	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	commandQueue->Signal(fence.Get(), 1);
	if (fence->GetCompletedValue() < 1) {
		fence->SetEventOnCompletion(1, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}
	CloseHandle(fenceEvent);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumInstances;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

	const uint32_t kInstancingSrvIndex = 2;
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);

	device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

	// カメラ設定
	Transform cameraTransform{ { 1.0f, 1.0f, 1.0f }, { 0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, -15.0f } };
	float cameraSpeed = 5.0f;
	float mouseSensitivity = 0.005f;

	const float kDeltaTime = 1.0f / 60.0f;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(), dxCommon->GetRtvDesc().Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	while (!winApp->IsEndRequested()) {
		winApp->ProcessMessage();

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Particle Controller");
		ImGui::Text("Effect Type");
		ImGui::RadioButton("Explosion", &currentEffect, kTypeExplosion); ImGui::SameLine();
		ImGui::RadioButton("Fountain", &currentEffect, kTypeFountain); ImGui::SameLine();
		ImGui::RadioButton("Spiral", &currentEffect, kTypeSpiral); ImGui::SameLine();
		ImGui::RadioButton("Rain", &currentEffect, kTypeRain);
		ImGui::Separator();
		ImGui::Text("Settings");
		ImGui::Checkbox("Use Gravity", &useGravity);
		ImGui::Checkbox("Glow (Additive Blend)", &useAdditiveBlend);
		ImGui::DragFloat3("Emitter Pos", &emitterPos.x, 0.1f);
		ImGui::Separator();
		ImGui::Text("Camera Controls:");
		ImGui::Text(" Hold Right Mouse: Rotate");
		ImGui::Text(" WASD: Move, Q/E: Up/Down");
		ImGui::End();

		// ==========================================================================================
		// カメラ操作ロジック
		// ==========================================================================================
		ImGuiIO& io = ImGui::GetIO();

		// マウスによる視点回転 (右ドラッグ時のみ)
		if (ImGui::IsMouseDragging(1)) {
			cameraTransform.rotate.y += io.MouseDelta.x * mouseSensitivity;
			cameraTransform.rotate.x += io.MouseDelta.y * mouseSensitivity;

			// カッコで囲んでマクロ衝突を回避
			cameraTransform.rotate.x = (std::max)(-1.5f, (std::min)(1.5f, cameraTransform.rotate.x));
		}

		Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
		if (GetAsyncKeyState('W') & 0x8000) moveDir.z += 1.0f;
		if (GetAsyncKeyState('S') & 0x8000) moveDir.z -= 1.0f;
		if (GetAsyncKeyState('D') & 0x8000) moveDir.x += 1.0f;
		if (GetAsyncKeyState('A') & 0x8000) moveDir.x -= 1.0f;
		if (GetAsyncKeyState('E') & 0x8000) moveDir.y += 1.0f;
		if (GetAsyncKeyState('Q') & 0x8000) moveDir.y -= 1.0f;

		if (moveDir.x != 0.0f || moveDir.y != 0.0f || moveDir.z != 0.0f) {
			// MakeRotateYMatrix は MathUtil.obj のものを使用
			Matrix4x4 cameraRotY = MakeRotateYMatrix(cameraTransform.rotate.y);
			// TransformNormal は main.cpp で定義したものを使用
			Vector3 rotatedMoveDir = TransformNormal(moveDir, cameraRotY);
			// Normalize は MathUtil.obj のものを使用
			rotatedMoveDir = Normalize(rotatedMoveDir);
			// Scale は main.cpp で定義したものを使用
			rotatedMoveDir = Scale(rotatedMoveDir, cameraSpeed * kDeltaTime);
			// Add は main.cpp で定義したものを使用
			cameraTransform.translate = Add(cameraTransform.translate, rotatedMoveDir);
		}

		Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
		Matrix4x4 viewMatrix = Inverse(cameraMatrix);
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)winApp->kClientWidth / (float)winApp->kClientHeight, 0.1f, 100.0f);
		Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

		for (uint32_t i = 0; i < kNumInstances; ++i) {
			if (particles[i].currentTime >= particles[i].lifeTime) {
				particles[i] = MakeNewParticle(randomEngine, currentEffect, emitterPos);
			}

			if (useGravity) {
				particles[i].velocity.y -= 9.8f * kDeltaTime * 0.5f;
			}

			particles[i].transform.translate.x += particles[i].velocity.x * kDeltaTime;
			particles[i].transform.translate.y += particles[i].velocity.y * kDeltaTime;
			particles[i].transform.translate.z += particles[i].velocity.z * kDeltaTime;

			particles[i].currentTime += kDeltaTime;

			float alpha = 1.0f - (particles[i].currentTime / particles[i].lifeTime);
			particles[i].color.w = alpha;

			Matrix4x4 worldMatrix = MakeAffineMatrix(particles[i].transform.scale, particles[i].transform.rotate, particles[i].transform.translate);

			instancingData[i].World = worldMatrix;
			instancingData[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
			instancingData[i].color = particles[i].color;
		}

		dxCommon->PreDraw();

		commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
		ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, descriptorHeaps);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		BlendMode blendMode = useAdditiveBlend ? kBlendModeAdd : kBlendModeNormal;
		commandList->SetPipelineState(graphicsPipeline->GetPipelineState(blendMode));

		particleModel->Draw(
			commandList,
			kNumInstances,
			textureSrvHandleGPU,
			instancingSrvHandleGPU
		);

		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
		dxCommon->PostDraw();
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	delete particleModel;
	delete graphicsPipeline;

	dxCommon->Finalize();
	CoUninitialize();
	winApp->Finalize();

	return 0;
}