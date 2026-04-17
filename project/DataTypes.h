#pragma once
#include "engine/Math/MathTypes.h"
#include <string>
#include <vector>
#include <cstdint>
// 頂点データ
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

// マテリアル
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;

	// ★以下の2つを追加
	float shininess;
	float environmentCoefficient;
};

// 座標変換行列
struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
	// ★修正点: カメラ座標をここから削除
};

// ★修正点: カメラ専用の構造体を追加
struct CameraForGpu {
	Vector3 worldPosition;
};

// 平行光源
struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

// モデルのマテリアル情報
struct MaterialData {
	std::string textureFilePath;
};

// モデルデータ
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

// シェーダーの struct TransformationMatrix に対応
struct TransformationMatrixForGPU {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

// シェーダーの struct Material に対応
struct MaterialForGPU {
	Vector4 color;            // 16バイト (float x 4)
	int32_t enableLighting;   // 4バイト
	float padding[3];         // 12バイトの余白（※HLSLとのバイト数を合わせるための必須パディングです！）
	Matrix4x4 uvTransform;    // 64バイト
};

// シェーダーの struct DirectionalLight に対応
struct DirectionalLightForGPU {
	Vector4 color;            // 16バイト
	Vector3 direction;        // 12バイト
	float intensity;          // 4バイト
};