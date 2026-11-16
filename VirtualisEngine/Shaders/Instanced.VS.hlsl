#include "Object3d.hlsli"

// インスタンシング描画用: 行列をStructuredBufferから読む(InstancingObject が使う。PSOは PipelineManager::GetInstancingPso)
StructuredBuffer<TransformationMatrix> gTransformationMatrices : register(t0);

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrices[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrices[instanceId].World));
    return output;
}
