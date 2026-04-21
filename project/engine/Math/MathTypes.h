#pragma once

struct Vector2 {
	float x, y;
};
struct Vector3 {
	float x, y, z;
};
struct Vector4 {
	float x, y, z, w;
};
struct Matrix3x3 {
	float m[3][3];
};
struct Matrix4x4 {
	float m[4][4];
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct Quaternion {
	float x, y, z, w;
};