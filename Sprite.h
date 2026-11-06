#pragma once

#include <d3d12.h>

#include "ResourceObject.h"
#include "WorldTransform.h"

class DirectXCommon;
class PipelineManager;

/// <summary>
/// スプライト(画面左上のクワッド)。カメラの影響を受けず、正射影で描く
/// </summary>
class Sprite
{
public:
	void Initialize(DirectXCommon& dx, D3D12_GPU_DESCRIPTOR_HANDLE textureSrv);
	void Update(float clientWidth, float clientHeight);
	/// enabled が false のときは何も積まない
	void Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;
	void DrawUi();

	bool enabled = false;
	TransformData transform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	TransformData uvTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

private:
	ResourceObject vertexResource_;
	ResourceObject indexResource_;
	ResourceObject materialResource_;
	ResourceObject transformResource_;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
	D3D12_INDEX_BUFFER_VIEW ibv_{};
	Material* materialData_ = nullptr;
	TransformationMatrix* transformData_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv_{};
};