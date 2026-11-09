#pragma once
#include <d3d12.h>

#include "ResourceObject.h"
#include "WorldTransform.h"

class DirectXCommon;

/// <summary>
/// 平行光源の定数バッファ(常時マップ)。シーンごとに1つ持つ
/// </summary>
class LightBuffer
{
public:
	void Initialize(DirectXCommon& dx);
	void DrawUi();

	DirectionalLight* Get() const
	{
		return data_;
	}
	D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const
	{
		return resource_.Get()->GetGPUVirtualAddress();
	}

private:
	ResourceObject resource_;
	DirectionalLight* data_ = nullptr;
};