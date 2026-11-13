#pragma once

#include <d3d12.h>
#include <memory>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "Engine/Particle/ParticleConfig.h"
#include "Engine/Particle/ParticleEffect.h"
#include "Engine/Particle/ParticleLibrary.h"
#include "Engine/Particle/ParticleSim.h"
#include "Engine/Graphics/ResourceObject.h"
#include "Engine/Math/WorldTransform.h"

class DirectXCommon;
class PipelineManager;

//===================================================================
// GPUへ渡す1パーティクル分のデータ(ParticleInstance.hlsli の ParticleInstanceData と同じ並び)
//===================================================================
struct ParticleInstanceData
{
	Matrix4x4 WVP;   // ビルボード化・2D変換まで済ませた行列
	Vector4   color; // 時間で変化した今の色
};
static_assert(sizeof(ParticleInstanceData) == 80, "ParticleInstanceData must match the HLSL struct");

//===================================================================
// CPU側の1パーティクル
// 発生した時点の設定を自分で持つ(スナップショット)。
// ホットリロードで設定が変わっても、すでに飛んでいる粒の見た目は変わらず、次に発生する粒から変わる
//===================================================================
struct Particle
{
	Vector3 position{};
	Vector3 velocity{};
	float age = 0.0f;
	float lifetime = 1.0f;
	float rotation = 0.0f;      // ラジアン
	float rotationSpeed = 0.0f; // ラジアン/秒

	// 発生時のスナップショット
	Vector3 gravity{};
	float drag = 0.0f;
	float sizeStart = 1.0f;
	float sizeEnd = 0.0f;
	ParticleCurve sizeCurve = ParticleCurve::Linear;
	Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
	ParticleCurve colorCurve = ParticleCurve::Linear;

	// 追加パラメータのスナップショット
	bool localSpace = false;     // true: position / velocity はエミッタからの相対(描くときにエミッタ位置を足す)
	float radialAccel = 0.0f;
	float orbitSpeed = 0.0f;     // ラジアン/秒
	float turbulence = 0.0f;
	float turbulenceFreq = 1.0f;
	float turbulenceSpeed = 0.5f;
	float seed = 0.0f;           // 乱流の位相(粒ごとに違う)
	bool groundCollision = false;
	float groundHeight = 0.0f;
	float bounciness = 0.5f;
	float bounceLifeLoss = 0.0f;
	bool useColorMid = false;
	Vector4 colorMid{ 1.0f, 1.0f, 1.0f, 1.0f };
	float colorMidTime = 0.5f;
	float fadeIn = 0.0f;
	bool alignToVelocity = false;
	float stretchScale = 0.0f;
	float stretchMax = 4.0f;

	// 終点エミッタを使うときだけ有効(発生時のスナップショット)。
	// followPath が true の粒は、速度ではなく「始点 → 終点」の道のりで位置が決まる
	bool followPath = false;
	Vector3 pathStart{};
	Vector3 pathEnd{};
	Vector3 offsetUnit{};        // 各軸 -1〜1 の乱数。実際のずれ = offsetUnit × その時点の広がり
	Vector3 startExtent{};       // 始点側の広がり(集中)
	Vector3 endExtent{};         // 終点側の広がり(拡散)
	ParticleCurve pathCurve = ParticleCurve::Linear;
};

/// <summary>
/// GPUインスタンシングで描くパーティクルエミッタ。
/// 全パーティクルを1つのStructuredBufferに詰めて、1回のDrawInstancedで描く。
///
/// 使い方(2通り):
///   ・ゲーム用 : Initialize(dx, library, "名前", デフォルト設定)
///                ライブラリに登録されるので、ParticleEditorの一覧に現れ、Applyで設定が書き換わる
///   ・単体     : Initialize(dx) + SetConfig()  (エディタのプレビュー用。ライブラリとは繋がらない)
///
/// 毎フレームの呼び出し順: Update(dt) → BuildInstances(...) → Draw(...)
/// </summary>
class ParticleEmitter
{
public:
	/// ゲーム用。ライブラリに登録し、以後そのEntryの変更(ホットリロード)を追従する
	void Initialize(DirectXCommon& dx, ParticleLibrary& library, const std::string& name, const ParticleConfig& defaultConfig);
	/// ゲーム用(複数レイヤーのエフェクト版)
	void Initialize(DirectXCommon& dx, ParticleLibrary& library, const std::string& name, const ParticleEffect& defaultEffect);
	/// 単体用(ライブラリなし)
	void Initialize(DirectXCommon& dx);

