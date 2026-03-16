#include "ImGuiManager.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h" // これが InitInfo の定義
#include "externals/imgui/imgui_impl_win32.h"
#endif

ImGuiManager* ImGuiManager::GetInstance()
{
	static ImGuiManager instance;
	return &instance;
}

struct ImGui_ImplDX12_InitInfo
{
	ID3D12Device* Device = nullptr;
	ID3D12CommandQueue* CommandQueue = nullptr;
	DXGI_FORMAT RTVFormat = DXGI_FORMAT_UNKNOWN;
	int NumFramesInFlight = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE LegacySingleSrvCpuDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE LegacySingleSrvGpuDescriptor = {};
};


void ImGuiManager::Finalize()
{
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
}

void ImGuiManager::Initialize(WinApp* winAPI, DirectXCommon* dxBase)
{
	winAPI_ = winAPI;
	dxBase_ = dxBase;

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winAPI_->GetHwnd());

	// SrvManagerからヒープとインデックスをもらう
	// ※プロジェクトの実装に合わせて取得関数名を適宜調整してください
	// 例: srvHeap_ = SrvManager::GetInstance()->GetDescriptorHeap();

	srvIndex_ = SrvManager::GetInstance()->Allocate();
	srvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(srvIndex_);
	srvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex_);

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = dxBase_->GetDevice();
	init_info.CommandQueue = dxBase_->GetCommandQueue();
	// プロジェクトの DirectXCommon に合わせて GetBackBufferCount を使用
	init_info.NumFramesInFlight = static_cast<int>(dxBase_->GetBackBufferCount());
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	init_info.LegacySingleSrvCpuDescriptor = srvHandleCPU_;
	init_info.LegacySingleSrvGpuDescriptor = srvHandleGPU_;

	ImGui_ImplDX12_Init(
		init_info.Device,
		init_info.NumFramesInFlight,
		init_info.RTVFormat,
		srvHeap_,
		init_info.LegacySingleSrvCpuDescriptor,
		init_info.LegacySingleSrvGpuDescriptor
	);

#endif
}

void ImGuiManager::Begin()
{
#ifdef USE_IMGUI
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

void ImGuiManager::End()
{
#ifdef USE_IMGUI
	ImGui::Render();
#endif
}

void ImGuiManager::Draw()
{
#ifdef USE_IMGUI
	ID3D12GraphicsCommandList* commandList = dxBase_->GetCommandList();
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap_ };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif
}