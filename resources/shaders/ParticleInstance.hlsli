// パーティクル用のインスタンシング描画(ParticleEmitter 専用)
// 1パーティクル = StructuredBufferの1要素。CPU側の ParticleInstanceData と並びを合わせること

struct ParticleInstanceData
{
    float32_t4x4 WVP;
    float32_t4 color;
};

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t4 color : COLOR0;
};
