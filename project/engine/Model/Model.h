#pragma once
#include "D3D12Util.h"
#include "DataTypes.h"
#include "MathUtil.h"
#include <string>
#include <vector>
#include <map>     // ★ 追加
#include <memory>  // ★ 追加

// ★ モデルデータ（メッシュ）を保持する構造体
struct MeshData {
	std::vector<VertexData> vertices;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	MaterialData materialInfo; // .mtlから読んだ情報
};

class Model {
public:
	static Model* Create(
		const std::string& directoryPath, const std::string& filename, ID3D12Device* device);

	void Update();

	void Draw(
		ID3D12GraphicsCommandList* commandList,
		const Matrix4x4& viewProjectionMatrix,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle);

	// ★ 複数のモデルデータ（メッシュ）を切り替えて使えるようにする
	void SetMesh(std::shared_ptr<MeshData> meshData) {
		meshData_ = meshData;
	}

	// ★ モデル（メッシュ）データをロード（または取得）する static 関数
	static std::shared_ptr<MeshData> LoadMesh(
		const std::string& directoryPath, const std::string& filename, ID3D12Device* device);


public:
	Transform transform;
	Material* materialData = nullptr; // CBV用のマテリアルデータ (インスタンスごと)

private:
	void Initialize(ID3D12Device* device); // ★ 引数変更

private:
	// ★ 削除 (MeshDataに移動)
	// std::vector<VertexData> vertices_;
	// Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	// D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	// ★ 追加 (メッシュデータを参照)
	std::shared_ptr<MeshData> meshData_;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_; // インスタンスごとのマテリアルCBV

	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_; // インスタンスごとのWVP CBV
	TransformationMatrix* wvpData_ = nullptr;

	// ★ 追加 (モデルデータの一括管理用キャッシュ)
	static std::map<std::string, std::shared_ptr<MeshData>> meshCache_;
	static ID3D12Device* device_; // ★ LoadMesh内で使うためデバイスを保持
};