#include "ParticleInstance.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    // パーティクルごとの色(時間で変化する)をテクスチャに掛ける
    output.color = input.color * textureColor;

    // 完全に透明なピクセルは捨てる
    if (output.color.a <= 0.0f)
    {
        discard;
    }
    return output;
}
