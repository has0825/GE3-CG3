#include "GraphicsPipeline.h"
#include <cassert>
#include <format>
#include <iostream>

#pragma comment(lib, "dxcompiler.lib")

// 外部ヘルパー関数のプロトタイプ宣言 (main.cpp等で定義されているもの)
// プロジェクトの構成によってはヘッダーインクルードに変更してください
void Log(std::ostream& os, const std::string& message);
std::string ConvertString(const std::wstring& str);

void GraphicsPipeline::Initialize(ID3D12Device* device) {
    // ログファイルを開く
    logStream_.open("ShaderCompile.log");

    // ===============================================
    // 1. DXC (DirectX Shader Compiler) の初期化
    // ===============================================
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));

    // ===============================================
    // 2. ルートシグネチャの作成 (Particle Shader仕様)
    // ===============================================
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // サンプラー設定 (テクスチャ用)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // register(s0)
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // ルートパラメータ設定 (3つ)
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // [0] Material (ConstantBuffer: register b0) -> Pixel Shader
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // [1] Particle Data (StructuredBuffer: register t1) -> Vertex Shader
    //     DescriptorTableとして定義
    D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing[1] = {};
    descriptorRangeInstancing[0].BaseShaderRegister = 1; // t1
    descriptorRangeInstancing[0].NumDescriptors = 1;
    descriptorRangeInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeInstancing;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInstancing);

    // [2] Texture (Texture2D: register t0) -> Pixel Shader
    //     DescriptorTableとして定義
    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = {};
    descriptorRangeTexture[0].BaseShaderRegister = 0; // t0
    descriptorRangeTexture[0].NumDescriptors = 1;
    descriptorRangeTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTexture[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeTexture;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeTexture);

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // シリアライズと生成
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(logStream_, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));


    // ===============================================
    // 3. シェーダーのコンパイル
    // ===============================================
    // Particle用のシェーダーファイルを読み込む
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Particle.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(vertexShaderBlob != nullptr);
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Particle.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(pixelShaderBlob != nullptr);


    // ===============================================
    // 4. Input Layout (頂点レイアウト)
    // ===============================================
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);


    // ===============================================
    // 5. 共通のPSO設定 (Rasterizer, Depth, Format)
    // ===============================================

    // ラスタライザ設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // パーティクルは両面描画するためカリングなし
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.FrontCounterClockwise = FALSE;

    // デプスステンシル設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    // 半透明描画のため深度バッファへの書き込みはOFFにする (Z-Fighting防止)
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;


    // ===============================================
    // 6. PSO生成 (ブレンドモードごとにループ)
    // ===============================================
    for (int i = 0; i < kCountOfBlendMode; ++i) {
        D3D12_BLEND_DESC blendDesc{};
        // 独立したブレンドを有効にするか（ここでは無関係だが念のためFALSE）
        blendDesc.IndependentBlendEnable = FALSE;

        // レンダーターゲット0の設定
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        switch (static_cast<BlendMode>(i)) {
        case kBlendModeNone:
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            break;

        case kBlendModeNormal:
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case kBlendModeAdd:
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE; // 加算
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case kBlendModeSubtract:
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT; // 減算
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case kBlendModeMultiply:
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR; // 乗算
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case kBlendModeAlphaClip:
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            break;
        }

        // PSO記述子の設定
        D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
        graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
        graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
        graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
        graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
        graphicsPipelineStateDesc.BlendState = blendDesc;
        graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
        graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
        graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        graphicsPipelineStateDesc.NumRenderTargets = 1;
        graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        graphicsPipelineStateDesc.SampleDesc.Count = 1;
        graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

        // 生成
        hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[i]));
        assert(SUCCEEDED(hr));
    }
}

// シェーダーコンパイル用ヘルパー関数
Microsoft::WRL::ComPtr<IDxcBlob> GraphicsPipeline::CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler)
{
    Log(logStream_, ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

    // 1. ファイル読み込み
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // 2. コンパイルオプション
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-Od", L"-Zpr"
    };

    // 3. コンパイル実行
    Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(&shaderResult)
    );
    assert(SUCCEEDED(hr));

    // 4. エラー確認
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(logStream_, shaderError->GetStringPointer());
        assert(false);
    }

    // 5. 結果取得
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));

    Log(logStream_, ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

    return shaderBlob;
}