#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

// 前方宣言
class WinApp;

// DirectX汎用クラス
class DirectXCommon {
public:
    // シングルトンインスタンスの取得
    static DirectXCommon* GetInstance();

    // 初期化
    void Initialize(WinApp* winApp);

    // 終了処理
    void Finalize();

    // 描画前処理
    void PreDraw();

    // 描画後処理
    void PostDraw();

    // ゲッター
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
    ID3D12DescriptorHeap* GetRtvDescriptorHeap() const { return rtvDescriptorHeap_.Get(); }
    D3D12_RENDER_TARGET_VIEW_DESC GetRtvDesc() const { return rtvDesc_; }
    UINT GetBackBufferCount() const { return kBackBufferCount_; }
    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }


private:
    DirectXCommon() = default;
    ~DirectXCommon() = default;
    DirectXCommon(const DirectXCommon&) = delete;
    const DirectXCommon& operator=(const DirectXCommon&) = delete;

    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(WinApp* winApp);
    void CreateRenderTarget();
    void CreateDepthBuffer(WinApp* winApp);
    void CreateFence();

private:
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    static const UINT kBackBufferCount_ = 2;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount_];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount_];
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_{};

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
};