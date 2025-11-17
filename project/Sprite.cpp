#include "Sprite.h"
#include "MathUtil.h"
#include "DirectXCommon.h" // ★これが抜けていたため DirectXCommon が見つからないエラーが出ていました

Sprite* Sprite::Create(const std::string& textureName, Vector2 position) {
    Sprite* sprite = new Sprite();
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

    // 四角形（2ポリゴン = 三角形2つ）の頂点データ作成
    // 左下、左上、右下、右上 の順序でTriangleStrip、あるいは6頂点でTriangleList
    // ここでは簡易的に6頂点でTriangleListを作ります
    // ※本来はIndexBufferを使うのが効率的ですが、実装を単純にするため頂点を羅列します

    // 暫定サイズ（後でSetTextureRectで調整されるが、初期値として画像サイズを入れておく）
    float w = (float)metadata_.width;
    float h = (float)metadata_.height;

    // 頂点配列 (6頂点)
    VertexData vertices[] = {
        // pos(x,y,z,w), tex(u,v), normal(x,y,z)
        // 1枚目の三角形
        {{0.0f, h, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 左下
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{w, h, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},    // 右下

        // 2枚目の三角形
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{w, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},    // 右上
        {{w, h, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},    // 右下
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
    materialData_->enableLighting = false; // スプライトはライティングしない（これを0にするかfalseにするかはシェーダの実装次第だが、int型なら0/1）
    // DataTypes.hの定義が int32_t enableLighting なら下記のように数値を入れる
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
    // UV座標を計算して uvTransform 行列を設定する
    // テクスチャの幅・高さに対する割合を計算
    float w = width / (float)metadata_.width;
    float h = height / (float)metadata_.height;
    float x = left / (float)metadata_.width;
    float y = top / (float)metadata_.height;

    // UVトランスフォーム行列の更新 (平行移動とスケーリング)
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
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

    // TextureManagerからハンドルを取得してセット
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureName_);
    commandList->SetGraphicsRootDescriptorTable(2, srvHandle);

    // 6頂点描画
    commandList->DrawInstanced(6, 1, 0, 0);
}