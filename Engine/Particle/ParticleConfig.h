#pragma once

#include <string>

#include "Engine/Graphics/BlendModes.h"
#include "Engine/Math/WorldTransform.h"

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

// 時間変化のカーブ(サイズ・色の補間に使う)。保存済みファイルの互換のため、既存の値の順番は変えない
enum class ParticleCurve : int
{
	Linear = 0,
	EaseInOut, // 出だしと終わりがゆっくり
	EaseIn,    // 最初ゆっくり、あとで速く(t^2)
	EaseOut,   // 最初速く、あとでゆっくり
	Pulse,     // start → end → start と往復する(サイズの脈動など)

	Count_
};

// 発生範囲の形(UnityのShapeモジュール相当)
enum class ParticleEmitShape : int
{
	Box = 0,    // spawnExtent の箱(従来どおり)
	Sphere,     // 球(2Dでは円)。半径は shapeRadius
	Hemisphere, // 半球(Y+側。2Dでは上半分)
	Circle,     // 円盤(3DではXZ平面。2Dでは円)
	Line,       // X軸方向の線(長さは spawnExtent.x の2倍)

	Count_
};

// ImGuiのコンボ用(区切りの\0は文字列リテラルを分けて書く。"\0" "2D"の連結が8進エスケープに化けるのを避ける)
inline constexpr const char* kRenderModeItems = "3D Billboard\0" "2D Screen\0";
inline constexpr const char* kCurveItems = "Linear\0" "Ease In-Out\0" "Ease In\0" "Ease Out\0" "Pulse (start-end-start)\0";
inline constexpr const char* kEmitShapeItems = "Box\0" "Sphere\0" "Hemisphere\0" "Circle\0" "Line\0";

// 設定ファイル(.particle)に書き出す名前
inline constexpr const char* kRenderModeNames[] = { "Billboard3D", "Screen2D" };
inline constexpr const char* kCurveNames[] = { "Linear", "EaseInOut", "EaseIn", "EaseOut", "Pulse" };
inline constexpr const char* kEmitShapeNames[] = { "Box", "Sphere", "Hemisphere", "Circle", "Line" };

struct ParticleConfig
{
	// 同時に存在できる最大数の上限(GPUバッファの大きさ)
	static constexpr int kMaxParticlesLimit = 4096;

	//--------------------------------------------------
	// 描画
	//--------------------------------------------------
	ParticleRenderMode renderMode = ParticleRenderMode::Billboard3D;
	ParticleBlendMode  blendMode = ParticleBlendMode::Add;
	// 同時に存在できる最大数。満杯のときの発生は無視される。変更するとGPUバッファを作り直す
	int maxParticles = 512;

	//--------------------------------------------------
	// 発生
	//--------------------------------------------------
	float   emitRate = 64.0f;                      // 1秒あたりの発生数
	bool    looping = true;                        // trueなら emitRate で自動発生
	Vector3 spawnExtent{ 0.5f, 0.5f, 0.5f };       // 発生位置のばらつき(エミッタ位置を中心にした箱の半径)

	// --- 発生範囲の形(UnityのShapeモジュール相当) ---
	ParticleEmitShape shape = ParticleEmitShape::Box; // Box以外では shapeRadius(Lineは spawnExtent.x)が範囲になる
	float shapeRadius = 1.0f;                      // Sphere / Hemisphere / Circle の半径(2Dではピクセル)
	float shapeThickness = 1.0f;                   // 0 = 表面だけ(輪・殻) / 1 = 中身まで一様
	bool  emitOutward = false;                     // true: 発射方向を「中心から外向き」にする(spreadAngleはその周りの広がり)

	// --- バースト(UnityのBurstsモジュール相当。emitRateの自動発生とは別に、まとめて出す) ---
	float startDelay = 0.0f;                       // 発生が始まるまでの待ち(秒)。自動発生にもバーストにも掛かる
	int   burstCount = 0;                          // 1回のバーストで出す数。0ならバーストしない
	float burstInterval = 1.0f;                    // バーストの間隔(秒)
	int   burstCycles = 1;                         // バーストの回数。0なら無限に繰り返す
	bool  burstOnce = false;                       // true: burstCount を1回だけ出す(Loopingのオン/オフとは別。burstCycles・Intervalは無視)。Looping をオフにすれば「1回きりの爆発」になり、Play() / エディタの Play で何度でも再生できる

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

	// --- 力・動きの追加(Unity / Godot / Niagara のよくある項目) ---
	float inheritVelocity = 0.0f;                  // エミッタの移動速度を粒の初速に足す割合(0〜1。ワールド空間のときだけ)
	bool  localSpace = false;                      // true: エミッタに付いて動く(Simulation Space = Local)。false: 発生後はワールドに残る
	float radialAcceleration = 0.0f;               // 中心から外へ押す加速度(負で中心へ吸い込む)
	float orbitSpeed = 0.0f;                       // 中心まわりを回る速さ(度/秒。3DはY軸、2Dは画面)
	float turbulence = 0.0f;                       // 乱流(ノイズ)の強さ(加速度)。煙・炎の揺らぎ用
	float turbulenceFrequency = 1.0f;              // ノイズの細かさ(大きいほど細かく変化)
	float turbulenceSpeed = 0.5f;                  // ノイズが時間で流れる速さ

	// --- 地面との衝突(Unityの Collision 簡易版。無限に広い水平面) ---
	bool  groundCollision = false;                 // 3D: ワールドYの床 / 2D: 画面のY(ピクセル)の床
	float groundHeight = 0.0f;
	float bounciness = 0.5f;                       // 跳ね返り(0〜1)
	float bounceLifeLoss = 0.0f;                   // 跳ねるたびに失う「残り寿命」の割合(0〜1)

	//--------------------------------------------------
	// サイズ
	//--------------------------------------------------
	float         sizeStart = 2.0f;                // 生成時のサイズ(2Dではピクセル)
	float         sizeEnd = 0.0f;                  // 消滅時のサイズ
	ParticleCurve sizeCurve = ParticleCurve::Linear;
	float         sizeRandomness = 0.0f;           // 粒ごとのサイズのばらつき(0〜1。0.3なら ±30%)

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

	// --- 色の追加 ---
	bool    randomColor = false;                   // true: 生成時の色を colorStart と colorStartB の間でランダムにする
	Vector4 colorStartB{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool    useColorMid = false;                   // true: 3色グラデーション(colorStart → colorMid → colorEnd)
	Vector4 colorMid{ 1.0f, 1.0f, 1.0f, 1.0f };
	float   colorMidTime = 0.5f;                   // colorMid の位置(寿命の割合 0〜1)
	float   fadeIn = 0.0f;                         // 生まれてから不透明になるまでの割合(0〜0.5。ポップの防止)

	//--------------------------------------------------
	// 板ポリの向き(UnityのStretched Billboard相当)
	//--------------------------------------------------
	bool  alignToVelocity = false;                 // true: 進行方向に板ポリを向ける(火花・雨・流れ星)
	float stretchScale = 0.0f;                     // 速さに応じて進行方向へ伸ばす量(速さ1あたりの倍率の増加)
	float stretchMax = 4.0f;                       // 伸びる倍率の上限

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
	// 爆発(球の表面から外へ一斉に飛ぶバースト)
	static ParticleConfig MakeExplosion();
	// 火花(進行方向に伸びて、地面で跳ねる)
	static ParticleConfig MakeSparks();
	// 煙(乱流でゆらゆら昇る・3色グラデーション)
	static ParticleConfig MakeSmoke();
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
