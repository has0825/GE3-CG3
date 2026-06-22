#pragma once
#include "engine/Math/MathTypes.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// 頂点データ
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
	uint32_t jointIndices[4];
	float jointWeights[4];
};

// 頂点データ（スキニング用）
struct VertexDataSkinning {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
	float weight[4];
	uint32_t index[4];
};

struct VertexInfluence {
	float weight[4];
	uint32_t index[4];
};

struct SkinningInformation {
	uint32_t numVertices;
};

struct VertexWeightData {
	float weight;
	uint32_t vertexIndex;
};

struct JointWeightData {
	Matrix4x4 inverseBindPoseMatrix;
	std::vector<VertexWeightData> vertexWeights;
};

// マテリアルデータ（Constant Buffer用）
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
	float shininess;
	float environmentCoefficient;
	float padding2[2];
};

// マテリアルデータ（アプリ管理用）
struct MaterialData {
	std::string textureFilePath;
};

// モデルデータ
struct Bone {
	std::string name;
	uint32_t index;
	Matrix4x4 offsetMatrix;

	Bone() : index(0), offsetMatrix({}) {}
};

struct ModelData {
	std::vector<VertexData> vertices;
	std::vector<uint32_t> indices;
	MaterialData material;
	std::map<std::string, JointWeightData> skinClusterData;
};

// シェーダーの struct TransformationMatrix に対応
struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

// 平行光源
struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
	
	// スポットライト拡張用
	int enableSpotLight;
	Vector3 spotLightPos;
	float spotLightRange;
	Vector3 spotLightDir;
	float spotLightCosAngle;
	Vector3 spotLightColor;
	float spotLightIntensity;
	float padding[3];
};


// カメラ（CBuffer用）
struct CameraData {
	Vector3 worldPosition;
};