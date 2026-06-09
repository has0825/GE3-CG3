#include "Sprite.h"
#include "MathUtil.h"
#include "DirectXCommon.h"

// ★ rawポインタから unique_ptr を返す形に変更
std::unique_ptr<Sprite> Sprite::Create(const std::string& textureName, Vector2 position) {
    std::unique_ptr<Sprite> sprite = std::make_unique<Sprite>();
    sprite->Initialize(textureName, position);
    return sprite;
}

void Sprite::Initialize(const std::string& textureName, Vector2 position) {
    textureName_ = textureName;
    TextureManager* texManager = TextureManager::GetInstance();

    // テクスチャがロードされていなければロード（一括管理）
    texManager->LoadTexture(textureName);
    metadata_ = texManager->GetMetaData(textureName);

    // デバイス取得
    ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

    // ★ サイズを 1.0f x 1.0f に固定する (大きさは Transform.scale で制御するため)
    VertexData vertices[] = {
        // pos(x,y,z,w), tex(u,v), normal(x,y,z)
        // 1枚目の三角形
        {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 左下
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 右下

        // 2枚目の三角形
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 右上
        {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 右下
    };

    // 頂点バッファ生成
    vertexResource_ = CreateBufferResource(device, sizeof(vertices));
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // 頂点データを転送
    VertexData* vertexMap = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap));
    std::memcpy(vertexMap, vertices, sizeof(vertices));
    vertexResource_->Unmap(0, nullptr);


    // マテリアルリソース生成
    materialResource_ = CreateBufferResource(device, sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = false; // スプライトはライティングしない
    materialData_->enableLighting = 0;
    materialData_->uvTransform = MakeIdentity4x4();

    // WVPリソース生成
    wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = MakeIdentity4x4();
    wvpData_->World = MakeIdentity4x4();

    // 初期トランスフォーム
    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {position.x, position.y, 0.0f} };

    // 初期状態として全体を表示
    SetTextureRect(0.0f, 0.0f, (float)metadata_.width, (float)metadata_.height);
}

void Sprite::Update() {
    // 必要に応じて更新処理
}

void Sprite::SetTextureRect(float left, float top, float width, float height) {
    float w = width / (float)metadata_.width;
    float h = height / (float)metadata_.height;
    float x = left / (float)metadata_.width;
    float y = top / (float)metadata_.height;

    materialData_->uvTransform = MakeIdentity4x4();
    materialData_->uvTransform.m[0][0] = w; // Scale X
    materialData_->uvTransform.m[1][1] = h; // Scale Y
    materialData_->uvTransform.m[3][0] = x; // Translate X
    materialData_->uvTransform.m[3][1] = y; // Translate Y
}

void Sprite::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection) {
    // ワールド行列計算
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    wvpData_->World = worldMatrix;
    wvpData_->WVP = Multiply(worldMatrix, viewProjection);

    // バッファセット
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

    // TextureManagerからハンドルを取得してセット
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureName_);
    commandList->SetGraphicsRootDescriptorTable(2, srvHandle);

    // 6頂点描画
    commandList->DrawInstanced(6, 1, 0, 0);
}

void Sprite::SetColor(const Vector4& color) {
    if (materialData_) {
        materialData_->color = color;
    }
}