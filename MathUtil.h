#pragma once
#include "MathTypes.h"
#include <cmath>

Matrix4x4 MakeIdentity4x4();
Matrix4x4 Matrix4x4MakeScaleMatrix(const Vector3& s);
Matrix4x4 MakeRotateXMatrix(float radian);
Matrix4x4 MakeRotateYMatrix(float radian);
Matrix4x4 MakeRotateZMatrix(float radian);
Matrix4x4 MakeTranslateMatrix(const Vector3& tlanslate);
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

// ★これがないとエラーになります
Matrix4x4 Inverse(Matrix4x4 m);

// ★これがないとエラーになります ('Transpose': 識別子が見つかりませんでした の原因)
Matrix4x4 Transpose(const Matrix4x4& m);

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
Vector3 Normalize(const Vector3& v);
float Length(const Vector3& v);