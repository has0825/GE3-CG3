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
    ID3D12PipelineState* GetPipelineState(BlendMode blendMode) const { return pipelineStates_.at(blendMode).Get(); }

    // Skybox用ゲッター
    ID3D12PipelineState* GetSkyboxPipelineState() const { return skyboxPipelineState_.Get(); }

    // ★追加：Object3d（キャラクターモデル）用のゲッター
    ID3D12RootSignature* GetObject3dRootSignature() const { return object3dRootSignature_.Get(); }
    ID3D12PipelineState* GetObject3dPipelineState() const { return object3dPipelineState_.Get(); }

    ID3D12RootSignature* GetSkinningRootSignature() const { return skinningRootSignature_.Get(); }
    ID3D12PipelineState* GetSkinningPipelineState() const { return skinningPipelineState_.Get(); }

private:
    // シェーダーのコンパイル
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
        const std::wstring& filePath,
        const wchar_t* profile,
        IDxcUtils* dxcUtils,
        IDxcCompiler3* dxcCompiler,
        IDxcIncludeHandler* includeHandler);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::map<BlendMode, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;

    // ★追加：Object3d（キャラクターモデル）用のメンバ変数
    Microsoft::WRL::ComPtr<ID3D12RootSignature> object3dRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> object3dPipelineState_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningPipelineState_;

    std::ofstream logStream_;
};