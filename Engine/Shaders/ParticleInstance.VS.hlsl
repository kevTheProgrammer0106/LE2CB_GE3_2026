#include "ParticleInstance.hlsli"

// 全パーティクルの行列と色(ルートパラメータ[4] / VS t0)
StructuredBuffer<ParticleInstanceData> gParticles : register(t0);

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    ParticleInstanceData particle = gParticles[instanceId];

    // ビルボード(カメラの向きを向く回転)や2Dの変換はCPU側でWVPに焼き込み済み
    output.position = mul(input.position, particle.WVP);
    output.texcoord = input.texcoord;
    output.color = particle.color;
    return output;
}
