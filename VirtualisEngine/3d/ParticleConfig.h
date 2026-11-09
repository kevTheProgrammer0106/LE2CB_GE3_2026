#pragma once

#include <string>

#include "BlendModes.h"
#include "WorldTransform.h"

//===================================================================
// パーティクルの設定(データだけ。GPUにもウィンドウにも依存しない)
//
// 1つの設定 = 1種類のエフェクト。GameSceneのエミッタとParticleEditorが
// ParticleLibrary を通して同じ設定を共有する(ホットリロード)。
//===================================================================

// 描画方式
enum class ParticleRenderMode : int
{
	Billboard3D = 0, // 3Dワールドに置き、常にカメラの方を向く板ポリ(深度テストあり)
	Screen2D,        // 画面にピクセル座標で直接描く。カメラの影響を受けず常に最前面

	Count_
};

// 時間変化のカーブ(サイズ・色の補間に使う)
enum class ParticleCurve : int
{
	Linear = 0,
	EaseInOut,

	Count_
};

// ImGuiのコンボ用(区切りの\0は文字列リテラルを分けて書く。"\0" "2D"の連結が8進エスケープに化けるのを避ける)
inline constexpr const char* kRenderModeItems = "3D Billboard\0" "2D Screen\0";
inline constexpr const char* kCurveItems = "Linear\0" "Ease In-Out\0";

// 設定ファイル(.particle)に書き出す名前
inline constexpr const char* kRenderModeNames[] = { "Billboard3D", "Screen2D" };
inline constexpr const char* kCurveNames[] = { "Linear", "EaseInOut" };

struct ParticleConfig
{
	// 同時に存在できる最大数の上限。エミッタはこの数ぶんのGPUバッファを最初に1回だけ確保する
	// (SRVスロットを増やさず、描画中にバッファを作り直さないため)
	static constexpr int kMaxParticlesLimit = 4096;

	//--------------------------------------------------
	// 描画
	//--------------------------------------------------
	ParticleRenderMode renderMode = ParticleRenderMode::Billboard3D;
	ParticleBlendMode  blendMode = ParticleBlendMode::Add;
	// 同時に存在できる最大数。満杯のときの発生は無視される(1〜kMaxParticlesLimit)
	int maxParticles = 512;

	//--------------------------------------------------
	// 発生
	//--------------------------------------------------
	float   emitRate = 64.0f;                      // 1秒あたりの発生数
	bool    looping = true;                        // trueなら emitRate で自動発生
	Vector3 spawnExtent{ 0.5f, 0.5f, 0.5f };       // 発生位置のばらつき(エミッタ位置を中心にした箱の半径)

	//--------------------------------------------------
	// 寿命・動き
	//--------------------------------------------------
	float   lifetimeMin = 1.5f;                    // 生存時間(秒)
	float   lifetimeMax = 2.5f;
	float   speedMin = 3.0f;                       // 初速(単位/秒。2Dではピクセル/秒)
	float   speedMax = 5.0f;
	Vector3 direction{ 0.0f, 1.0f, 0.0f };         // 発射方向(Y上。2Dでも画面の上が+Y)
	float   spreadAngle = 30.0f;                   // 発射方向からの広がり(度)。180で全方向
	Vector3 gravity{ 0.0f, 0.0f, 0.0f };           // 加速度(単位/秒^2)
	float   drag = 0.0f;                           // 空気抵抗(1秒あたりの速度の減衰率)

	//--------------------------------------------------
	// サイズ
	//--------------------------------------------------
	float         sizeStart = 2.0f;                // 生成時のサイズ(2Dではピクセル)
	float         sizeEnd = 0.0f;                  // 消滅時のサイズ
	ParticleCurve sizeCurve = ParticleCurve::Linear;

	//--------------------------------------------------
	// 回転(板ポリが画面内で回る角度。度)
	//--------------------------------------------------
	float rotationMin = 0.0f;                      // 生成時の角度
	float rotationMax = 0.0f;
	float rotationSpeedMin = 0.0f;                 // 回転速度(度/秒)
	float rotationSpeedMax = 0.0f;

	//--------------------------------------------------
	// 色
	//--------------------------------------------------
	Vector4       colorStart{ 1.0f, 1.0f, 1.0f, 1.0f }; // 生成時の色
	Vector4       colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };   // 消滅時の色
	ParticleCurve colorCurve = ParticleCurve::Linear;

	// 範囲外の値・最小最大の逆転を直す。読み込み・適用の前に必ず通す
	void Sanitize();

	//--------------------------------------------------
	// プリセット(Initialize時の初期値や、エディタの「プリセット」ボタンに使う)
	//--------------------------------------------------
	static ParticleConfig MakeDefault();
	// 3Dの炎(旧ParticleEmitterの黄→赤の色を引き継ぐ)
	static ParticleConfig MakeFire();
	// 2Dの火花(画面右下などに置く用)
	static ParticleConfig MakeSparkle2D();
};

//===================================================================
// 設定のテキスト入出力(resources/particles/名前.particle)
//
// 書式は「キー = 値」の行。#以降はコメント。外部ライブラリには依存しない。
// 無いキーは現在の値のまま残るので、必要な行だけ書けば上書きできる。
//===================================================================
class ParticleConfigSerializer
{
public:
	ParticleConfigSerializer() = delete;

	static std::string ToText(const ParticleConfig& config);
	// 1つもキーが読めなかったとき(書き込み途中のファイルなど)は false を返し、outは変更しない
	static bool FromText(const std::string& text, ParticleConfig& out);
};