	/// 発生・移動・寿命切れの処理。ライブラリの更新(revision)もここで検知する
	void Update(float deltaTime);

	/// 今の粒をGPUバッファに書き込む。
	/// 3Dはカメラの向きでビルボード化し、2Dはピクセル座標の正射影で画面に直接置く
	void BuildInstances(const Matrix4x4& view, const Matrix4x4& projection, float clientWidth, float clientHeight);

	/// ルートシグネチャとトポロジは呼び出し側(シーン)が設定済みであること
	void Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;

	/// 今すぐ count 個発生させる(バースト)
	void Emit(int count);
	/// 全部消し、発生のタイミング(startDelay・バースト回数・エフェクトの時刻)も最初からにする
	void Clear();
	/// エフェクトを最初から再生する(エフェクトの時刻を0に戻し、各レイヤーのバーストを撃ち直す)。
	/// 出ている粒は消えるまで残る。clearParticles を true にすると一度全部消してから始める。
	/// Burst Once のエフェクトを何度でも鳴らす用(爆発・被弾など)
	void Play(bool clearParticles = false);
	/// 単体用: 親レイヤーの設定だけ直接差し替える(次の発生から反映。子レイヤーはそのまま)
	void SetConfig(const ParticleConfig& config);
	/// 単体用: エフェクト(全レイヤー)を直接差し替える。エフェクトの時刻は保つ(編集中も再生が途切れない)
	void SetEffect(const ParticleEffect& effect);
	const ParticleEffect& GetEffect() const
	{
		return effect_;
	}

	//----------------------------------------------------------------
	// レイヤー(エフェクトの木)の状態。エディタのギズモ・タイムラインの表示に使う
	//----------------------------------------------------------------
	/// エフェクトを再生してからの時刻(秒)
	float GetEffectTime() const
	{
		return effectTime_;
	}
	/// 親 + 子 の合計の粒数
	size_t GetTotalAliveCount() const;
	/// レイヤーの現在のワールド位置(親の位置 + offset を積み上げたもの)
	Vector3 GetLayerPosition(int layerIndex) const;
	/// レイヤーが今発生中か(タイムラインの窓の中で、ミュートされていない)
	bool IsLayerEmitting(int layerIndex) const;

	const ParticleConfig& GetConfig() const
	{
		return config_;
	}
	const std::string& GetName() const
	{
		return name_;
	}
	/// ライブラリ上の番号(単体用なら -1)
	int GetLibraryIndex() const
	{
		return libraryIndex_;
	}
	size_t GetAliveCount() const
	{
		return particles_.size();
	}
	ParticleRenderMode GetRenderMode() const
	{
		return config_.renderMode;
	}

	//----------------------------------------------------------------
	// 終点エミッタ(任意)
	//   セットすると、このエミッタの粒は「このエミッタの位置 → 終点エミッタの位置」へ向かって進み、
	//   発生した直後は spawnExtent(集中)、終点に着くころは終点エミッタの spawnExtent(拡散)へ広がる。
	//   それ以外の値(寿命・サイズ・色・回転・発生数など)は、このエミッタの設定をそのまま使う。
	//   セットしなければ(nullptr)今までと完全に同じ動き。
	//   ・終点エミッタは「位置と広がりを持つ目印」。発生は終点側では行わない(自分の looping が true なら自分の粒は出る)
	//   ・描画方式(3D/2D)が違う終点は無視される
	//   ・飛んでいる粒は発生時の始点・終点を覚えているので、あとからエミッタを動かしても既存の粒の道は変わらない
	//   ・終点は設定ファイル(.particle)には保存されない(ゲーム側のコードで繋ぐ)
	//----------------------------------------------------------------
	void SetEndEmitter(const ParticleEmitter* endEmitter);
	void ClearEndEmitter();
	const ParticleEmitter* GetEndEmitter() const
	{
		return endEmitter_;
	}
	/// 終点が有効か(セット済み・自分自身でない・描画方式が同じ)
	bool HasActiveEndPoint() const;

