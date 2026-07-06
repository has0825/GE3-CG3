#include "Framework.h"
#include "SrvManager.h"
#include <dbghelp.h>
#include <strsafe.h>
#include <iostream> // 追加: Log用

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dbghelp.lib")

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

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

// 例外ダンプ出力関数
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t filePath[MAX_PATH] = { 0 };
    CreateDirectory(L"./Dumps", nullptr);
    StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
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

void Framework::Run() {
    // 例外フィルタの設定
    SetUnhandledExceptionFilter(ExportDump);

    // 1. システム初期化
    Initialize();

    // 2. ゲームループ
    while (true) {
        // 終了リクエストがあれば抜ける
        if (IsEndRequest()) {
            break;
        }

#ifdef USE_IMGUI
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
#endif

        // 3. 更新処理
        Update();

        // 4. 描画処理
        Draw();
    }

    // 5. 終了処理
    Finalize();
}

void Framework::Initialize() {
    winApp_ = WinApp::GetInstance();
    winApp_->Initialize();

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_);

    SrvManager::GetInstance()->Initialize(dxCommon_);

    // COMの初期化（Audio用）
    CoInitializeEx(0, COINIT_MULTITHREADED);

    audio_ = std::make_unique<Audio>();
    audio_->Initialize();
    
    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    if (graphicsPipeline_) {
        graphicsPipeline_->Initialize(dxCommon_->GetDevice());
    }

    // SRVヒープ作成
    srvDescriptorHeap_ = CreateDescriptorHeap(dxCommon_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
    descriptorSizeSRV_ = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

#ifdef USE_IMGUI
    // ImGui初期化
    imguiDescriptorHeap_ = CreateDescriptorHeap(dxCommon_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(winApp_->GetHwnd());
    ImGui_ImplDX12_Init(dxCommon_->GetDevice(), dxCommon_->GetBackBufferCount(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, imguiDescriptorHeap_.Get(),
        imguiDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
        imguiDescriptorHeap_->GetGPUDescriptorHandleForHeapStart());
#endif
}

void Framework::Finalize() {
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif

    // Audioはstd::unique_ptrの破棄時にデストラクタで終了処理が行われるため削除

    CoUninitialize();

    dxCommon_->Finalize();
    winApp_->Finalize();
}

void Framework::Update() {}

void Framework::Draw() {}

bool Framework::IsEndRequest() {
    return winApp_->ProcessMessage();
}