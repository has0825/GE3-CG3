#include "object3d.hlsli"

// 1�C���X�^���X������̃f�[�^�\��
// C++����InstancingData�\���̂ƈ�v������
struct InstancingData
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// �S�C���X�^���X�̃f�[�^��󂯎�邽�߂̍\�����o�b�t�@
// t1���W�X�^�Ƀo�C���h����
StructuredBuffer<InstancingData> gInstancingData : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};


// main�֐��̈����ɃC���X�^���XID��ǉ�
VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput output;

    // �C���X�^���XID��g���āA���̒��_�ɑΉ�����C���X�^���X�̃f�[�^��擾
    InstancingData instancingData = gInstancingData[instanceID];

    // �擾�����C���X�^���X�ŗL�̍s���g���č��W�ϊ�
    output.position = mul(input.position, instancingData.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) instancingData.World));
    output.worldPosition = mul(input.position, instancingData.World).xyz; // worldPosition��Y�ꂸ�Ɍv�Z

    return output;
}