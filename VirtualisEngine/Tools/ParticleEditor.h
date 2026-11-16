#pragma once

#include <string>

#include "VirtualisEngine/Graphics/Camera.h"
#include "VirtualisEngine/Graphics/DebugDraw.h"
#include "VirtualisEngine/Core/IScene.h"
#include "VirtualisEngine/Graphics/LightBuffer.h"
#include "VirtualisEngine/Particle/Particle.h"
#include "VirtualisEngine/Particle/ParticleConfig.h"
#include "VirtualisEngine/Particle/ParticleEffect.h"
#include "VirtualisEngine/Particle/ParticleLibrary.h"

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

	/// 選択中のレイヤーの設定(selectedLayer_ は常に範囲内に保つ)
	ParticleConfig& SelectedConfig();
	/// 構造(レイヤーの増減・親子)が変わった後に呼ぶ。番号を範囲内に直す
	void ClampSelectedLayer();
	/// レイヤーの木(親子関係を字下げで表示)と追加・削除ボタン。何か変えたら true
	bool DrawLayerTree();
	/// レイヤーごとの発生期間バー + 再生位置。クリックでレイヤー選択
	void DrawTimeline();
	/// 選択中レイヤーの親子関係・タイミングの入力欄。何か変えたら true
	bool DrawLayerSettings();
	/// ギズモ(発生の形・向き・広がり・床・レイヤーの原点と親子線)を線として積む
	void BuildGizmos(float clientWidth, float clientHeight);
	/// 1レイヤーぶんの形・向き・床のギズモ。3Dはワールド、2Dはピクセル座標の線
	void BuildLayerGizmo(DebugDraw& draw, const ParticleConfig& config, const Vector3& origin, bool selected);

	ParticleLibrary* library_ = nullptr;

	Camera camera_;
	// ルートパラメータ[3]を未設定のまま描かないために持つ(パーティクルのシェーダーは参照しない)
	LightBuffer light_;
	ParticleEmitter preview_; // プレビュー専用(ライブラリとは繋がらない)

	// 床のグリッド(エディタ専用のDebugDraw。パーティクルより先に描いて、粒が線の上に重なるようにする)
	DebugDraw grid_;
	bool showGrid_ = true;
	float gridHalfWidth_ = 10.0f;
	int gridSubdivision_ = 20;      // 半幅10・分割20 → 1マス1.0
	float gridHeight_ = 0.0f;       // グリッドのY位置(0 = ワールド原点を通る平面)

	// ギズモ(パーティクルのあとに描く。深度を使わない線なので、粒の上に重なって見える)
	DebugDraw gizmo3d_;               // カメラのビュー射影で描く
	DebugDraw gizmo2d_;               // 2Dレイヤー用(ピクセル座標の正射影)
	bool showGizmos_ = true;
	bool gizmoShape_ = true;          // 発生の形
	bool gizmoDirection_ = true;      // 発射方向・広がり
	bool gizmoGround_ = true;         // 床
	bool gizmoLayers_ = true;         // レイヤーの原点と親子線
	bool gizmoAllLayers_ = false;     // false: 選択中だけ形を出す / true: 全レイヤーの形を出す

	// 自動リプレイ: 1回きり(Burst Once)のエフェクトは鳴り終わると静かになるので、一定時間後にもう一度鳴らす
	bool autoReplay_ = true;
	float replayDelay_ = 0.75f;       // 鳴り終わってから次の再生までの待ち(秒)
	float idleTimer_ = 0.0f;

	int selected_ = -1;
	int selectedLayer_ = 0;           // 編集中のレイヤー(0 = 親)
	ParticleEffect working_;          // 編集中の値(全レイヤー)
	uint32_t loadedRevision_ = 0;     // working_ を読み込んだ時点のEntryのrevision
	bool dirty_ = false;              // Apply・保存していない変更がある
	bool autoApply_ = false;
	bool paused_ = false;
	Vector3 preview3dPosition_{ 0.0f, 0.0f, 0.0f }; // 原点(グリッドの中心と同じ場所)
	std::string status_;              // 「Applied」などの結果表示
};
