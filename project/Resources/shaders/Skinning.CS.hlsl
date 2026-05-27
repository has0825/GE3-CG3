struct Well {
    float32_t4x4 skeletonSpaceMatrix;
    float32_t4x4 skeletonSpaceInverseTransposeMatrix;
};

struct Vertex {
    float32_t4 position;
    float32_t2 texcoord;
    float32_t3 normal;
    int32_t4 jointIndices;
    float32_t4 jointWeights;
};

struct VertexInfluence {
    float32_t4 weight;
    int32_t4 index;
};

struct SkinningInformation {
    uint32_t numVertices;
};

StructuredBuffer<Well> gMatrixPalette : register(t0);
StructuredBuffer<Vertex> gInputVertices : register(t1);
StructuredBuffer<VertexInfluence> gInfluences : register(t2);

RWStructuredBuffer<Vertex> gOutputVertices : register(u0);

ConstantBuffer<SkinningInformation> gSkinningInformation : register(b0);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gSkinningInformation.numVertices) {
        return;
    }

    Vertex input = gInputVertices[DTid.x];
    VertexInfluence influence = gInfluences[DTid.x];

    Vertex output;
    output.texcoord = input.texcoord;
    output.jointIndices = input.jointIndices;
    output.jointWeights = input.jointWeights;

    // 位置の変換
    float32_t4 skinnedPosition = mul(input.position, gMatrixPalette[influence.index.x].skeletonSpaceMatrix) * influence.weight.x;
    skinnedPosition += mul(input.position, gMatrixPalette[influence.index.y].skeletonSpaceMatrix) * influence.weight.y;
    skinnedPosition += mul(input.position, gMatrixPalette[influence.index.z].skeletonSpaceMatrix) * influence.weight.z;
    skinnedPosition += mul(input.position, gMatrixPalette[influence.index.w].skeletonSpaceMatrix) * influence.weight.w;
    skinnedPosition.w = 1.0f;

    // 法線の変換
    float32_t3 skinnedNormal = mul(input.normal, (float32_t3x3)gMatrixPalette[influence.index.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x;
    skinnedNormal += mul(input.normal, (float32_t3x3)gMatrixPalette[influence.index.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y;
    skinnedNormal += mul(input.normal, (float32_t3x3)gMatrixPalette[influence.index.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z;
    skinnedNormal += mul(input.normal, (float32_t3x3)gMatrixPalette[influence.index.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;
    skinnedNormal = normalize(skinnedNormal);

    output.position = skinnedPosition;
    output.normal = skinnedNormal;

    gOutputVertices[DTid.x] = output;
}
