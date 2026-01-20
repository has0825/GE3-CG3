#include "Camera.h"

// ★追加: デフォルトコンストラクタ
Camera::Camera() : Camera(1280, 720) {
}

Camera::Camera(int windowWidth, int windowHeight) {
	// デフォルト値の設定
	transform_ = {
		{1.0f, 1.0f, 1.0f}, // scale
		{0.0f, 0.0f, 0.0f}, // rotate
		{0.0f, 0.0f, -10.0f} // translate (少し後ろに下げる)
	};
	fov_ = 0.45f;
	aspectRatio_ = (float)windowWidth / (float)windowHeight;
	nearClip_ = 0.1f;
	farClip_ = 100.0f;

	// 初回計算
	Update();
}

void Camera::Update() {
	// 1. カメラのワールド行列を作成
	worldMatrix_ = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

	// 2. ビュー行列を作成 (カメラのワールド行列の逆行列)
	viewMatrix_ = Inverse(worldMatrix_);

	// 3. プロジェクション行列を作成
	projectionMatrix_ = MakePerspectiveFovMatrix(fov_, aspectRatio_, nearClip_, farClip_);

	// 4. 合成行列 (ViewProjection) を作成
	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);

	// ★追加: ビルボード行列の計算
	// カメラのワールド行列から回転成分だけを取り出す（平行移動を0にする）
	// これをパーティクルに掛けると、常にカメラの方を向くようになる
	billboardMatrix_ = worldMatrix_;
	billboardMatrix_.m[3][0] = 0.0f;
	billboardMatrix_.m[3][1] = 0.0f;
	billboardMatrix_.m[3][2] = 0.0f;
}