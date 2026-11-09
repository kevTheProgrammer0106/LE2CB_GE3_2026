#include "BlendModes.h"

D3D12_BLEND_DESC CreateParticleBlendDesc(ParticleBlendMode mode)
{
	D3D12_BLEND_DESC desc{};
	D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];

	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	switch ( mode )
	{
		case ParticleBlendMode::None:
			rt.BlendEnable = FALSE;     // ブレンド無効
			break;

		case ParticleBlendMode::Normal:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
			rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;   // デスティネーションに (1-α) 乗算
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		case ParticleBlendMode::Add:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
			rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま加算
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		case ParticleBlendMode::Subtract:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;        // ソースにアルファ乗算
			rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま保持
			rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;   // Dest - Src（逆減算）
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		case ParticleBlendMode::Multiply:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_DEST_COLOR;       // デスティネーションの色を掛け合わせる
			rt.DestBlend = D3D12_BLEND_ZERO;            // デスティネーション成分は破棄
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		case ParticleBlendMode::Screen:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;   // ソースに (1-Dest) を乗算
			rt.DestBlend = D3D12_BLEND_ONE;             // デスティネーションをそのまま保持
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		case ParticleBlendMode::Exclusion:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;   // ソースに (1-Dest) を乗算
			rt.DestBlend = D3D12_BLEND_INV_SRC_COLOR;   // デスティネーションに (1-Src) を乗算
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;

		default:
			rt.BlendEnable = FALSE;     // 未知のモードはブレンドなしにフォールバック
			break;
	}
	return desc;
}