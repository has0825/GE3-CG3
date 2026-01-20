#include "Sprite.h"
#include "MathUtil.h"
#include "DirectXCommon.h"

std::unique_ptr<Sprite> Sprite::Create(const std::string& textureName, Vector2 position) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(textureName, position);
    return sprite;
}

void Sprite::Initialize(const std::string& textureName, Vector2 position) {
    textureName_ = textureName;
    TextureManager* texManager = TextureManager::GetInstance();

    texManager->LoadTexture(textureName);
    metadata_ = texManager->GetMetaData(textureName);

    ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

    float w = (float)metadata_.width;
    float h = (float)metadata_.height;

    // 6頂点でTriangleList
    VertexData vertices[6];
    // 左下
    vertices[0].position = { 0.0f, h, 0.0f, 1.0f };
    vertices[0].texcoord = { 0.0f, 1.0f };
    // 左上
    vertices[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertices[1].texcoord = { 0.0f, 0.0f };
    // 右下
    vertices[2].position = { w, h, 0.0f, 1.0f };
    vertices[2].texcoord = { 1.0f, 1.0f };

    // 左上
    vertices[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertices[3].texcoord = { 0.0f, 0.0f };
    // 右上
    vertices[4].position = { w, 0.0f, 0.0f, 1.0f };
    vertices[4].texcoord = { 1.0f, 0.0f };
    // 右下
    vertices[5].position = { w, h, 0.0f, 1.0f };
    vertices[5].texcoord = { 1.0f, 1.0f };

    for (int i = 0; i < 6; ++i) {
        vertices[i].normal = { 0.0f, 0.0f, -1.0f };
    }

    vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * 6);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertData));
    std::memcpy(vertData, vertices, sizeof(VertexData) * 6);
    vertexResource_->Unmap(0, nullptr);

    materialResource_ = CreateBufferResource(device, sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->enableLighting = 0;
    materialData_->uvTransform = MakeIdentity4x4();

    wvpResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = MakeIdentity4x4();
    wvpData_->World = MakeIdentity4x4();

    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {position.x, position.y, 0.0f} };
}

void Sprite::Update() {
    // 特になし
}

void Sprite::SetTextureRect(float left, float top, float width, float height) {
    float w = width / (float)metadata_.width;
    float h = height / (float)metadata_.height;
    float x = left / (float)metadata_.width;
    float y = top / (float)metadata_.height;

    materialData_->uvTransform = MakeIdentity4x4();
    materialData_->uvTransform.m[0][0] = w;
    materialData_->uvTransform.m[1][1] = h;
    materialData_->uvTransform.m[3][0] = x;
    materialData_->uvTransform.m[3][1] = y;
}

void Sprite::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection) {
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    wvpData_->World = worldMatrix;
    wvpData_->WVP = Multiply(worldMatrix, viewProjection);

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureName_);
    commandList->SetGraphicsRootDescriptorTable(2, srvHandle);

    commandList->DrawInstanced(6, 1, 0, 0);
}