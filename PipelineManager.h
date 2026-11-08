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
///   - Particle3d : ParticleInstance.VS/PS(StructuredBufferで行列+色を受け取る)。深度テストあり・深度書き込みなし
///   - Particle2d : 同じシェーダーで深度テストなし。画面に直接重ねる2Dパーティクル用
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
	/// 3Dビルボードのパーティクル用
	ID3D12PipelineState* GetParticle3dPso(ParticleBlendMode mode) const
	{
		return particle3d_[static_cast< int >( mode )].Get();
	}
	/// 2Dスクリーンのパーティクル用
	ID3D12PipelineState* GetParticle2dPso(ParticleBlendMode mode) const
	{
		return particle2d_[static_cast< int >( mode )].Get();
	}

private:
	static Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sprite_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> model_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> instancing_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> particle3d_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> particle2d_[kBlendModeCount];
};