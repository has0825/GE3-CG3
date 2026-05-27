#include "Skybox.hlsli"

cbuffer TransformationMatrix : register(b0)
{
    matrix WVP;
};

VertexShaderOutput main(float4 position : POSITION)
{
    VertexShaderOutput output;
    output.position = mul(position, WVP).xyww; // 常に最遠方(z=1.0)になるように計算
    output.texcoord = position.xyz; // ローカル座標をそのままサンプリングに使用
    return output;
}