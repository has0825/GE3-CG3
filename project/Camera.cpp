#include "Camera.h"

Camera::Camera(int windowWidth, int windowHeight) {
	aspectRatio_ = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
	transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };
	Update();
}

void Camera::Update() {
	// アフィン変換行列 (カメラのワールド行列)
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

	// ビュー行列 (ワールド行列の逆行列)
	viewMatrix_ = Inverse(worldMatrix);

	// プロジェクション行列
	projectionMatrix_ = MakePerspectiveFovMatrix(0.45f, aspectRatio_, 0.1f, 100.0f);

	// 合成
	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}