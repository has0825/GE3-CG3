#pragma once
#include "MathUtil.h"

// カメラクラス
class Camera {
public:
	// ★追加: 引数なしコンストラクタ（GamePlaySceneでの生成エラーを防ぐため）
	Camera();
	// 既存のコンストラクタ
	Camera(int windowWidth, int windowHeight);

	// 毎フレーム呼ぶ更新処理
	void Update();

	// --- セッター ---
	void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
	void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
	void SetFov(float fov) { fov_ = fov; }

	// --- ゲッター ---
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

	// ★追加: ビルボード行列を取得（パーティクルで使用）
	const Matrix4x4& GetBillboardMatrix() const { return billboardMatrix_; }

	const Vector3& GetTranslate() const { return transform_.translate; }
	const Vector3& GetRotate() const { return transform_.rotate; }

	// 参照渡しで座標を直接操作可能にする
	struct Transform {
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};
	Transform& GetTransform() { return transform_; }

private:
	Transform transform_;
	Matrix4x4 worldMatrix_; // ★メンバ変数として保持するように変更
	Matrix4x4 viewMatrix_;
	Matrix4x4 projectionMatrix_;
	Matrix4x4 viewProjectionMatrix_;
	Matrix4x4 billboardMatrix_; // ★追加

	float fov_;
	float aspectRatio_;
	float nearClip_;
	float farClip_;
};