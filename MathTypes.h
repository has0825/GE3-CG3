#pragma once
#include <string>
#include <vector>
#include <cstdint> // uint32_t, int32_t のために必要

// 数学型 (ここでは簡易的な定義を仮定)
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Vector4 { float x, y, z, w; };

struct Matrix4x4 {
	float m[4][4];
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

// =========================================================
// GPU/CPU 共通の構造体定義 (DataTypes.h の内容を一部含む)
// =========================================================

// (他の構造体は変更なし)
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};
struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};
struct CameraForGpu {
	Vector3 worldPosition;
};

// ★修正点: color と intensity メンバを元に戻す
struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

// (以降の構造体は変更なし)
struct MaterialData {
	std::string textureFilePath;
};
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};