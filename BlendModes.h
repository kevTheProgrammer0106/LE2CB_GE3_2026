#pragma once
#include "Particle.h"

// ImGuiのコンボ用。ParticleBlendModeの並びと同期させること
inline constexpr const char* kBlendModeItems = "None\0Normal\0Add\0Subtract\0Multiply\0Screen\0Exclusion\0";

inline constexpr int kBlendModeCount = static_cast< int >( ParticleBlendMode::Count_ );