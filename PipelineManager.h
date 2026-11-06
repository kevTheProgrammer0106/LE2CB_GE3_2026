#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "BlendModes.h"

class DirectXCommon;

/// <summary>
/// ルートシグネチャ + PSO一式。ウィンドウ間で共有する(RTVフォーマットが同じ前提)。
///
/// PSOの種類:
///   - Sprite     : スプライト用(通常のアルファブレンド)
///   - Model      : モデル用(ブレンドモードごと)
///   - Instancing : Particle.VS(StructuredBufferで行列を受け取る)+ Particle.PS。インスタンシング描画用
///   - Emitter    : Object3D.VS(CBVで行列を受け取る)+ Particle.PS。ParticleEmitter::Draw 用。
///                  ParticleEmitterはパーティクルごとにCBVを張るので、Particle.VSでは読み先が合わない
/// </summary>
class PipelineManager
{
public:
	void Initialize(DirectXCommon& dx, DXGI_FORMAT rtvFormat);

	ID3D12RootSignature* GetRootSignature() const
	{
		return rootSignature_.Get();
	}
	ID3D12PipelineState* GetSpritePso() const
	{
		return sprite_.Get();
	}
	ID3D12PipelineState* GetModelPso(ParticleBlendMode mode) const
	{
		return model_[static_cast< int >( mode )].Get();
	}
	ID3D12PipelineState* GetInstancingPso(ParticleBlendMode mode) const
	{
		return instancing_[static_cast< int >( mode )].Get();
	}
	/// ParticleEmitter::Draw に渡す用(ブレンドモードでインデックスする配列)
	ID3D12PipelineState* const* GetEmitterPsoTable() const
	{
		return emitterRaw_;
	}

private:
	static Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sprite_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> model_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> instancing_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> emitter_[kBlendModeCount];
	ID3D12PipelineState* emitterRaw_[kBlendModeCount] = {};
};