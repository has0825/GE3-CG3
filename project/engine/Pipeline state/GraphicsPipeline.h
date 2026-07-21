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
    static GraphicsPipeline* GetInstance();

    // 初期化
    void Initialize(ID3D12Device* device);

    // ゲッター
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    ID3D12PipelineState* GetPipelineState(BlendMode blendMode) const { return pipelineStates_.at(blendMode).Get(); }

    // Skybox用ゲッター
    ID3D12PipelineState* GetSkyboxPipelineState() const { return skyboxPipelineState_.Get(); }

    // ★追加：Object3d（キャラクターモデル）用のゲッター
    ID3D12RootSignature* GetObject3dRootSignature() const { return object3dRootSignature_.Get(); }

    // ★追加：Skydome（天球）専用のゲッター（Zバッファ書き込みなし・最遠面固定シェーダー）
    ID3D12PipelineState* GetSkydomePipelineState() const { return skydomePipelineState_.Get(); }
    ID3D12RootSignature* GetSkydomeRootSignature() const { return skydomeRootSignature_.Get(); }
    	ID3D12PipelineState* GetObject3dPipelineState() { return object3dPipelineState_.Get(); }
        ID3D12PipelineState* GetObject3dBlendNormalPipelineState() { return object3dBlendNormalPipelineState_.Get(); }
	ID3D12RootSignature* GetSkinningRootSignature() { return skinningRootSignature_.Get(); }
	ID3D12PipelineState* GetSkinningPipelineState() { return skinningPipelineState_.Get(); }
	ID3D12RootSignature* GetComputeRootSignature() { return computeRootSignature_.Get(); }
	ID3D12PipelineState* GetSkinningComputePipelineState() { return skinningComputePipelineState_.Get(); }
	ID3D12RootSignature* GetGpuParticleRootSignature() { return gpuParticleRootSignature_.Get(); }
	ID3D12PipelineState* GetGpuParticlePipelineState() { return gpuParticlePipelineState_.Get(); }
	ID3D12RootSignature* GetGpuParticleInitializeRootSignature() { return gpuParticleInitializeRootSignature_.Get(); }
	ID3D12PipelineState* GetGpuParticleInitializePipelineState() { return gpuParticleInitializePipelineState_.Get(); }
	ID3D12RootSignature* GetGpuParticleEmitRootSignature() { return gpuParticleEmitRootSignature_.Get(); }
	ID3D12PipelineState* GetGpuParticleEmitPipelineState() { return gpuParticleEmitPipelineState_.Get(); }
	ID3D12RootSignature* GetGpuParticleUpdateRootSignature() { return gpuParticleUpdateRootSignature_.Get(); }
	ID3D12PipelineState* GetGpuParticleUpdatePipelineState() { return gpuParticleUpdatePipelineState_.Get(); }
    ID3D12PipelineState* GetFullscreenPipelineState() const { return copyImagePipelineState_.Get(); }
    ID3D12PipelineState* GetGrayscalePipelineState() const { return grayscalePipelineState_.Get(); }
    ID3D12PipelineState* GetSepiaPipelineState() const { return sepiaPipelineState_.Get(); }
    ID3D12PipelineState* GetVignettePipelineState() const { return vignettePipelineState_.Get(); }
    ID3D12PipelineState* GetBoxFilterPipelineState() const { return boxFilterPipelineState_.Get(); }
    ID3D12PipelineState* GetLuminanceOutlinePipelineState() const { return luminanceOutlinePipelineState_.Get(); }
    // Sprite pipeline getters
    ID3D12RootSignature* GetSpriteRootSignature() const { return spriteRootSignature_.Get(); }
    ID3D12PipelineState* GetSpritePipelineState() const { return spritePipelineState_.Get(); }
    ID3D12PipelineState* GetRadialBlurPipelineState() const { return radialBlurPipelineState_.Get(); }
    ID3D12PipelineState* GetDissolvePipelineState() const { return dissolvePipelineState_.Get(); }
    ID3D12PipelineState* GetRandomPipelineState() const { return randomPipelineState_.Get(); }
    ID3D12RootSignature* GetFullscreenRootSignature() const { return copyImageRootSignature_.Get(); }
    ID3D12RootSignature* GetDissolveRootSignature() const { return dissolveRootSignature_.Get(); }
    ID3D12PipelineState* GetGaussianFilterPipelineState() const { return gaussianFilterPipelineState_.Get(); }
    ID3D12PipelineState* GetInvertPipelineState() const { return invertPipelineState_.Get(); }
    ID3D12PipelineState* GetPixelatePipelineState() const { return pixelatePipelineState_.Get(); }
    ID3D12PipelineState* GetChromaticAberrationPipelineState() const { return chromaticAberrationPipelineState_.Get(); }
    ID3D12PipelineState* GetDepthOutlinePipelineState() const { return depthOutlinePipelineState_.Get(); }
    ID3D12RootSignature* GetDepthOutlineRootSignature() const { return depthOutlineRootSignature_.Get(); }


private:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() = default;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> object3dBlendNormalPipelineState_;

    // ★追加：Skydome（天球）専用パイプラインステート
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skydomePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skydomeRootSignature_;

    	Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningComputePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleInitializeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleInitializePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleEmitRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleEmitPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleUpdateRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleUpdatePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> copyImagePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> grayscalePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sepiaPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> vignettePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> boxFilterPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> luminanceOutlinePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> radialBlurPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> dissolvePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> randomPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gaussianFilterPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> invertPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pixelatePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> chromaticAberrationPipelineState_;
	    Microsoft::WRL::ComPtr<ID3D12RootSignature> dissolveRootSignature_;
    // Sprite pipeline members
    Microsoft::WRL::ComPtr<ID3D12RootSignature> spriteRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> depthOutlineRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> depthOutlinePipelineState_;


    std::ofstream logStream_;
};