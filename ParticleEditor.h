#pragma once

#include "IScene.h"
#include "LightBuffer.h"
#include "Particle.h"
#include "SceneCamera.h"

class DirectXCommon;

/// <summary>
/// パーティクルエディタウィンドウの中身。専用のカメラ・エミッタ・ライトを持つ独立したワークスペース。
/// ゲームウィンドウのシーンとは何も共有しない(共有するのは読み取り専用のAssetCacheとPSOだけ)
/// </summary>
class ParticleEditor : public IScene
{
public:
	void Initialize(DirectXCommon& dx);

	void DrawUi() override;
	void Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight) override;
	void Draw(const RenderContext& context) override;
	void Shutdown() override;

private:
	SceneCamera camera_;
	// ルートパラメータ[3]を未設定のまま描かないために持つ(パーティクルのシェーダーは参照しない)
	LightBuffer light_;
	ParticleEmitter emitter_;

	bool paused_ = false;
};