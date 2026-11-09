#pragma once

#include <string>

#include "IScene.h"
#include "LightBuffer.h"
#include "Particle.h"
#include "ParticleConfig.h"
#include "ParticleLibrary.h"
#include "SceneCamera.h"

class DirectXCommon;

/// <summary>
/// パーティクルエディタウィンドウの中身。専用のカメラ・プレビュー・ライトを持つ独立したワークスペース。
///
/// GameSceneで宣言されたエミッタは ParticleLibrary に登録されていて、ここの一覧に現れる。
///   1. 一覧から選ぶ → 設定を「作業コピー」に読み込む(ゲームには影響しない)
///   2. 値をいじる   → プレビュー(このウィンドウ内だけ)がすぐ変わる
///   3. Apply を押す → ライブラリの設定を書き換える(revision が進む)
///                     → GameSceneのエミッタが検知して、次に発生する粒から新しい値になる
/// 「Auto Apply」をオンにすると、値を変えるたびに自動でApplyされる。
/// </summary>
class ParticleEditor : public IScene
{
public:
	void Initialize(DirectXCommon& dx, ParticleLibrary& library);

	void DrawUi() override;
	void Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight) override;
	void Draw(const RenderContext& context) override;
	void Shutdown() override;

	/// 一覧のindex番目を編集対象にする(ゲーム側の「Edit」ボタンから)
	void SelectEntry(int index);

private:
	/// 選択中のEntry(無ければnullptr)
	ParticleLibrary::Entry* GetSelected() const;
	/// 作業コピーが変わったので、プレビューに反映し、AutoApplyならゲームにも反映する
	void OnWorkingChanged();
	/// 作業コピーをゲームへ反映する(=ホットリロード)
	void ApplyToGame();
	/// プレビューの位置を描画方式に合わせる(3Dはワールド、2Dは画面中央付近)
	void UpdatePreviewPosition(float clientWidth, float clientHeight);

	ParticleLibrary* library_ = nullptr;

	SceneCamera camera_;
	// ルートパラメータ[3]を未設定のまま描かないために持つ(パーティクルのシェーダーは参照しない)
	LightBuffer light_;
	ParticleEmitter preview_; // プレビュー専用(ライブラリとは繋がらない)

	int selected_ = -1;
	ParticleConfig working_;          // 編集中の値
	uint32_t loadedRevision_ = 0;     // working_ を読み込んだ時点のEntryのrevision
	bool dirty_ = false;              // Apply・保存していない変更がある
	bool autoApply_ = false;
	bool paused_ = false;
	Vector3 preview3dPosition_{ 0.0f, -2.0f, 10.0f };
	std::string status_;              // 「Applied」などの結果表示
};