#pragma once
#include "MathTypes.h"
#include <string>
#include <vector>

// 定数定義
const int kNumDirectionalLights = 1;
const int kNumPointLights = 3; // 任意に増やせますが、シェーダーと合わせる必要があります
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

struct DirectionalLight {
    Vector4 color;     // ライト色 (RGB) + 強度 (A) または 個別にIntensityを持つか
    Vector3 direction; // ライトの向き
    float intensity;   // 輝度強度
};

struct PointLight {
    Vector4 color;     // ライト色
    Vector3 position;  // 位置
    float intensity;   // 強度
    float radius;      // ライトが届く最大距離（減衰計算用）
    float decay;       // 減衰率（通常1.0）
    float padding[2];
};

struct SpotLight {
    Vector4 color;     // ライト色
    Vector3 position;  // 位置
    float intensity;   // 強度
    Vector3 direction; // 方向
    float distance;    // 届く距離
    float decay;       // 減衰率
    float cosAngle;    // スポットライトの余弦 (cos(theta))
    float cosFalloffStart; // フォールオフ開始角度の余弦
    float padding[2];
};

// 矩形ライト（AreaLight）簡易版
struct AreaLight {
    Vector4 color;     // ライト色
    Vector3 position;  // 中心位置
    float intensity;   // 強度
    Vector3 up;        // ライトの「上」方向ベクトル（高さの半分）
    float height;      // 高さ
    Vector3 right;     // ライトの「右」方向ベクトル（幅の半分）
    float width;       // 幅
    Vector3 direction; // 発光方向
    float decay;       // 減衰
};

// ライト統合データ（定数バッファ用）
// シェーダーの register(b1) に送るデータ
struct LightGroup {
    DirectionalLight directionalLights[kNumDirectionalLights];
    PointLight pointLights[kNumPointLights];
    SpotLight spotLights[kNumSpotLights];
    AreaLight areaLights[kNumAreaLights];

    // 有効なライトの数
    int32_t numDirectionalLights;
    int32_t numPointLights;
    int32_t numSpotLights;
    int32_t numAreaLights;
};

// カメラ情報
struct CameraForGpu {
    Vector3 worldPosition;
};

// モデル読み込み用マテリアルデータ
struct MaterialData {
    std::string textureFilePath;
};

// モデルデータ
struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};