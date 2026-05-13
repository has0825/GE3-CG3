struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(uint vertexId : SV_VertexID) {
    VertexShaderOutput output;
    if (vertexId == 0) {
        output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
        output.texcoord = float2(0.0f, 1.0f);
    } else if (vertexId == 1) {
        output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
        output.texcoord = float2(0.0f, -1.0f);
    } else {
        output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
        output.texcoord = float2(2.0f, 1.0f);
    }
    return output;
}
