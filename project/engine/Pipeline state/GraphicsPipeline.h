#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <fstream>
#include <map>

// ブレンドモードの定義
enum BlendMode {
    kBlendModeNone,       // ブレンドなし
    kBlendModeNormal,     // 通常αブレンド
    kBlendModeAdd,        // 加算
    kBlendModeSubtract,   // 減算
    kBlendModeMultiply,   // 乗算
    kBlendModeAlphaClip,  // アルファクリッピング用
    kCountOfBlendMode,    // カウント用
};


// グラフィックスパイプライン管理クラス
class GraphicsPipeline {
public:
    // 初期化
    void Initialize(ID3D12Device* device);

    // ゲッター
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    ID3D12PipelineState* GetPipelineState(BlendMode blendMode) const { return pipelineStates_[blendMode].Get(); }

private:
    // シェーダーのコンパイル
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
        const std::wstring& filePath,
        const wchar_t* profile,
        IDxcUtils* dxcUtils,
        IDxcCompiler3* dxcCompiler,
        IDxcIncludeHandler* includeHandler);

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_[kCountOfBlendMode]; // 全ブレンドモード分のPSO
    std::ofstream logStream_;
};