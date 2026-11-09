#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>

#include "Collider.h"
#include "ResourceObject.h"
#include "WorldTransform.h"

class DirectXCommon;
class PipelineManager;

/// <summary>
/// インスタンシング描画のデモ(StructuredBufferで行列を渡す板ポリ)
/// </summary>
class InstancingObject
{
public:
	static constexpr uint32_t kNumInstance = 10; // インスタンシング描画の数

	void Initialize(DirectXCommon& dx);
	void Update(const Matrix4x4& viewProjection);
	/// enabled が false のときは何も積まない
	void Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;
	void DrawUi();

	/// 全インスタンスをまとめた、軸に平行な範囲(ワールド座標)。標準の箱コライダーとして使う。
	/// 回転は無視して、板ポリ(-1〜1)の拡大・平行移動だけで見積もる
	AABB GetWorldBounds() const;

	bool enabled = true;

private:
	ResourceObject vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
	UINT vertexCount_ = 0;

	// スプライトのマテリアルを借りずに、専用のマテリアル(白・UV変換なし)を持つ
	ResourceObject materialResource_;
	Material* materialData_ = nullptr;

	ResourceObject instanceBuffer_;
	TransformationMatrix* data_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};
	std::array<TransformData, kNumInstance> transforms_{};
};