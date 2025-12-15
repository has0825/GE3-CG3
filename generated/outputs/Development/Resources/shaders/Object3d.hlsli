//03_00
struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

//05_03
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    
};
//05_03
struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};