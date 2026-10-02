#pragma once

#define _USE_MATH_DEFINES

#include <d3d12.h>
#include <math.h>
#include <vector>

#include "ResourceObject.h"
#include "WorldTransform.h"


inline float Lerp(float start, float end, float t) {
    return (1.0f - t) * start + t * end;
};
inline float EaseInOut(float start, float end, float t) {
    float num;
    num = -(cosf(static_cast<float>(M_PI) * t) - 1.0f) / 2;
    return Lerp(start, end, num);
};

// -------------------------------------------------------
// ブレンドモード（main.cpp の enum と対応 — 同期を保つこと）
// -------------------------------------------------------
enum class ParticleBlendMode
{
    None,       // ブレンドなし（不透明）
    Normal,     // Src*SrcA + Dest*(1-SrcA)          — 標準合成
    Add,        // Src*SrcA + Dest*1                  — 炎・発光など
    Subtract,   // Dest*1   - Src*SrcA                — 影・侵食など
    Multiply,   // Dest*Src                           — 焼き込み
    Screen,     // Src*(1-Dest) + Dest*1              — ソフトライト
    Exclusion,  // (1-Dest)*Src + (1-Src)*Dest        — サイケデリック効果

    Count_      // 番兵値 — 使用禁止
};

// 指定したモードに対応する D3D12_BLEND_DESC を生成する。
// PSO を作成する際に、モードごとに一度だけ呼び出すこと。
inline D3D12_BLEND_DESC CreateParticleBlendDesc(ParticleBlendMode mode)
{
    D3D12_BLEND_DESC desc{};
    D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];

    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode)
    {
    case ParticleBlendMode::None:
        rt.BlendEnable = FALSE;     // ブレンド無効
        break;

    case ParticleBlendMode::Normal:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;   // デスティネーションに (1-α) 乗算
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case ParticleBlendMode::Add:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
        rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま加算
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case ParticleBlendMode::Subtract:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
        rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま保持
        rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;  // Dest - Src（逆減算）
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case ParticleBlendMode::Multiply:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_DEST_COLOR;       // デスティネーションの色を掛け合わせる
        rt.DestBlend = D3D12_BLEND_ZERO;            // デスティネーション成分は破棄
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case ParticleBlendMode::Screen:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;  // ソースに (1-Dest) を乗算
        rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま保持
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    case ParticleBlendMode::Exclusion:
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;  // ソースに (1-Dest) を乗算
        rt.DestBlend = D3D12_BLEND_INV_SRC_COLOR;  // デスティネーションに (1-Src) を乗算
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;

    default:
        rt.BlendEnable = FALSE;     // 未知のモードはブレンドなしにフォールバック
        break;
    }
    return desc;
}

// -------------------------------------------------------
// パーティクル構造体
// -------------------------------------------------------
struct Particle
{
    TransformData transform;    // ワールド変換データ（位置・回転・スケール）
    Vector4       spawnOrigin; // 生成時の発生位置（引き寄せ先の基準点として保持）
    Vector4       color;        // 現在の色（RGBA）
    Vector4       velocity;     // 速度ベクトル（xyz成分を使用）
    float         lifetime;     // 生存時間（秒）
    float         age;          // 経過時間（秒）
    bool          active;       // アクティブフラグ（trueのとき更新・描画対象）

    // 生存時間に対する経過時間の割合 [0.0 〜 1.0]
    float NormalizedAge() const { return age / lifetime; }
    // 生存時間を過ぎていれば true を返す
    bool  IsDead()        const { return age >= lifetime; }
};

// -------------------------------------------------------
// エミッタ設定
// -------------------------------------------------------
struct ParticleEmitterConfig
{
    Vector4 position{ 0.0f, 0.0f, 0.0f, 1.0f };        // 発生位置（ワールド座標）
    Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };      // 生成時の色（不透明白）
    Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };        // 消滅時の色（透明白）
    float   lifetimeMin = 3.0f;     // 生存時間の最小値（秒）
    float   lifetimeMax = 5.0f;     // 生存時間の最大値（秒）
    float   speedMin = 2.5f;        // 初速の最小値
    float   speedMax = 3.0f;        // 初速の最大値
    float   sizeStart = 5.0f;       // 生成時のサイズ
    float   sizeEnd = 0.0f;         // 消滅時のサイズ
    float   emitRate = 64.0f;       // 1秒あたりの発生数
    bool    looping = true;         // ループ再生フラグ

    // このエミッタが使用するブレンドモード。
    // Draw() に渡す PSO 配列は、このモードに対応したものを用意すること。
    ParticleBlendMode blendMode = ParticleBlendMode::Add;
};

// -------------------------------------------------------
// ParticleEmitter クラス
// -------------------------------------------------------
class ParticleEmitter
{
public:
    static const int MAX_PARTICLES = 256;   // 最大パーティクル数

	ParticleEmitter() = default;
	ParticleEmitter(const ParticleEmitter&) = delete;
	ParticleEmitter& operator=(const ParticleEmitter&) = delete;

    // 初期化：GPU リソースの確保などを行う
    void Initialize(ID3D12Device* device);
    // 指定数のパーティクルを即時発生させる
    void Emit(int count);
    // フレームごとの更新処理（位置・色・寿命の更新）
    void Update(float deltaTime);

    // 描画処理。
    // pipelineStates は ParticleBlendMode::Count_ 以上の要素数を持つ
    // PSO 配列へのポインタ（ブレンドモードでインデックス）。
    // nullptr を渡すと、既にコマンドリストに設定済みの PSO をそのまま使用する。
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projectionMatrix,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        ID3D12PipelineState* const* pipelineStates = nullptr
    );

    // シャットダウン：GPU リソースの解放
    void Shutdown();

    ParticleEmitterConfig config;   // エミッタの設定（外部から直接変更可）

    // デストラクタでシャットダウンを自動呼び出し
    ~ParticleEmitter() { Shutdown(); }

private:
    // 1個のパーティクルを生成してプールに追加する
    void    SpawnParticle();
    // ランダムな方向ベクトルに speed を掛けた速度を返す
    Vector4 RandomVelocity(float speed);
    // [min, max] の一様乱数を返す
    float   RandomFloat(float min, float max);

    std::vector<Particle> particles_;   // パーティクルのプール

    // パーティクルごとの WVP 行列リソース（GPU 上のバッファ）
    ResourceObject wvpResources_[MAX_PARTICLES] = {};
    // パーティクルごとのマテリアル（色）リソース（GPU 上のバッファ）
    ResourceObject materialResources_[MAX_PARTICLES] = {};
    // WVP 行列の CPU 側マップポインタ
    TransformationMatrix* wvpData_[MAX_PARTICLES] = {};
    // マテリアル色の CPU 側マップポインタ
    Material* materialData_[MAX_PARTICLES] = {};

    // 全パーティクルで共有する頂点バッファリソース（ビルボード用クワッド）
    ResourceObject sharedVertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};    // 頂点バッファビュー

    float emitAccumulator_ = 0.0f;  // 発生レートの端数を積算するアキュムレータ
    bool  initialized_ = false;     // Initialize() 呼び出し済みフラグ
};