	/// 終点までの進み方(Linear: 等速 / EaseInOut: 出だしと着地がゆっくり)。広がり方も同じカーブに従う
	ParticleCurve endTravelCurve = ParticleCurve::Linear;

	void DrawUi();
	void Shutdown();

	// 発生位置の中心。3Dはワールド座標、2Dはピクセル座標(左上が原点・Yは下向き)
	Vector3 position{ 0.0f, 0.0f, 0.0f };
	bool enabled = true;

private:
	/// このエミッタ1つぶんだけ描く(子は描かない)
	void DrawSelf(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;
	void CreateGpuResources(DirectXCommon& dx);
	void ApplyConfig(const ParticleConfig& config);
	/// effect_ に合わせて子エミッタ(1レイヤー = 1エミッタ)を用意し、設定を流し込む
	void SyncLayers();
	/// エフェクトの時刻を進め、各レイヤーの位置・発生のオン/オフを決める
	void UpdateTimeline(float deltaTime);
	/// 時刻を0に戻し、全レイヤーのバーストを撃ち直せる状態にする(粒は消さない)
	void RestartTimeline();
	void Spawn();
	float RandomRange(float minValue, float maxValue);

	ParticleConfig config_;       // 親レイヤーの設定(= effect_.layers[0].config のSanitize済みコピー)
	std::vector<Particle> particles_;

	// --- レイヤー(エフェクトの木) ---
	// 子レイヤー1つにつき、このクラスのインスタンスを1つ持つ(GPUバッファとSRVスロットも1つずつ)。
	// SRVスロットは解放できないので、子エミッタは一度作ったら捨てずに使い回す(レイヤーが減っても残す)
	DirectXCommon* dx_ = nullptr;
	ParticleEffect effect_;
	std::vector<std::unique_ptr<ParticleEmitter>> children_; // children_[i - 1] = レイヤー i
	size_t activeChildCount_ = 0;                            // 今使っている子の数 = effect_.layers.size() - 1
	std::vector<ParticleLayerWindow> windows_;
	struct LayerState
	{
		Vector3 position{};     // ワールド位置
		Vector3 captured{};     // followParent が false のとき、発生を始めた瞬間に覚えた位置
		bool wasActive = false; // 前フレームに発生中だったか(開始の瞬間を見つける)
		bool emitting = false;  // 今発生中か(親にミュートされていない + 窓の中)
	};
	std::vector<LayerState> layerStates_;                    // effect_.layers と同じ数([0] = 親)
	float effectTime_ = 0.0f;
	bool playing_ = true;
	bool emitting_ = true;        // false の間は新しい粒を出さない(タイムラインの窓の外)。出ている粒の動きは続く
	bool isLayerChild_ = false;   // 子レイヤーとして親に管理されている(自分では時刻を進めない)
	float emitAccumulator_ = 0.0f; // 発生数の端数を持ち越す
	ParticleSim::Scheduler scheduler_; // startDelay・バースト
	Vector3 lastPosition_{};           // エミッタの移動速度(inheritVelocity)を求めるための前フレームの位置
	Vector3 emitterVelocity_{};
	bool hasLastPosition_ = false;
	std::mt19937 rng_;

	// ライブラリとの連携
	const ParticleEmitter* endEmitter_ = nullptr;

	ParticleLibrary* library_ = nullptr;
	ParticleLibrary::Entry* entry_ = nullptr;
	uint32_t syncedRevision_ = 0;
	int libraryIndex_ = -1;
	std::string name_;

	// GPU側
	ResourceObject vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
	ResourceObject instanceBuffer_;
	ParticleInstanceData* instanceData_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};
	ResourceObject materialResource_; // ルートパラメータ[0]を未設定のまま描かないために持つ(シェーダーは読まない)
	uint32_t instanceCount_ = 0;
};
