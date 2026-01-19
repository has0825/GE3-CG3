#pragma once
#include "MathTypes.h"
#include <string>
#include <vector>

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
    float shininess;      // ★追加: 光沢度
    float padding[2];     // パディング調整 (合計16バイトになるように)
    Matrix4x4 uvTransform;
};

// 座標変換行列データ
struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose; // ★追加: 法線変換用
};

// ライト情報
struct DirectionalLight {
    Vector4 color;
    Vector3 direction;
    float intensity;
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