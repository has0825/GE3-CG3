#pragma once
#include "MathTypes.h"
#include <string>
#include <vector>

// 定数定義
const int kNumDirectionalLights = 1;
const int kNumPointLights = 3;
const int kNumSpotLights = 3;
const int kNumAreaLights = 1;

// 頂点データ
struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
};

// マテリアルデータ
struct Material {
    Vector4 color;
    int32_t enableLighting;
    float shininess;      // 光沢度
    float padding[2];     // パディング
    Matrix4x4 uvTransform;
};

// 座標変換行列データ
struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose;
};

// --- ライト構造体 ---

// 構造体定義 (GPUのメモリレイアウト(16バイト境界)に合わせるためパディングを追加)
struct DirectionalLight {
    Vector4 color;
    Vector3 direction;
    float intensity;
};

struct PointLight {
    Vector4 color;
    Vector3 position;
    float intensity;
    float radius;
    float decay;
    float padding[2]; // ★追加: 16バイトアライメント用
};

struct SpotLight {
    Vector4 color;
    Vector3 position;
    float intensity;
    Vector3 direction;
    float distance;
    float decay;
    float cosAngle;
    float padding[2]; // ★追加: これがないと次のライトデータが壊れる
};


// 矩形ライト（AreaLight）
struct AreaLight {
    Vector4 color;     // ライト色
    Vector3 position;  // 中心位置
    float intensity;   // 強度
    Vector3 up;        // ライトの「上」方向ベクトル
    float height;      // 高さ
    Vector3 right;     // ライトの「右」方向ベクトル
    float width;       // 幅
    Vector3 direction; // 発光方向
    float decay;       // 減衰
};

// ライト統合データ（定数バッファ用）
struct LightGroup {
    DirectionalLight directionalLights[kNumDirectionalLights];
    PointLight pointLights[kNumPointLights];
    SpotLight spotLights[kNumSpotLights];
    int32_t numDirectionalLights;
    int32_t numPointLights;
    int32_t numSpotLights;
    int32_t padding; // 調整用
};

// カメラ情報 (元々あった構造体を復元)
struct CameraForGpu {
    Vector3 worldPosition;
    float padding;
};

// モデル読み込み用マテリアルデータ (元々あった構造体を復元)
struct MaterialData {
    std::string textureFilePath;
};

// モデルデータ (元々あった構造体を復元)
struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};