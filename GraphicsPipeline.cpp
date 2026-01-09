#include "GraphicsPipeline.h"
#include <cassert>
#include <format>
#include <fstream>
#include <string>
#include <vector>

// DXCライブラリのリンク指定
#pragma comment(lib, "dxcompiler.lib")

// CD3DX12系クラスを使うために必要
#include "externals/DirectXTex/d3dx12.h"

// =========================================================================
//  ▼ 修正箇所: ここを namespace { ... } で囲みました
//     これにより、main.cpp にある同名関数と衝突しなくなります (内部リンケージ化)
// =========================================================================
namespace {

    // string -> wstring
    std::wstring ConvertString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    // wstring -> string
    std::string ConvertString(const std::wstring& str) {
        if (str.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    void Log(std::ostream& os, const std::string& message) {
        os << message << std::endl;
        OutputDebugStringA(message.c_str());
    }
}
// =========================================================================


// --- GraphicsPipeline 実装 ---

void GraphicsPipeline::Initialize(ID3D12Device* device) {
    // ログファイルのオープン
    logStream_.open("ShaderCompile.log");

    // --- DXCの初期化 ---
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));

    // --- ルートシグネチャの作成 ---
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // パラメータ設定: Material, Light, Camera, Texture, Instancing
    // ★重要: ここで配列数を 5 に設定し、Instancing用のスロットを確保
    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    // 0. Material (b0) -> Pixel Shader
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // 1. DirectionalLight (b1) -> Pixel Shader
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 1;

    // 2. Camera (b2) -> Vertex & Pixel Shader
    // カメラ座標は頂点シェーダーでもピクセルシェーダーでも使う可能性があるため ALL が安全ですが、
    // 現状のコードに合わせて PIXEL になっています。
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].Descriptor.ShaderRegister = 2;

    // 3. Texture (t0) -> Pixel Shader
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0; // t0
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    // 4. Instancing StructuredBuffer (t1) -> Vertex Shader
    // ★ここが修正の肝です。インスタンシング用の行列データを受け取る設定
    D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing[1] = {};
    descriptorRangeInstancing[0].BaseShaderRegister = 1; // t1
    descriptorRangeInstancing[0].NumDescriptors = 1;
    descriptorRangeInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VSで読むのでVERTEX
    rootParameters[4].DescriptorTable.pDescriptorRanges = descriptorRangeInstancing;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInstancing);

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // サンプラー設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

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


    // --- パイプラインステート (PSO) の作成 ---
    // インプットレイアウト
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
    graphicsPipelineStateDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

    // シェーダーコンパイル
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(vertexShaderBlob != nullptr);
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };

    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(pixelShaderBlob != nullptr);
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

    // ラスタライザ設定
    graphicsPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    graphicsPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphicsPipelineStateDesc.RasterizerState.FrontCounterClockwise = FALSE;
    graphicsPipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    graphicsPipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    graphicsPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    graphicsPipelineStateDesc.RasterizerState.DepthClipEnable = TRUE;
    graphicsPipelineStateDesc.RasterizerState.MultisampleEnable = FALSE;
    graphicsPipelineStateDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    graphicsPipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
    graphicsPipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // デプスステンシル設定
    graphicsPipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
    graphicsPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    graphicsPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    graphicsPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    graphicsPipelineStateDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    graphicsPipelineStateDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    graphicsPipelineStateDesc.DepthStencilState.FrontFace = defaultStencilOp;
    graphicsPipelineStateDesc.DepthStencilState.BackFace = defaultStencilOp;

    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // ブレンドステート設定 (共通設定)
    D3D12_RENDER_TARGET_BLEND_DESC& blendDesc = graphicsPipelineStateDesc.BlendState.RenderTarget[0];
    blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.BlendEnable = TRUE;

    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // --- 各ブレンドモード用PSOの生成 ---

    // Normal
    blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeNormal]));
    assert(SUCCEEDED(hr));

    // None
    blendDesc.BlendEnable = FALSE;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeNone]));
    assert(SUCCEEDED(hr));

    // Add
    blendDesc.BlendEnable = TRUE;
    blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend = D3D12_BLEND_ONE;
    blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeAdd]));
    assert(SUCCEEDED(hr));

    // Subtract
    blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend = D3D12_BLEND_ONE;
    blendDesc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeSubtract]));
    assert(SUCCEEDED(hr));

    // Multiply
    blendDesc.SrcBlend = D3D12_BLEND_ZERO;
    blendDesc.DestBlend = D3D12_BLEND_SRC_COLOR;
    blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeMultiply]));
    assert(SUCCEEDED(hr));

    // AlphaClip
    blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[kBlendModeAlphaClip]));
    assert(SUCCEEDED(hr));
}

Microsoft::WRL::ComPtr<IDxcBlob> GraphicsPipeline::CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler)
{
    Log(logStream_, ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-Od", L"-Zpr"
    };

    Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
    hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(logStream_, shaderError->GetStringPointer());
        assert(false);
    }

    Microsoft::WRL::ComPtr<IDxcBlob> blob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob), nullptr);
    assert(SUCCEEDED(hr));

    Log(logStream_, ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
    return blob;
}