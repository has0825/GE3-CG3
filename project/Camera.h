#pragma once
#include "MathUtil.h"

// カメラクラス
class Camera {
public:
	// コンストラクタ (画面サイズ指定)
	Camera(int windowWidth, int windowHeight);

	// 更新処理
	void Update();

	// 行列・座標の取得
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	const Transform& GetTransform() const { return transform_; }

	// トランスフォームへのアクセス (書き換え用)
	Transform& GetTransform() { return transform_; }
	void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
	void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }

private:
	Transform transform_;
	Matrix4x4 viewMatrix_;
	Matrix4x4 projectionMatrix_;
	Matrix4x4 viewProjectionMatrix_;
	float aspectRatio_;
};