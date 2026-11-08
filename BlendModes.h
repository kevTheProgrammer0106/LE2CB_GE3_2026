#pragma once

#include <d3d12.h>

// -------------------------------------------------------
// ブレンドモード
// ImGuiのコンボ(kBlendModeItems)・名前表(kBlendModeNames)と並びを同期させること
// -------------------------------------------------------
enum class ParticleBlendMode
{
	None,       // ブレンドなし（不透明）
	Normal,     // Src*SrcA + Dest*(1-SrcA)          — 標準合成
	Add,        // Src*SrcA + Dest*1                  — 炎・発光など
	Subtract,   // Dest*1   - Src*SrcA                — 影・侵食など
	Multiply,   // Dest*Src                           — 焼き込み
	Screen,     // Src*(1-Dest) + Dest*1              — ソフトライト
	Exclusion,  // (1-Dest)*Src + (1-Src)*Dest        — サイケデリック効果

	Count_      // 番兵値 — 使用禁止
};

// ImGuiのコンボ用
inline constexpr const char* kBlendModeItems = "None\0Normal\0Add\0Subtract\0Multiply\0Screen\0Exclusion\0";

// 設定ファイル(.particle)に書き出す名前
inline constexpr const char* kBlendModeNames[] = {
	"None", "Normal", "Add", "Subtract", "Multiply", "Screen", "Exclusion",
};

inline constexpr int kBlendModeCount = static_cast< int >( ParticleBlendMode::Count_ );

static_assert( sizeof(kBlendModeNames) / sizeof(kBlendModeNames[0]) == kBlendModeCount,
	"kBlendModeNames must match ParticleBlendMode" );

// 指定したモードに対応する D3D12_BLEND_DESC を生成する。
// PSO を作成する際に、モードごとに一度だけ呼び出すこと。
D3D12_BLEND_DESC CreateParticleBlendDesc(ParticleBlendMode mode);