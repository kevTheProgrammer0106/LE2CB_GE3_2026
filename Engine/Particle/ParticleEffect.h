#pragma once

#include <string>
#include <vector>

#include "Engine/Particle/ParticleConfig.h"
#include "Engine/Math/WorldTransform.h"

//===================================================================
// パーティクルエフェクト = 「レイヤー」の木。
//
//   layers[0] が親(ルート)。GameSceneのエミッタの位置にあたる。
//   layers[1..] は子。parent で親レイヤーを指す(必ず自分より前の番号なので、ループは作れない)。
//   子の位置 = 親の位置 + offset(followParent が true の間は親に付いて動く)。
//   子の開始時刻 = 親の開始時刻 + startTime。親が有限の duration で終わるなら、子もそこで発生を止める。
//
// 例: 爆発 = 閃光(親) ─┬ 火花 (すぐ)
//                       ├ 煙   (0.15秒後)
//                       └ 残り火 (煙の0.5秒後。煙の子)
//
// 1レイヤーの中身(形・動き・色など)は今までの ParticleConfig。古い .particle ファイルは
// 「親だけのエフェクト」として読み込まれる。
//===================================================================

struct ParticleLayer
{
	std::string name = "Layer";
	int parent = -1;                  // 親レイヤーの番号(ルートは -1)
	Vector3 offset{ 0.0f, 0.0f, 0.0f }; // 親の位置からのずれ(3Dはワールド単位、2Dはピクセル)
	bool followParent = true;         // true: 親の動きに付いていく / false: 発生を始めた瞬間の位置に置いたまま
	bool enabled = true;              // false: ミュート(発生しない。出ている粒は消えるまで残る)
	float startTime = 0.0f;           // 親の開始からの待ち(秒)。ルートでは使わない(常に0)
	float duration = 0.0f;            // 発生を続ける長さ(秒)。0 = 終わらない(親が終わるまで)
	ParticleConfig config;
};

struct ParticleEffect
{
	static constexpr int kMaxLayers = 8; // レイヤー1つにつきGPUバッファとSRVスロットを1つ使うので、上限を決めておく

	std::vector<ParticleLayer> layers;   // 常に1つ以上(layers[0] = 親)
	bool loop = false;                   // true: length 秒たったら最初から繰り返す
	float length = 0.0f;                 // ループの長さ(秒)。0なら自動(有限なレイヤーが全部終わる時刻)

	/// 親だけの空のエフェクト
	static ParticleEffect FromConfig(const ParticleConfig& rootConfig);

	ParticleConfig& Root()
	{
		return layers[0].config;
	}
	const ParticleConfig& Root() const
	{
		return layers[0].config;
	}

	/// 範囲外の値・親番号の矛盾を直す。読み込み・適用の前に必ず通す
	void Sanitize();

	/// parent の子として新しいレイヤーを足す。足せたら新しい番号、上限なら -1
	int AddLayer(int parent, const std::string& name, const ParticleConfig& config);
	/// index のレイヤーを消す(ルートは消せない)。子は消したレイヤーの親につなぎ直す
	bool RemoveLayer(int index);
	/// ルートからの深さ(ルート = 0)
	int GetDepth(int index) const;
	/// index の子孫かどうか(自分自身も含む)
	bool IsDescendantOf(int index, int ancestor) const;
};

/// 1レイヤーが発生している期間(エフェクト開始からの絶対時刻)
struct ParticleLayerWindow
{
	float start = 0.0f;
	float end = 0.0f;       // 無限のときは kInfinite
	bool infinite = true;   // true: 終わらない
};
inline constexpr float kParticleInfinite = 1.0e30f;

namespace ParticleTimeline
{
	/// 全レイヤーの発生期間を求める(親の期間に収まるよう、子は親の終了で打ち切られる)
	std::vector<ParticleLayerWindow> ComputeWindows(const ParticleEffect& effect);
	/// 有限なレイヤーが全部終わる時刻。全部無限なら 0
	float ComputeLength(const ParticleEffect& effect);
	/// 時刻 t にレイヤーが発生中か
	bool IsActive(const ParticleLayerWindow& window, float t);
}

//===================================================================
// テキスト入出力(resources/particles/名前.particle)
//
// ファイルの先頭 = 親レイヤー(今までの書式そのまま + エフェクトの追加キー)。
// 子レイヤーは [layer] の行から始まる区間に、同じ書式で書く。
//   effectLoop = 1        # エフェクト全体のキー(先頭の区間に書く)
//   effectLength = 3.0
//   layerName = Smoke     # レイヤーのキー(各区間に書く)
//   layerParent = 0
//   layerOffset = 0 1 0
//   layerFollow = 1
//   layerEnabled = 1
//   layerStart = 0.15
//   layerDuration = 0
//===================================================================
class ParticleEffectSerializer
{
public:
	ParticleEffectSerializer() = delete;

	static std::string ToText(const ParticleEffect& effect);
	/// 1つも読めなかったときは false を返し、out は変更しない。無いキーは out の今の値のまま残る
	static bool FromText(const std::string& text, ParticleEffect& out);
};

//===================================================================
// 重ね合わせ済みのエフェクト見本(エディタの「Effect Presets」・GameSceneの初期値に使う)
//===================================================================
namespace ParticleEffectPresets
{
	/// 閃光(親) → 火花 / 煙 / 残り火。Looping なしの1回きり(Play で再生)
	ParticleEffect MakeExplosionCombo();
	/// 炎(親) → 立ちのぼる煙 / 火の粉。ずっと続く
	ParticleEffect MakeCampfire();
	/// 光の玉(親) → 周回する光 / 尾を引く粒。ずっと続く
	ParticleEffect MakeMagicOrb();
}
