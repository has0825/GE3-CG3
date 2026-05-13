#include "GraphicsPipeline.h"
#include "DataTypes.h"
#include <cassert>
#include <format>
#include <fstream>

// 外部で定義された関数のプロトタイプ宣言
void Log(std::ostream& os, const std::string& message);
std::string ConvertString(const std::wstring& str);

GraphicsPipeline* GraphicsPipeline::GetInstance() {
	static GraphicsPipeline instance;
	return &instance;
}

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
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

	// --- Skinning用パイプラインの作成 ---
	D3D12_ROOT_SIGNATURE_DESC skinningRootDesc{};
	skinningRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER skinningParams[7] = {};
	// [0] Material (b0, PS)
	skinningParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	skinningParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	skinningParams[0].Descriptor.ShaderRegister = 0;
	// [1] TransformationMatrix (b0, VS)
	skinningParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	skinningParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	skinningParams[1].Descriptor.ShaderRegister = 0;
	// [2] MatrixPalette (t0, VS)
	static D3D12_DESCRIPTOR_RANGE paletteRange[1] = {};
	paletteRange[0].BaseShaderRegister = 0;
	paletteRange[0].NumDescriptors = 1;
	paletteRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	paletteRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	skinningParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	skinningParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	skinningParams[2].DescriptorTable.pDescriptorRanges = paletteRange;
	skinningParams[2].DescriptorTable.NumDescriptorRanges = 1;
	// [3] Texture (t0, PS)
	static D3D12_DESCRIPTOR_RANGE skinningTexRange[1] = {};
	skinningTexRange[0].BaseShaderRegister = 0;
	skinningTexRange[0].NumDescriptors = 1;
	skinningTexRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	skinningTexRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	skinningParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	skinningParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	skinningParams[3].DescriptorTable.pDescriptorRanges = skinningTexRange;
	skinningParams[3].DescriptorTable.NumDescriptorRanges = 1;
	// [4] Environment Map (t1, PS)
	static D3D12_DESCRIPTOR_RANGE skinningEnvRange[1] = {};
	skinningEnvRange[0].BaseShaderRegister = 1;
	skinningEnvRange[0].NumDescriptors = 1;
	skinningEnvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	skinningEnvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	skinningParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	skinningParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	skinningParams[4].DescriptorTable.pDescriptorRanges = skinningEnvRange;
	skinningParams[4].DescriptorTable.NumDescriptorRanges = 1;
	// [5] DirectionalLight (b2, PS)
	skinningParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	skinningParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	skinningParams[5].Descriptor.ShaderRegister = 2;
	// [6] Camera (b3, PS)
	skinningParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	skinningParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	skinningParams[6].Descriptor.ShaderRegister = 3;

	skinningRootDesc.pParameters = skinningParams;
	skinningRootDesc.NumParameters = _countof(skinningParams);
	skinningRootDesc.pStaticSamplers = staticSamplers;
	skinningRootDesc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> skinningSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> skinningErrorBlob;
	hr = D3D12SerializeRootSignature(&skinningRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &skinningSignatureBlob, &skinningErrorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(skinningErrorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, skinningSignatureBlob->GetBufferPointer(), skinningSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&skinningRootSignature_));
	assert(SUCCEEDED(hr));

	// Skinning用シェーダーのコンパイル
	Microsoft::WRL::ComPtr<IDxcBlob> skinningVSBlob = CompileShader(L"SkinningPost.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(skinningVSBlob != nullptr);

	// Skinning用InputLayoutの拡張 (CSでスキニング済みの頂点を受け取るため、ボーン情報は不要)
	D3D12_INPUT_ELEMENT_DESC skinningInputElements[3] = {};
	skinningInputElements[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	skinningInputElements[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	skinningInputElements[2] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinningPsoDesc = obj3dPsoDesc; // ベースは同じ
	skinningPsoDesc.pRootSignature = skinningRootSignature_.Get();
	skinningPsoDesc.InputLayout = { skinningInputElements, _countof(skinningInputElements) };
	skinningPsoDesc.VS = { skinningVSBlob->GetBufferPointer(), skinningVSBlob->GetBufferSize() };
	// PSは共通（Object3d.PS.hlsl）

	hr = device->CreateGraphicsPipelineState(&skinningPsoDesc, IID_PPV_ARGS(&skinningPipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：Compute Shader（スキニング用）のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER computeParams[3] = {};

	// [0] t0, t1, t2 (MatrixPalette, InputVertices, Influences)
	D3D12_DESCRIPTOR_RANGE computeSrvRanges[1] = {};
	computeSrvRanges[0].BaseShaderRegister = 0;
	computeSrvRanges[0].NumDescriptors = 3;
	computeSrvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	computeSrvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	computeParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	computeParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	computeParams[0].DescriptorTable.pDescriptorRanges = computeSrvRanges;
	computeParams[0].DescriptorTable.NumDescriptorRanges = 1;

	// [1] u0 (OutputVertices)
	D3D12_DESCRIPTOR_RANGE computeUavRanges[1] = {};
	computeUavRanges[0].BaseShaderRegister = 0;
	computeUavRanges[0].NumDescriptors = 1;
	computeUavRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	computeUavRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	computeParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	computeParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	computeParams[1].DescriptorTable.pDescriptorRanges = computeUavRanges;
	computeParams[1].DescriptorTable.NumDescriptorRanges = 1;

	// [2] b0 (SkinningInformation)
	computeParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	computeParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	computeParams[2].Descriptor.ShaderRegister = 0;

	D3D12_ROOT_SIGNATURE_DESC computeRootDesc{};
	computeRootDesc.pParameters = computeParams;
	computeRootDesc.NumParameters = _countof(computeParams);
	computeRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> computeSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> computeErrorBlob;
	hr = D3D12SerializeRootSignature(&computeRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &computeSignatureBlob, &computeErrorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(computeErrorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, computeSignatureBlob->GetBufferPointer(), computeSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature_));
	assert(SUCCEEDED(hr));

	// CSのコンパイル
	Microsoft::WRL::ComPtr<IDxcBlob> skinningCSBlob = CompileShader(L"Skinning.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(skinningCSBlob != nullptr);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc{};
	computePsoDesc.pRootSignature = computeRootSignature_.Get();
	computePsoDesc.CS = { skinningCSBlob->GetBufferPointer(), skinningCSBlob->GetBufferSize() };

	hr = device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&skinningComputePipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：GPU Particle（初期化用）のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER gpuParticleInitParams[1] = {};
	// [0] u0 (gParticles), u1 (gFreeListIndex), u2 (gFreeList)
	D3D12_DESCRIPTOR_RANGE gpuParticleInitUavRange[1] = {};
	gpuParticleInitUavRange[0].BaseShaderRegister = 0;
	gpuParticleInitUavRange[0].NumDescriptors = 3;
	gpuParticleInitUavRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	gpuParticleInitUavRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	gpuParticleInitParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	gpuParticleInitParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	gpuParticleInitParams[0].DescriptorTable.pDescriptorRanges = gpuParticleInitUavRange;
	gpuParticleInitParams[0].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_ROOT_SIGNATURE_DESC gpuParticleInitRootDesc{};
	gpuParticleInitRootDesc.pParameters = gpuParticleInitParams;
	gpuParticleInitRootDesc.NumParameters = _countof(gpuParticleInitParams);
	gpuParticleInitRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> gpuParticleInitSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> gpuParticleInitErrorBlob;
	hr = D3D12SerializeRootSignature(&gpuParticleInitRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &gpuParticleInitSignatureBlob, &gpuParticleInitErrorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(gpuParticleInitErrorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, gpuParticleInitSignatureBlob->GetBufferPointer(), gpuParticleInitSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&gpuParticleInitializeRootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleInitCSBlob = CompileShader(L"InitializeParticle.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(gpuParticleInitCSBlob != nullptr);

	D3D12_COMPUTE_PIPELINE_STATE_DESC gpuParticleInitPsoDesc{};
	gpuParticleInitPsoDesc.pRootSignature = gpuParticleInitializeRootSignature_.Get();
	gpuParticleInitPsoDesc.CS = { gpuParticleInitCSBlob->GetBufferPointer(), gpuParticleInitCSBlob->GetBufferSize() };

	hr = device->CreateComputePipelineState(&gpuParticleInitPsoDesc, IID_PPV_ARGS(&gpuParticleInitializePipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：GPU Particle（描画用）のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER gpuParticleRenderParams[3] = {};
	// [0] b0 (PerView)
	gpuParticleRenderParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	gpuParticleRenderParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	gpuParticleRenderParams[0].Descriptor.ShaderRegister = 0;

	// [1] t0 (gParticles)
	D3D12_DESCRIPTOR_RANGE gpuParticleRenderSrvRange[1] = {};
	gpuParticleRenderSrvRange[0].BaseShaderRegister = 0;
	gpuParticleRenderSrvRange[0].NumDescriptors = 1;
	gpuParticleRenderSrvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	gpuParticleRenderSrvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	gpuParticleRenderParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	gpuParticleRenderParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	gpuParticleRenderParams[1].DescriptorTable.pDescriptorRanges = gpuParticleRenderSrvRange;
	gpuParticleRenderParams[1].DescriptorTable.NumDescriptorRanges = 1;

	// [2] t1 (Texture - Optional but added for flexibility)
	D3D12_DESCRIPTOR_RANGE gpuParticleTexRange[1] = {};
	gpuParticleTexRange[0].BaseShaderRegister = 1;
	gpuParticleTexRange[0].NumDescriptors = 1;
	gpuParticleTexRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	gpuParticleTexRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	gpuParticleRenderParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	gpuParticleRenderParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	gpuParticleRenderParams[2].DescriptorTable.pDescriptorRanges = gpuParticleTexRange;
	gpuParticleRenderParams[2].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_ROOT_SIGNATURE_DESC gpuParticleRenderRootDesc{};
	gpuParticleRenderRootDesc.pParameters = gpuParticleRenderParams;
	gpuParticleRenderRootDesc.NumParameters = _countof(gpuParticleRenderParams);
	gpuParticleRenderRootDesc.pStaticSamplers = staticSamplers;
	gpuParticleRenderRootDesc.NumStaticSamplers = 1;
	gpuParticleRenderRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> gpuParticleRenderSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> gpuParticleRenderErrorBlob;
	hr = D3D12SerializeRootSignature(&gpuParticleRenderRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &gpuParticleRenderSignatureBlob, &gpuParticleRenderErrorBlob);
	if (FAILED(hr)) {
		Log(logStream_, reinterpret_cast<char*>(gpuParticleRenderErrorBlob->GetBufferPointer()));
		assert(false);
	}
	hr = device->CreateRootSignature(0, gpuParticleRenderSignatureBlob->GetBufferPointer(), gpuParticleRenderSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&gpuParticleRootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleVSBlob = CompileShader(L"GpuParticle.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(gpuParticleVSBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePSBlob = CompileShader(L"GpuParticle.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(gpuParticlePSBlob != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpuParticlePsoDesc{};
	gpuParticlePsoDesc.pRootSignature = gpuParticleRootSignature_.Get();
	gpuParticlePsoDesc.VS = { gpuParticleVSBlob->GetBufferPointer(), gpuParticleVSBlob->GetBufferSize() };
	gpuParticlePsoDesc.PS = { gpuParticlePSBlob->GetBufferPointer(), gpuParticlePSBlob->GetBufferSize() };
	
	// InputLayout (POSITION, TEXCOORD)
	D3D12_INPUT_ELEMENT_DESC gpuParticleInputElements[2] = {};
	gpuParticleInputElements[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	gpuParticleInputElements[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	gpuParticlePsoDesc.InputLayout = { gpuParticleInputElements, _countof(gpuParticleInputElements) };

	// Use Additive blend state
	{
		D3D12_BLEND_DESC additiveBlendDesc{};
		additiveBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		additiveBlendDesc.RenderTarget[0].BlendEnable = TRUE;
		additiveBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		additiveBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		additiveBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		additiveBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		additiveBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		additiveBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		gpuParticlePsoDesc.BlendState = additiveBlendDesc;
	}

	gpuParticlePsoDesc.RasterizerState = rasterizerDesc;
	gpuParticlePsoDesc.DepthStencilState = depthStencilDesc;
	gpuParticlePsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	gpuParticlePsoDesc.NumRenderTargets = 1;
	gpuParticlePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	gpuParticlePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpuParticlePsoDesc.SampleDesc.Count = 1;
	gpuParticlePsoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = device->CreateGraphicsPipelineState(&gpuParticlePsoDesc, IID_PPV_ARGS(&gpuParticlePipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：GPU Particle（射出用）のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER emitParams[3] = {};
	// [0] u0 (gParticles), u1 (gFreeListIndex), u2 (gFreeList)
	D3D12_DESCRIPTOR_RANGE emitUavRange[1] = {};
	emitUavRange[0].BaseShaderRegister = 0;
	emitUavRange[0].NumDescriptors = 3;
	emitUavRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	emitUavRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	emitParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	emitParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	emitParams[0].DescriptorTable.pDescriptorRanges = emitUavRange;
	emitParams[0].DescriptorTable.NumDescriptorRanges = 1;

	// [1] b0 (gEmitter)
	emitParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	emitParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	emitParams[1].Descriptor.ShaderRegister = 0;

	// [2] b1 (gPerFrame)
	emitParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	emitParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	emitParams[2].Descriptor.ShaderRegister = 1;

	D3D12_ROOT_SIGNATURE_DESC emitRootDesc{};
	emitRootDesc.pParameters = emitParams;
	emitRootDesc.NumParameters = _countof(emitParams);
	emitRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> emitSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> emitErrorBlob;
	hr = D3D12SerializeRootSignature(&emitRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &emitSignatureBlob, &emitErrorBlob);
	assert(SUCCEEDED(hr));
	hr = device->CreateRootSignature(0, emitSignatureBlob->GetBufferPointer(), emitSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&gpuParticleEmitRootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> emitCSBlob = CompileShader(L"EmitParticle.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(emitCSBlob != nullptr);

	D3D12_COMPUTE_PIPELINE_STATE_DESC emitPsoDesc{};
	emitPsoDesc.pRootSignature = gpuParticleEmitRootSignature_.Get();
	emitPsoDesc.CS = { emitCSBlob->GetBufferPointer(), emitCSBlob->GetBufferSize() };
	hr = device->CreateComputePipelineState(&emitPsoDesc, IID_PPV_ARGS(&gpuParticleEmitPipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：GPU Particle（更新用）のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER updateParams[2] = {};
	// [0] u0 (gParticles), u1 (gFreeListIndex), u2 (gFreeList)
	D3D12_DESCRIPTOR_RANGE updateUavRange[1] = {};
	updateUavRange[0].BaseShaderRegister = 0;
	updateUavRange[0].NumDescriptors = 3;
	updateUavRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	updateUavRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	updateParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	updateParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	updateParams[0].DescriptorTable.pDescriptorRanges = updateUavRange;
	updateParams[0].DescriptorTable.NumDescriptorRanges = 1;

	// [1] b0 (gPerFrame)
	updateParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	updateParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	updateParams[1].Descriptor.ShaderRegister = 0;

	D3D12_ROOT_SIGNATURE_DESC updateRootDesc{};
	updateRootDesc.pParameters = updateParams;
	updateRootDesc.NumParameters = _countof(updateParams);
	updateRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> updateSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> updateErrorBlob;
	hr = D3D12SerializeRootSignature(&updateRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &updateSignatureBlob, &updateErrorBlob);
	assert(SUCCEEDED(hr));
	hr = device->CreateRootSignature(0, updateSignatureBlob->GetBufferPointer(), updateSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&gpuParticleUpdateRootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> updateCSBlob = CompileShader(L"UpdateParticle.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(updateCSBlob != nullptr);

	D3D12_COMPUTE_PIPELINE_STATE_DESC updatePsoDesc{};
	updatePsoDesc.pRootSignature = gpuParticleUpdateRootSignature_.Get();
	updatePsoDesc.CS = { updateCSBlob->GetBufferPointer(), updateCSBlob->GetBufferSize() };
	hr = device->CreateComputePipelineState(&updatePsoDesc, IID_PPV_ARGS(&gpuParticleUpdatePipelineState_));
	assert(SUCCEEDED(hr));

	// ====================================================================
	// ★追加部分：CopyImage（フルスクリーン三角形コピー）用のルートシグネチャ＆パイプライン
	// ====================================================================

	D3D12_ROOT_PARAMETER copyImageParams[1] = {};
	D3D12_DESCRIPTOR_RANGE copyImageRange[1] = {};
	copyImageRange[0].BaseShaderRegister = 0;
	copyImageRange[0].NumDescriptors = 1;
	copyImageRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	copyImageRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	copyImageParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	copyImageParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	copyImageParams[0].DescriptorTable.pDescriptorRanges = copyImageRange;
	copyImageParams[0].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_STATIC_SAMPLER_DESC copySamplerDesc{};
	copySamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	copySamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	copySamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	copySamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	copySamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	copySamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	copySamplerDesc.ShaderRegister = 0;
	copySamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC copyRootDesc{};
	copyRootDesc.pParameters = copyImageParams;
	copyRootDesc.NumParameters = _countof(copyImageParams);
	copyRootDesc.pStaticSamplers = &copySamplerDesc;
	copyRootDesc.NumStaticSamplers = 1;
	copyRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> copySignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> copyErrorBlob;
	hr = D3D12SerializeRootSignature(&copyRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &copySignatureBlob, &copyErrorBlob);
	assert(SUCCEEDED(hr));
	hr = device->CreateRootSignature(0, copySignatureBlob->GetBufferPointer(), copySignatureBlob->GetBufferSize(), IID_PPV_ARGS(&copyImageRootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> fullscreenVSBlob = CompileShader(L"Fullscreen.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(fullscreenVSBlob != nullptr);
	
    // CopyImage (Normal)
    Microsoft::WRL::ComPtr<IDxcBlob> copyPSBlob = CompileShader(L"CopyImage.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(copyPSBlob != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC copyPsoDesc{};
	copyPsoDesc.pRootSignature = copyImageRootSignature_.Get();
	copyPsoDesc.VS = { fullscreenVSBlob->GetBufferPointer(), fullscreenVSBlob->GetBufferSize() };
	copyPsoDesc.PS = { copyPSBlob->GetBufferPointer(), copyPSBlob->GetBufferSize() };
	
	// InputLayoutは利用しない
	copyPsoDesc.InputLayout = { nullptr, 0 };

	// BlendState: 無し
	D3D12_BLEND_DESC copyBlendDesc{};
	copyBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	copyBlendDesc.RenderTarget[0].BlendEnable = FALSE;
	copyPsoDesc.BlendState = copyBlendDesc;

	// RasterizerState: 裏面も描画するようにカリングなし
	D3D12_RASTERIZER_DESC copyRasterizerDesc{};
	copyRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	copyRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	copyPsoDesc.RasterizerState = copyRasterizerDesc;

	// DepthStencilState: 深度テストなし
	D3D12_DEPTH_STENCIL_DESC copyDepthDesc{};
	copyDepthDesc.DepthEnable = FALSE;
	copyPsoDesc.DepthStencilState = copyDepthDesc;

	copyPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	copyPsoDesc.NumRenderTargets = 1;
	copyPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // スワップチェーン（RTV）に合わせてSRGBあり
	copyPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	copyPsoDesc.SampleDesc.Count = 1;
	copyPsoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = device->CreateGraphicsPipelineState(&copyPsoDesc, IID_PPV_ARGS(&copyImagePipelineState_));
	assert(SUCCEEDED(hr));

    // Grayscale
    Microsoft::WRL::ComPtr<IDxcBlob> grayscalePSBlob = CompileShader(L"Grayscale.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(grayscalePSBlob != nullptr);
    copyPsoDesc.PS = { grayscalePSBlob->GetBufferPointer(), grayscalePSBlob->GetBufferSize() };
    hr = device->CreateGraphicsPipelineState(&copyPsoDesc, IID_PPV_ARGS(&grayscalePipelineState_));
    assert(SUCCEEDED(hr));

    // Sepia
    Microsoft::WRL::ComPtr<IDxcBlob> sepiaPSBlob = CompileShader(L"Sepia.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
    assert(sepiaPSBlob != nullptr);
    copyPsoDesc.PS = { sepiaPSBlob->GetBufferPointer(), sepiaPSBlob->GetBufferSize() };
    hr = device->CreateGraphicsPipelineState(&copyPsoDesc, IID_PPV_ARGS(&sepiaPipelineState_));
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