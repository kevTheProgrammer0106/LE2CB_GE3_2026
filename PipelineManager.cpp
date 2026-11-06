#include "PipelineManager.h"

#include <cassert>

#include "DirectXCommon.h"

using Microsoft::WRL::ComPtr;

//===================================================================
// ルートシグネチャ
//===================================================================
ComPtr<ID3D12RootSignature> PipelineManager::CreateRootSignature(ID3D12Device* device)
{
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// テクスチャ用 (PS t0)
	D3D12_DESCRIPTOR_RANGE textureRange[1] = {};
	textureRange[0].BaseShaderRegister = 0;
	textureRange[0].NumDescriptors = 1;
	textureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// インスタンシング用 (VS t0)
	D3D12_DESCRIPTOR_RANGE instancingRange[1] = {};
	instancingRange[0].BaseShaderRegister = 0;
	instancingRange[0].NumDescriptors = 1;
	instancingRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	instancingRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// [0] Material(PS b0) [1] TransformationMatrix(VS b0) [2] Texture(PS table)
	// [3] DirectionalLight(PS b1) [4] Instancing(VS table)
	D3D12_ROOT_PARAMETER rootParameters[5] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = textureRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(textureRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[4].DescriptorTable.pDescriptorRanges = instancingRange;
	rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(instancingRange);

	desc.pParameters = rootParameters;
	desc.NumParameters = _countof(rootParameters);

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.pStaticSamplers = staticSamplers;
	desc.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	ComPtr<ID3DBlob> signatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if ( FAILED(hr) )
	{
		Logger::Log(reinterpret_cast< char* >( errorBlob->GetBufferPointer() ));
		assert(false);
	}

	ComPtr<ID3D12RootSignature> rootSignature;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));
	return rootSignature;
}

//===================================================================
// PSO
//===================================================================
void PipelineManager::Initialize(DirectXCommon& dx, DXGI_FORMAT rtvFormat)
{
	ID3D12Device* device = dx.GetDevice();
	rootSignature_ = CreateRootSignature(device);

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// RasterizerState: 裏面も表示する
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// DepthStencilState: 近ければ描画する
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// パーティクルは深度書き込みをしない
	D3D12_DEPTH_STENCIL_DESC particleDepthDesc = depthStencilDesc;
	particleDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	// シェーダー
	ComPtr<IDxcBlob> vs = dx.CompileShader(L"Object3D.VS.hlsl", L"vs_6_0");
	ComPtr<IDxcBlob> ps = dx.CompileShader(L"Object3D.PS.hlsl", L"ps_6_0");
	ComPtr<IDxcBlob> particleVs = dx.CompileShader(L"Particle.VS.hlsl", L"vs_6_0");
	ComPtr<IDxcBlob> particlePs = dx.CompileShader(L"Particle.PS.hlsl", L"ps_6_0");
	assert(vs && ps && particleVs && particlePs);

	// ベースのPSO設定(モデル/スプライト用)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC baseDesc{};
	baseDesc.pRootSignature = rootSignature_.Get();
	baseDesc.InputLayout = inputLayoutDesc;
	baseDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	baseDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	baseDesc.RasterizerState = rasterizerDesc;
	baseDesc.NumRenderTargets = 1;
	baseDesc.RTVFormats[0] = rtvFormat;
	baseDesc.DepthStencilState = depthStencilDesc;
	baseDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	baseDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	baseDesc.SampleDesc.Count = 1;
	baseDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// スプライト: 通常のアルファブレンド
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = baseDesc;
		desc.BlendState = CreateParticleBlendDesc(ParticleBlendMode::Normal);
		HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&sprite_));
		assert(SUCCEEDED(hr));
		( void ) hr;
	}

	// ブレンドモードごとのPSO
	for ( int i = 0; i < kBlendModeCount; ++i )
	{
		const D3D12_BLEND_DESC blend = CreateParticleBlendDesc(static_cast< ParticleBlendMode >(i));

		// モデル用
		D3D12_GRAPHICS_PIPELINE_STATE_DESC modelDesc = baseDesc;
		modelDesc.BlendState = blend;
		HRESULT hr = device->CreateGraphicsPipelineState(&modelDesc, IID_PPV_ARGS(&model_[i]));
		assert(SUCCEEDED(hr));

		// インスタンシング用(Particle.VS: StructuredBufferから行列を読む)
		D3D12_GRAPHICS_PIPELINE_STATE_DESC instancingDesc = baseDesc;
		instancingDesc.VS = { particleVs->GetBufferPointer(), particleVs->GetBufferSize() };
		instancingDesc.PS = { particlePs->GetBufferPointer(), particlePs->GetBufferSize() };
		instancingDesc.DepthStencilState = particleDepthDesc;
		instancingDesc.BlendState = blend;
		hr = device->CreateGraphicsPipelineState(&instancingDesc, IID_PPV_ARGS(&instancing_[i]));
		assert(SUCCEEDED(hr));

		// エミッタ用(Object3D.VS: パーティクルごとのCBVから行列を読む + Particle.PS)
		D3D12_GRAPHICS_PIPELINE_STATE_DESC emitterDesc = baseDesc;
		emitterDesc.PS = { particlePs->GetBufferPointer(), particlePs->GetBufferSize() };
		emitterDesc.DepthStencilState = particleDepthDesc;
		emitterDesc.BlendState = blend;
		hr = device->CreateGraphicsPipelineState(&emitterDesc, IID_PPV_ARGS(&emitter_[i]));
		assert(SUCCEEDED(hr));
		( void ) hr;

		emitterRaw_[i] = emitter_[i].Get();
	}
}