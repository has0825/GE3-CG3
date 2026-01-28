#pragma once
#include "MathTypes.h"
#include <string>
#include <vector>

// 定数定義
const int kNumDirectionalLights = 1;
const int kNumPointLights = 3;
const int kNumSpotLights = 1;

struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
};

struct Material {
    Vector4 color;
    int32_t enableLighting;
    float shininess;
    float padding[2];
    Matrix4x4 uvTransform;
};

struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose;
};

// --- ライト構造体 ---

// 32バイト (16の倍数なのでOK)
struct DirectionalLight {
    Vector4 color;      // 16
    Vector3 direction;  // 12
    float intensity;    // 4
};

// 40バイト + 8バイトパディング = 48バイト (16の倍数に合わせる)
struct PointLight {
    Vector4 color;      // 16
    Vector3 position;   // 12
    float intensity;    // 4
    float radius;       // 4
    float decay;        // 4
    float padding[2];   // 8 (合計48バイト)
};

struct SpotLight {
    Vector4 color;
    Vector3 position;
    float intensity;
    Vector3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart; // Falloff開始角度を追加
    float padding;         // 16バイト境界合わせ
};

struct LightGroup {
    DirectionalLight directionalLights[kNumDirectionalLights];
    PointLight pointLights[kNumPointLights];
    SpotLight spotLights[kNumSpotLights]; // スポットライト追加
    int32_t numDirectionalLights;
    int32_t numPointLights;
    int32_t numSpotLights;                // 数を保持
    float padding;
};

struct CameraForGpu {
    Vector3 worldPosition;
    float padding;
};

struct MaterialData {
    std::string textureFilePath;
};

struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};