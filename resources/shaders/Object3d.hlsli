
struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

struct Material
{
    float32_t4 color;
    int lightingMode;
    float32_t3 padding; // 明示的なパディング。C++側のfloat padding[3]と対応させる
    float32_t4x4 uvTransform;
};

struct DirectionalLight
{
    float32_t4 color; //!< ライトの色
    float32_t3 direction; // x y z 
    float intensity; //!< 輝度
};

struct VertexShaderInput
{
    float32_t4 position : POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};
struct VertexShaderOutput
{
	float32_t4 position : SV_POSITION;
	float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};
