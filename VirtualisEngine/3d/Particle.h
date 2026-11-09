#pragma once

#include <d3d12.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "ParticleConfig.h"
#include "ParticleLibrary.h"
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
	/// 全部消す
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
	std::mt19937 rng_;

	// ライブラリとの連携
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