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
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);
Matrix4x4 Inverse(Matrix4x4 m);
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
Vector3 Normalize(const Vector3& v);
Vector3 Cross(const Vector3& v1, const Vector3& v2);
Vector4 Multiply(const Vector4& v, const Matrix4x4& m);

Matrix4x4 MakeScaleMatrix(const Vector3& s);

Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);
Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);
Matrix4x4 MakeRotateMatrixFromQuaternion(const Quaternion& q);