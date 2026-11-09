// デバッグ線用(DebugDraw)。頂点ごとの色をそのまま通す

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// ルートパラメータ[1](VS b0)。DebugDrawは WVP にビュー射影行列を入れる
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t4 color : COLOR0;
};

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.color = input.color;
    return output;
}
