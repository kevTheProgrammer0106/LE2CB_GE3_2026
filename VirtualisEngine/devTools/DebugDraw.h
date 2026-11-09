#pragma once

#include <d3d12.h>

#include <cstdint>

#include "Collider.h"
#include "ResourceObject.h"
#include "WorldTransform.h"

class DirectXCommon;
class PipelineManager;
class AssetCache;

/// <summary>
/// デバッグ用の線描画(コライダーの枠・選択ハイライト・矢印など)。
/// 使い方は「毎フレーム Begin → 線を積む → Draw」。1回のDrawInstancedで全部の線を描く。
/// 頂点バッファは自分で持つので、ウィンドウ(シーン)ごとに別のインスタンスを作ること。
/// 深度テストなしで常に手前に見える(PipelineManager::GetDebugLinePso)
/// </summary>
class DebugDraw
{
public:
	/// 色の見本(用途ごと)
	struct Color
	{
		static constexpr Vector4 kCollider{ 0.2f, 1.0f, 0.2f, 1.0f };     // 緑: コライダー(Unityの既定と同じ)
		static constexpr Vector4 kSelected{ 1.0f, 0.55f, 0.1f, 1.0f };    // オレンジ: 選択中
		static constexpr Vector4 kHover{ 1.0f, 0.95f, 0.2f, 1.0f };       // 黄: カーソルが乗っている
		static constexpr Vector4 kEmitter{ 0.2f, 0.9f, 1.0f, 1.0f };      // 水色: パーティクルの発生範囲
		static constexpr Vector4 kObject{ 0.8f, 0.4f, 1.0f, 1.0f };       // 紫: その他
		static constexpr Vector4 kAxisX{ 1.0f, 0.2f, 0.2f, 1.0f };
		static constexpr Vector4 kAxisY{ 0.2f, 1.0f, 0.2f, 1.0f };
		static constexpr Vector4 kAxisZ{ 0.3f, 0.5f, 1.0f, 1.0f };
	};

	/// 1フレームに積める線の最大数
	static constexpr uint32_t kMaxLines = 8192;

	void Initialize(DirectXCommon& dx);
	/// 前フレームの線を捨てて、このフレームのviewProjectionを設定する(Updateで呼ぶ)
	void Begin(const Matrix4x4& viewProjection);

	void Line(const Vector3& a, const Vector3& b, const Vector4& color);
	/// 軸に平行な箱の12本の辺
	void Box(const AABB& box, const Vector4& color);
	void BoxAt(const Vector3& center, float halfX, float halfY, float halfZ, const Vector4& color);
	/// 回転・拡大したコライダー(OBB)の12本の辺
	void ColliderBox(const BoxCollider& collider, const Matrix4x4& world, const Vector4& color);
	/// XY平面の円
	void Circle(const Vector3& center, float radius, const Vector4& color, int segments = 48);
	/// a → b の線。bに矢じりを付けて向きが分かるようにする
	void Arrow(const Vector3& a, const Vector3& b, const Vector4& color);

	/// Begin ～ 線を積んだ後、描画の最後に呼ぶ(RootSignatureとDescriptorHeapは設定済みの前提)
	void Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, const AssetCache& assets,
		D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;
	void Shutdown();

private:
	struct DebugVertex
	{
		Vector4 position;
		Vector4 color;
	};

	ResourceObject vertexResource_;
	DebugVertex* mapped_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
	uint32_t vertexCount_ = 0;

	ResourceObject materialResource_; // ルートパラメータ[0]を未設定のまま描かないために持つ(シェーダーは読まない)
	ResourceObject wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};