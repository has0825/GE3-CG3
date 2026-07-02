#include "Camera.h"

Camera::Camera(int windowWidth, int windowHeight) {
	// デフォルト値の設定
	transform_ = {
		{1.0f, 1.0f, 1.0f}, // scale
		{0.0f, 0.0f, 0.0f}, // rotate
		{0.0f, 0.0f, -10.0f} // translate (少し後ろに下げる)
	};
	fov_ = 0.45f;
	aspectRatio_ = (float)windowWidth / (float)windowHeight;
	nearClip_ = 5.0f;     // ニアクリップを上げ、Z精度を手前から中・遠距離へ解放
	farClip_ = 5000000.0f;  // 天球（半径3000000m）をカバーできるよう大きく広げる

	// 初回計算
	Update();
}

void Camera::Update() {
	// 1. カメラのワールド行列を作成
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

	// 2. ビュー行列を作成 (カメラのワールド行列の逆行列)
	viewMatrix_ = Inverse(worldMatrix);

	// 3. プロジェクション行列を作成
	projectionMatrix_ = MakePerspectiveFovMatrix(fov_, aspectRatio_, nearClip_, farClip_);

	// 4. 合成行列 (ViewProjection) を作成
	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);

	// 5. ビルボード行列を作成
	Matrix4x4 rotateX = MakeRotateXMatrix(transform_.rotate.x);
	Matrix4x4 rotateY = MakeRotateYMatrix(transform_.rotate.y);
	Matrix4x4 rotateZ = MakeRotateZMatrix(transform_.rotate.z);
	billboardMatrix_ = Multiply(Multiply(rotateX, rotateY), rotateZ);
}