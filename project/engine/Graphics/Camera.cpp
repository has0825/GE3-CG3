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
	if (target_) {
		// LookAtビュー行列の直接作成 (オイラー角による回転順序バグを完全回避)
		Vector3 eye = transform_.translate;
		Vector3 target = *target_;
		Vector3 up = { 0.0f, 1.0f, 0.0f }; // 上方向

		Vector3 diff = { target.x - eye.x, target.y - eye.y, target.z - eye.z };
		Vector3 L = Normalize(diff);
		Vector3 R = Normalize(Cross(up, L));
		Vector3 U = Cross(L, R);

		// 行ベクトル系 DirectX ビュー行列
		viewMatrix_ = MakeIdentity4x4();
		viewMatrix_.m[0][0] = R.x; viewMatrix_.m[0][1] = U.x; viewMatrix_.m[0][2] = L.x;
		viewMatrix_.m[1][0] = R.y; viewMatrix_.m[1][1] = U.y; viewMatrix_.m[1][2] = L.y;
		viewMatrix_.m[2][0] = R.z; viewMatrix_.m[2][1] = U.z; viewMatrix_.m[2][2] = L.z;
		viewMatrix_.m[3][0] = -(R.x * eye.x + R.y * eye.y + R.z * eye.z);
		viewMatrix_.m[3][1] = -(U.x * eye.x + U.y * eye.y + U.z * eye.z);
		viewMatrix_.m[3][2] = -(L.x * eye.x + L.y * eye.y + L.z * eye.z);

		// ワールド行列から回転部分を抽出（ビルボード用）
		Matrix4x4 worldMatrix = Inverse(viewMatrix_);
		billboardMatrix_ = worldMatrix;
		billboardMatrix_.m[3][0] = 0.0f;
		billboardMatrix_.m[3][1] = 0.0f;
		billboardMatrix_.m[3][2] = 0.0f;
	} else {
		// 1. カメラのワールド行列を作成
		Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

		// 2. ビュー行列を作成 (カメラのワールド行列の逆行列)
		viewMatrix_ = Inverse(worldMatrix);

		// 5. ビルボード行列を作成
		Matrix4x4 rotateX = MakeRotateXMatrix(transform_.rotate.x);
		Matrix4x4 rotateY = MakeRotateYMatrix(transform_.rotate.y);
		Matrix4x4 rotateZ = MakeRotateZMatrix(transform_.rotate.z);
		billboardMatrix_ = Multiply(Multiply(rotateX, rotateY), rotateZ);
	}

	// 3. プロジェクション行列を作成
	projectionMatrix_ = MakePerspectiveFovMatrix(fov_, aspectRatio_, nearClip_, farClip_);

	// 4. 合成行列 (ViewProjection) を作成
	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}