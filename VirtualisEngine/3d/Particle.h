#pragma once

#include <d3d12.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "ParticleConfig.h"
#include "ParticleLibrary.h"
#include "ParticleSim.h"
#include "ResourceObject.h"
#include "WorldTransform.h"

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
static_assert( sizeof(ParticleInstanceData) == 80, "ParticleInstanceData must match the HLSL struct" );

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
	/// 全部消し、発生のタイミング(startDelay・バースト回数)も最初からにする
	void Clear();
	/// 単体用: 設定を直接差し替える(次の発生から反映)
	void SetConfig(const ParticleConfig& config);

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
	void CreateGpuResources(DirectXCommon& dx);
	void ApplyConfig(const ParticleConfig& config);
	void Spawn();
	float RandomRange(float minValue, float maxValue);

	ParticleConfig config_;
	std::vector<Particle> particles_;
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