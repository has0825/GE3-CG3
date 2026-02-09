#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <string>
#include <vector>
#include <format>
#include <numbers> // PI用
#include <cmath>   // cos, sin用

#include "WinApp.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include "Model.h"
#include "GraphicsPipeline.h"
#include "MathUtil.h"
#include "DataTypes.h" 

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// 文字列変換
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

// 度数法をラジアンに変換
float ToRadians(float degrees) {
	return degrees * (std::numbers::pi_v<float> / 180.0f);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize(L"CG2");

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp);
	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// パイプライン
	GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
	graphicsPipeline->Initialize(device);

	// SRV Heap
	const UINT kMaxSRVCount = 128;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
		CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ---------------------------------------------------------
	// リソース読み込み
	// ---------------------------------------------------------

	// 1. Terrain Texture (Index 0)
	std::string textureFileTerrain = "Resources/terrain/grass.png";
	DirectX::ScratchImage imgTerrain = LoadTexture(textureFileTerrain);
	const DirectX::TexMetadata& metaTerrain = imgTerrain.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> texResTerrain = CreateTextureResource(device, metaTerrain);
	Microsoft::WRL::ComPtr<ID3D12Resource> uploadResTerrain = UploadTextureData(texResTerrain.Get(), imgTerrain, device, commandList);

	D3D12_CPU_DESCRIPTOR_HANDLE terrainSrvCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE terrainSrvGPU = srvHandleGPU;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTerrain{};
	srvDescTerrain.Format = metaTerrain.format;
	srvDescTerrain.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescTerrain.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescTerrain.Texture2D.MipLevels = UINT(metaTerrain.mipLevels);
	device->CreateShaderResourceView(texResTerrain.Get(), &srvDescTerrain, terrainSrvCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// 2. Ball Texture (Index 1)
	std::string textureFileBall = "Resources/monsterBall.png";
	DirectX::ScratchImage imgBall = LoadTexture(textureFileBall);
	const DirectX::TexMetadata& metaBall = imgBall.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> texResBall = CreateTextureResource(device, metaBall);
	Microsoft::WRL::ComPtr<ID3D12Resource> uploadResBall = UploadTextureData(texResBall.Get(), imgBall, device, commandList);

	D3D12_CPU_DESCRIPTOR_HANDLE ballSrvCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE ballSrvGPU = srvHandleGPU;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescBall{};
	srvDescBall.Format = metaBall.format;
	srvDescBall.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescBall.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescBall.Texture2D.MipLevels = UINT(metaBall.mipLevels);
	device->CreateShaderResourceView(texResBall.Get(), &srvDescBall, ballSrvCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// 3. ★追加: UV Checker Texture (Index 2)
	std::string textureFileChecker = "Resources/uvChecker.png";
	DirectX::ScratchImage imgChecker = LoadTexture(textureFileChecker);
	const DirectX::TexMetadata& metaChecker = imgChecker.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> texResChecker = CreateTextureResource(device, metaChecker);
	Microsoft::WRL::ComPtr<ID3D12Resource> uploadResChecker = UploadTextureData(texResChecker.Get(), imgChecker, device, commandList);

	D3D12_CPU_DESCRIPTOR_HANDLE checkerSrvCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE checkerSrvGPU = srvHandleGPU;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescChecker{};
	srvDescChecker.Format = metaChecker.format;
	srvDescChecker.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescChecker.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescChecker.Texture2D.MipLevels = UINT(metaChecker.mipLevels);
	device->CreateShaderResourceView(texResChecker.Get(), &srvDescChecker, checkerSrvCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// ---------------------------------------------------------
	// モデル生成
	// ---------------------------------------------------------

	// A. Terrain (OBJ)
	Model* terrainModel = Model::Create("Resources/terrain", "terrain.obj", device);

	// B. Sphere (Generated)
	Model* sphereModel = Model::CreateSphereModel(device, 16);

	// C. ★追加: Plane (glTF)
	// glTFファイルを読む
	Model* planeModel = Model::Create("Resources", "plane.gltf", device);


	// ---------------------------------------------------------
	// Transform Buffers
	// ---------------------------------------------------------

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTransform{};
	srvDescTransform.Format = DXGI_FORMAT_UNKNOWN;
	srvDescTransform.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescTransform.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDescTransform.Buffer.FirstElement = 0;
	srvDescTransform.Buffer.NumElements = 1;
	srvDescTransform.Buffer.StructureByteStride = sizeof(TransformationMatrix);
	srvDescTransform.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	// Terrain Transform (Index 3)
	Microsoft::WRL::ComPtr<ID3D12Resource> terrainTransformBuffer = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* terrainMapData = nullptr;
	terrainTransformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&terrainMapData));

	D3D12_CPU_DESCRIPTOR_HANDLE terrainTransCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE terrainTransGPU = srvHandleGPU;
	device->CreateShaderResourceView(terrainTransformBuffer.Get(), &srvDescTransform, terrainTransCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// Sphere Transform (Index 4)
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereTransformBuffer = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* sphereMapData = nullptr;
	sphereTransformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&sphereMapData));

	D3D12_CPU_DESCRIPTOR_HANDLE sphereTransCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE sphereTransGPU = srvHandleGPU;
	device->CreateShaderResourceView(sphereTransformBuffer.Get(), &srvDescTransform, sphereTransCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// ★追加: Plane Transform (Index 5)
	Microsoft::WRL::ComPtr<ID3D12Resource> planeTransformBuffer = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* planeMapData = nullptr;
	planeTransformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&planeMapData));

	D3D12_CPU_DESCRIPTOR_HANDLE planeTransCPU = srvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE planeTransGPU = srvHandleGPU;
	device->CreateShaderResourceView(planeTransformBuffer.Get(), &srvDescTransform, planeTransCPU);

	srvHandleCPU.ptr += descriptorSize;
	srvHandleGPU.ptr += descriptorSize;

	// ---------------------------------------------------------
	// Light Buffer
	// ---------------------------------------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> lightBuffer = CreateBufferResource(device, sizeof(LightGroup));
	LightGroup* lightData = nullptr;
	lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&lightData));

	// メモリを0クリア
	memset(lightData, 0, sizeof(LightGroup));

	// 1. Directional Light (太陽光)
	lightData->numDirectionalLights = 1;
	lightData->directionalLights[0].color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
	lightData->directionalLights[0].direction = { 0.0f, -1.0f, 0.0f }; // 真下
	lightData->directionalLights[0].intensity = 0.5f;

	// 2. Point Light (点光源)
	lightData->numPointLights = 1;
	lightData->pointLights[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightData->pointLights[0].position = { -2.0f, 1.0f, 0.0f };
	lightData->pointLights[0].intensity = 1.0f;
	lightData->pointLights[0].radius = 10.0f;
	lightData->pointLights[0].decay = 1.0f;

	// 3. Spot Light 初期化
	lightData->numSpotLights = 1;
	lightData->spotLights[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightData->spotLights[0].position = { 90.0f, 90.0f, 90.0f };
	lightData->spotLights[0].direction = Normalize({ 0.0f, -1.0f, 0.0f }); // 真下
	lightData->spotLights[0].distance = 0.1f;
	lightData->spotLights[0].intensity = 4.0f;
	lightData->spotLights[0].decay = 3.5f;
	lightData->spotLights[0].cosAngle = std::cos(ToRadians(45.0f));
	lightData->spotLights[0].cosFalloffStart = std::cos(ToRadians(30.0f));

	float spotAngleControl[2] = { -90.0f, 0.0f }; // 真下

	// Camera Buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer = CreateBufferResource(device, sizeof(CameraForGpu));
	CameraForGpu* cameraData = nullptr;
	cameraBuffer->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));

	// Material Buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer = CreateBufferResource(device, sizeof(Material));
	Material* materialData = nullptr;
	materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = 3; // Blinn-Phong
	materialData->shininess = 50.0f;
	materialData->uvTransform = MakeIdentity4x4();

	// ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(),
		dxCommon->GetRtvDesc().Format,
		srvDescriptorHeap.Get(),
		srvHandleCPU,
		srvHandleGPU);

	// ループ変数
	Vector3 cameraTranslate = { 0.0f, 5.0f, -15.0f };
	Vector3 cameraRotate = { 0.2f, 0.0f, 0.0f };

	while (true) {
		if (winApp->ProcessMessage()) break;

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// ---------------------------------------------------------
		// ImGui Controls
		// ---------------------------------------------------------
		ImGui::Begin("Lighting Controller");

		if (ImGui::BeginTabBar("LightTabs")) {

			// --- Tab 1: Directional Light ---
			if (ImGui::BeginTabItem("Directional (Sun)")) {
				ImGui::Checkbox("Enable Directional", (bool*)&lightData->numDirectionalLights);
				if (lightData->numDirectionalLights > 0) {
					ImGui::Text("Global Light Direction");
					ImGui::DragFloat3("Direction", &lightData->directionalLights[0].direction.x, 0.01f, -1.0f, 1.0f);
					lightData->directionalLights[0].direction = Normalize(lightData->directionalLights[0].direction);

					ImGui::ColorEdit4("Color", &lightData->directionalLights[0].color.x);
					ImGui::DragFloat("Intensity", &lightData->directionalLights[0].intensity, 0.01f, 0.0f, 5.0f);
				}
				ImGui::EndTabItem();
			}

			// --- Tab 2: Point Light ---
			if (ImGui::BeginTabItem("Point (Bulb)")) {
				ImGui::SliderInt("Num Lights", &lightData->numPointLights, 0, kNumPointLights);

				for (int i = 0; i < lightData->numPointLights; ++i) {
					ImGui::PushID(i);
					ImGui::Separator();
					ImGui::Text("Point Light %d", i);
					ImGui::DragFloat3("Position", &lightData->pointLights[i].position.x, 0.1f);

					ImGui::ColorEdit4("Color", &lightData->pointLights[i].color.x);
					ImGui::DragFloat("Intensity", &lightData->pointLights[i].intensity, 0.01f, 0.0f, 10.0f);
					ImGui::DragFloat("Radius", &lightData->pointLights[i].radius, 0.1f, 0.1f, 50.0f);
					ImGui::DragFloat("Decay", &lightData->pointLights[i].decay, 0.01f, 0.0f, 5.0f);
					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}

			// --- Tab 3: Spot Light (Flashlight) ---
			if (ImGui::BeginTabItem("Spot (Flashlight)")) {
				ImGui::Checkbox("Enable Spot", (bool*)&lightData->numSpotLights);

				for (int i = 0; i < lightData->numSpotLights; ++i) {
					ImGui::PushID(i);
					ImGui::Separator();
					ImGui::Text("Spot Light %d", i);

					// 1. 位置
					ImGui::DragFloat3("Position", &lightData->spotLights[i].position.x, 0.1f);

					// 2. 向き (角度で操作)
					ImGui::Text("Direction Angle Control");
					bool angleChanged = false;
					// Pitch: 上下 (-90度 〜 90度)
					if (ImGui::SliderFloat("Pitch (Up/Down)", &spotAngleControl[0], -89.0f, 89.0f)) angleChanged = true;
					// Yaw: 左右 (0度 〜 360度)
					if (ImGui::SliderFloat("Yaw (Left/Right)", &spotAngleControl[1], 0.0f, 360.0f)) angleChanged = true;

					if (angleChanged) {
						// 角度からベクトルを計算
						float radPitch = ToRadians(spotAngleControl[0]);
						float radYaw = ToRadians(spotAngleControl[1]);

						Vector3 dir;
						dir.y = std::sin(radPitch);
						float xzLen = std::cos(radPitch);
						dir.x = xzLen * std::sin(radYaw);
						dir.z = xzLen * std::cos(radYaw);

						lightData->spotLights[i].direction = Normalize(dir);
					}

					ImGui::Text("Result Direction: (%.2f, %.2f, %.2f)",
						lightData->spotLights[i].direction.x,
						lightData->spotLights[i].direction.y,
						lightData->spotLights[i].direction.z);

					// 3. 色と強さ
					ImGui::ColorEdit4("Color", &lightData->spotLights[i].color.x);
					ImGui::DragFloat("Intensity", &lightData->spotLights[i].intensity, 0.1f, 0.0f, 100.0f);

					// 4. 距離パラメータ
					ImGui::DragFloat("Distance", &lightData->spotLights[i].distance, 0.1f, 0.1f, 100.0f);
					ImGui::DragFloat("Decay", &lightData->spotLights[i].decay, 0.01f, 0.0f, 5.0f);

					// 5. 照射範囲 (円錐の角度)
					float outerAngleDeg = std::acos(lightData->spotLights[i].cosAngle) * 180.0f / std::numbers::pi_v<float>;
					float innerAngleDeg = std::acos(lightData->spotLights[i].cosFalloffStart) * 180.0f / std::numbers::pi_v<float>;

					bool coneChanged = false;
					ImGui::Text("Cone Angles");
					if (ImGui::DragFloat("Outer (Limit)", &outerAngleDeg, 0.5f, 0.1f, 89.0f)) coneChanged = true;
					if (ImGui::DragFloat("Inner (Full)", &innerAngleDeg, 0.5f, 0.0f, outerAngleDeg)) coneChanged = true;

					if (coneChanged) {
						if (innerAngleDeg > outerAngleDeg) innerAngleDeg = outerAngleDeg;
						lightData->spotLights[i].cosAngle = std::cos(ToRadians(outerAngleDeg));
						lightData->spotLights[i].cosFalloffStart = std::cos(ToRadians(innerAngleDeg));
					}

					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ImGui::Text("Camera Settings");
		ImGui::DragFloat3("Camera Trans", &cameraTranslate.x, 0.1f);
		ImGui::DragFloat3("Camera Rot", &cameraRotate.x, 0.01f);

		// Planeの操作用
		if (planeModel) {
			ImGui::Separator();
			ImGui::Text("Plane glTF Settings");
			ImGui::DragFloat3("Plane Trans", &planeModel->transform.translate.x, 0.1f);
			ImGui::DragFloat3("Plane Rotate", &planeModel->transform.rotate.x, 0.01f);
			ImGui::DragFloat3("Plane Scale", &planeModel->transform.scale.x, 0.01f);
		}

		ImGui::End();

		// ---------------------------------------------------------
		// 更新処理
		// ---------------------------------------------------------

		// Update Camera
		Matrix4x4 cameraWorld = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, cameraRotate, cameraTranslate);
		Matrix4x4 viewMatrix = Inverse(cameraWorld);
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)WinApp::kClientWidth / (float)WinApp::kClientHeight, 0.1f, 100.0f);
		Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
		cameraData->worldPosition = cameraTranslate;

		// 1. Update Terrain
		if (terrainModel) {
			terrainModel->transform.scale = { 1.0f, 1.0f, 1.0f };
			terrainModel->transform.rotate = { 0.0f, 0.0f, 0.0f };
			terrainModel->transform.translate = { 0.0f, -2.0f, 0.0f };

			Matrix4x4 worldMatrix = MakeAffineMatrix(terrainModel->transform.scale, terrainModel->transform.rotate, terrainModel->transform.translate);
			// ノードのローカル変換適用
			worldMatrix = Multiply(terrainModel->rootNode.localMatrix, worldMatrix);

			Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
			Matrix4x4 worldInv = Inverse(worldMatrix);
			Matrix4x4 worldInvT = Transpose(worldInv);

			terrainMapData->World = worldMatrix;
			terrainMapData->WVP = wvpMatrix;
			terrainMapData->WorldInverseTranspose = worldInvT;
		}

		// 2. Update Sphere
		if (sphereModel) {
			sphereModel->transform.scale = { 1.0f, 1.0f, 1.0f };
			sphereModel->transform.rotate = { 0.0f, 0.0f, 0.0f };
			sphereModel->transform.translate = { 0.0f, -1.0f, 0.0f };

			Matrix4x4 worldMatrix = MakeAffineMatrix(sphereModel->transform.scale, sphereModel->transform.rotate, sphereModel->transform.translate);
			// ノードのローカル変換適用
			worldMatrix = Multiply(sphereModel->rootNode.localMatrix, worldMatrix);

			Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
			Matrix4x4 worldInv = Inverse(worldMatrix);
			Matrix4x4 worldInvT = Transpose(worldInv);

			sphereMapData->World = worldMatrix;
			sphereMapData->WVP = wvpMatrix;
			sphereMapData->WorldInverseTranspose = worldInvT;
		}

		// 3. ★追加: Update Plane
		if (planeModel) {
			// 初期位置 (ImGuiで動かせるように初期化は最初だけにするか、ImGui変数を反映するか)
			// ここではtransformメンバ変数がImGuiで直接書き換わるので、
			// 毎フレーム固定値を代入しないように注意する（初期化時のみセットするのが理想だが、簡易的に0チェック等）
			if (planeModel->transform.scale.x == 1.0f && planeModel->transform.translate.y == 0.0f) {
				planeModel->transform.translate = { 0.0f, 2.0f, 0.0f }; // 少し浮かす
			}

			Matrix4x4 worldMatrix = MakeAffineMatrix(planeModel->transform.scale, planeModel->transform.rotate, planeModel->transform.translate);
			// ノードのローカル変換適用 (glTFのノード階層)
			worldMatrix = Multiply(planeModel->rootNode.localMatrix, worldMatrix);

			Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
			Matrix4x4 worldInv = Inverse(worldMatrix);
			Matrix4x4 worldInvT = Transpose(worldInv);

			planeMapData->World = worldMatrix;
			planeMapData->WVP = wvpMatrix;
			planeMapData->WorldInverseTranspose = worldInvT;
		}

		// ---------------------------------------------------------
		// 描画処理
		// ---------------------------------------------------------
		dxCommon->PreDraw();

		commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
		commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeNormal));
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, descriptorHeaps);

		// 共通バッファ
		commandList->SetGraphicsRootConstantBufferView(0, materialBuffer->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(1, lightBuffer->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(2, cameraBuffer->GetGPUVirtualAddress());

		// 1. Terrain Draw (Grass Texture)
		if (terrainModel) {
			commandList->SetGraphicsRootDescriptorTable(3, terrainSrvGPU);   // Texture
			commandList->SetGraphicsRootDescriptorTable(4, terrainTransGPU); // Transform
			terrainModel->Draw(commandList, 1, terrainSrvGPU, terrainTransGPU);
		}

		// 2. Sphere Draw (Ball Texture)
		if (sphereModel) {
			commandList->SetGraphicsRootDescriptorTable(3, ballSrvGPU);      // Texture
			commandList->SetGraphicsRootDescriptorTable(4, sphereTransGPU);  // Transform
			sphereModel->Draw(commandList, 1, ballSrvGPU, sphereTransGPU);
		}

		// 3. ★追加: Plane Draw (Checker Texture)
		if (planeModel) {
			// テクスチャにCheckerを指定
			commandList->SetGraphicsRootDescriptorTable(3, checkerSrvGPU);   // Texture
			commandList->SetGraphicsRootDescriptorTable(4, planeTransGPU);   // Transform
			planeModel->Draw(commandList, 1, checkerSrvGPU, planeTransGPU);
		}

		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

		dxCommon->PostDraw();
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	delete terrainModel;
	delete sphereModel;
	delete planeModel; // ★忘れず削除
	delete graphicsPipeline;

	dxCommon->Finalize();
	winApp->Finalize();

	return 0;
}