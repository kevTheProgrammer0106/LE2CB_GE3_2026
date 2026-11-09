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
///   - Line       : 線(LINELIST)用。ShapeObjectのLine図形で使う(アンチエイリアスありとなしの2種類)
///   - DebugLine  : DebugDraw用。頂点カラー(DebugLine.VS/PS)、深度テストなし
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
	/// 線(LINELIST)を描くShapeObject用。それ以外はModelと同じ設定(通常のアルファブレンド)。
	/// smooth が true なら、線のアンチエイリアス(AntialiasedLineEnable)を有効にしたPSO
	ID3D12PipelineState* GetLinePso(bool smooth = false) const
	{
		return smooth ? lineSmooth_.Get() : line_.Get();
	}
	/// DebugDraw用。頂点に色を持つ線で、深度テストなし(常に手前に見える)・アンチエイリアスあり
	ID3D12PipelineState* GetDebugLinePso() const
	{
		return debugLine_.Get();
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> line_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> lineSmooth_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> debugLine_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> model_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> instancing_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> particle3d_[kBlendModeCount];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> particle2d_[kBlendModeCount];
};