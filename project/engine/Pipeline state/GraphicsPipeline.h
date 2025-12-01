#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <fstream>
#include <vector>

// ブレンドモードの定義
enum BlendMode {
    kBlendModeNone,       // ブレンドなし
    kBlendModeNormal,     // 通常αブレンド (SrcAlpha, InvSrcAlpha)
    kBlendModeAdd,        // 加算合成 (SrcAlpha, One)
    kBlendModeSubtract,   // 減算合成 (RevSubtract)
    kBlendModeMultiply,   // 乗算合成 (Zero, SrcColor)
    kBlendModeAlphaClip,  // アルファクリッピング (PS内でdiscard)
    kCountOfBlendMode,    // カウント用
};

// グラフィックスパイプライン管理クラス
class GraphicsPipeline {
public:
    // コンストラクタ・デストラクタ
    GraphicsPipeline() = default;
    ~GraphicsPipeline() = default;

    // 初期化
    void Initialize(ID3D12Device* device);

    // ゲッター
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    // 指定したブレンドモードのPSOを取得
    ID3D12PipelineState* GetPipelineState(BlendMode blendMode) const { return pipelineStates_[blendMode].Get(); }

private:
    // シェーダーのコンパイル用ヘルパー関数
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
        const std::wstring& filePath,
        const wchar_t* profile,
        IDxcUtils* dxcUtils,
        IDxcCompiler3* dxcCompiler,
        IDxcIncludeHandler* includeHandler);

private:
    // ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    // パイプラインステート (ブレンドモードごとに作成)
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_[kCountOfBlendMode];

    // ログ出力用ストリーム
    std::ofstream logStream_;
};