#include "GraphicsPipeline.h"
#include "DataTypes.h"
#include <cassert>
#include <format>
#include <fstream>

// 外部で定義された関数のプロトタイプ宣言
void Log(std::ostream& os, const std::string& message);
std::string ConvertString(const std::wstring& str);

void GraphicsPipeline::Initialize(ID3D12Device* device) {
	logStream_.open("ShaderCompile.log");

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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

	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Skybox用にALLにするか、VSでも使うので制限緩和
	rootParameters[0].Descriptor.ShaderRegister = 0;

	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 1;
	descriptorRangeForInstancing[0].NumDescriptors = 1;
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	// 通常のパーティクル用
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Particle.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(vertexShaderBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Particle.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(pixelShaderBlob != nullptr);

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
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
	inputElementDescs[3].SemanticName = "BONEINDICES";
	inputElementDescs[3].SemanticIndex = 0;
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_UINT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[4].SemanticName = "BONEWEIGHTS";
	inputElementDescs[4].SemanticIndex = 0;
	inputElementDescs[4].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.FrontCounterClockwise = FALSE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	for (int i = 0; i < kCountOfBlendMode; ++i) {
		D3D12_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		switch (static_cast<BlendMode>(i)) {
		case kBlendModeNone: blendDesc.RenderTarget[0].BlendEnable = FALSE; break;
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
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case kBlendModeSubtract:
			blendDesc.RenderTarget[0].BlendEnable = TRUE;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
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
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case kBlendModeAlphaClip: blendDesc.RenderTarget[0].BlendEnable = FALSE; break;
		}

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

		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[static_cast<BlendMode>(i)]));
		assert(SUCCEEDED(hr));
	}

	// --- Skybox用パイプラインステートの作成 ---
	Microsoft::WRL::ComPtr<IDxcBlob> skyboxVSBlob = CompileShader(L"Skybox.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(skyboxVSBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> skyboxPSBlob = CompileShader(L"Skybox.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(skyboxPSBlob != nullptr);

	D3D12_INPUT_ELEMENT_DESC skyboxInputElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPsoDesc{};
	skyboxPsoDesc.pRootSignature = rootSignature_.Get();
	skyboxPsoDesc.InputLayout = { skyboxInputElements, _countof(skyboxInputElements) };
	skyboxPsoDesc.VS = { skyboxVSBlob->GetBufferPointer(), skyboxVSBlob->GetBufferSize() };
	skyboxPsoDesc.PS = { skyboxPSBlob->GetBufferPointer(), skyboxPSBlob->GetBufferSize() };

	D3D12_BLEND_DESC skyboxBlendDesc{};
	skyboxBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	skyboxBlendDesc.RenderTarget[0].BlendEnable = FALSE;
	skyboxPsoDesc.BlendState = skyboxBlendDesc;

	D3D12_RASTERIZER_DESC skyboxRasterizerDesc{};
	skyboxRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // 内側から見るためNONE
	skyboxRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	skyboxPsoDesc.RasterizerState = skyboxRasterizerDesc;

	D3D12_DEPTH_STENCIL_DESC skyboxDepthDesc{};
	skyboxDepthDesc.DepthEnable = true;
	skyboxDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度書き込みゼロ
	skyboxDepthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // 常に最遠方描画
	skyboxPsoDesc.DepthStencilState = skyboxDepthDesc;

	skyboxPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	skyboxPsoDesc.NumRenderTargets = 1;
	skyboxPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	skyboxPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyboxPsoDesc.SampleDesc.Count = 1;
	skyboxPsoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = device->CreateGraphicsPipelineState(&skyboxPsoDesc, IID_PPV_ARGS(&skyboxPipelineState_));
	assert(SUCCEEDED(hr));


	// ====================================================================
	// ★追加部分：Object3d（キャラクター等）用ルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_SIGNATURE_DESC obj3dRootDesc{};
	obj3dRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER obj3dParams[7] = {};

	// [0] Material (b0, PIXELシェーダー用)
	obj3dParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	obj3dParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	obj3dParams[0].Descriptor.ShaderRegister = 0;

	// [1] WVP行列などのTransformationMatrix (b1, VERTEXシェーダー用)
	obj3dParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	obj3dParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	obj3dParams[1].Descriptor.ShaderRegister = 1;

	// [2] テクスチャ (t0, PIXELシェーダー用)
	D3D12_DESCRIPTOR_RANGE obj3dTexRange[1] = {};
	obj3dTexRange[0].BaseShaderRegister = 0;
	obj3dTexRange[0].NumDescriptors = 1;
	obj3dTexRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	obj3dTexRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	obj3dParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	obj3dParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	obj3dParams[2].DescriptorTable.pDescriptorRanges = obj3dTexRange;
	obj3dParams[2].DescriptorTable.NumDescriptorRanges = 1;

	// [3] 環境マップテクスチャ (t1, PIXELシェーダー用)
	D3D12_DESCRIPTOR_RANGE obj3dEnvRange[1] = {};
	obj3dEnvRange[0].BaseShaderRegister = 1;
	obj3dEnvRange[0].NumDescriptors = 1;
	obj3dEnvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	obj3dEnvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	obj3dParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	obj3dParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	obj3dParams[3].DescriptorTable.pDescriptorRanges = obj3dEnvRange;
	obj3dParams[3].DescriptorTable.NumDescriptorRanges = 1;

	// [4] 平行光源 DirectionalLight (b2, PIXELシェーダー用)
	obj3dParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	obj3dParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	obj3dParams[4].Descriptor.ShaderRegister = 2;

	// [5] カメラ座標 Camera (b3, PIXELシェーダー用)
	obj3dParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	obj3dParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	obj3dParams[5].Descriptor.ShaderRegister = 3;

	// [6] SkinningPalette (b4, VERTEXシェーダー用)
	obj3dParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	obj3dParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	obj3dParams[6].Descriptor.ShaderRegister = 4;

	obj3dRootDesc.pParameters = obj3dParams;
	obj3dRootDesc.NumParameters = _countof(obj3dParams);
	obj3dRootDesc.pStaticSamplers = staticSamplers; // パーティクルと同じサンプラーを流用
	obj3dRootDesc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> obj3dSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> obj3dErrorBlob;
	hr = D3D12SerializeRootSignature(&obj3dRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &obj3dSignatureBlob, &obj3dErrorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(obj3dErrorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, obj3dSignatureBlob->GetBufferPointer(), obj3dSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&object3dRootSignature_));
	if (FAILED(hr)) {
		Log(logStream_, "Failed to Create Object3d RootSignature.\n");
		assert(false);
	}
	assert(SUCCEEDED(hr));

	// Object3d用シェーダーのコンパイル
	Microsoft::WRL::ComPtr<IDxcBlob> obj3dVSBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(obj3dVSBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> obj3dPSBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(obj3dPSBlob != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC obj3dPsoDesc{};
	obj3dPsoDesc.pRootSignature = object3dRootSignature_.Get();
	obj3dPsoDesc.InputLayout = inputLayoutDesc; // パーティクルと同じPOSITION, TEXCOORD, NORMALを使用
	obj3dPsoDesc.VS = { obj3dVSBlob->GetBufferPointer(), obj3dVSBlob->GetBufferSize() };
	obj3dPsoDesc.PS = { obj3dPSBlob->GetBufferPointer(), obj3dPSBlob->GetBufferSize() };

	D3D12_BLEND_DESC obj3dBlendDesc{};
	obj3dBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	obj3dBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	obj3dBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	obj3dBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	obj3dBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	obj3dBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	obj3dBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	obj3dBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	obj3dPsoDesc.BlendState = obj3dBlendDesc;

	// Zバッファ（深度）を有効化して、手前のものが奥を隠すようにする
	D3D12_DEPTH_STENCIL_DESC obj3dDepthDesc{};
	obj3dDepthDesc.DepthEnable = true;
	obj3dDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // モデルは深度を書き込む
	obj3dDepthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	obj3dPsoDesc.DepthStencilState = obj3dDepthDesc;

	obj3dPsoDesc.RasterizerState = rasterizerDesc;
	obj3dPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	obj3dPsoDesc.NumRenderTargets = 1;
	obj3dPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	obj3dPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	obj3dPsoDesc.SampleDesc.Count = 1;
	obj3dPsoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = device->CreateGraphicsPipelineState(&obj3dPsoDesc, IID_PPV_ARGS(&object3dPipelineState_));
	if (FAILED(hr)) {
		Log(logStream_, "Failed to Create Object3d PipelineState.\n");
		assert(false);
	}
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

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	Log(logStream_, ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

	return shaderBlob;
}