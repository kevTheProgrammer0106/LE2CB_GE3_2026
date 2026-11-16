#define NOMINMAX
#include "VirtualisEngine/Particle/ParticleEffect.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <sstream>

//===================================================================
// ParticleEffect
//===================================================================
ParticleEffect ParticleEffect::FromConfig(const ParticleConfig& rootConfig)
{
	ParticleEffect effect;
	ParticleLayer root;
	root.name = "Parent";
	root.parent = -1;
	root.config = rootConfig;
	effect.layers.push_back(root);
	return effect;
}

void ParticleEffect::Sanitize()
{
	if ( layers.empty() )
	{
		layers.push_back(ParticleLayer{});
		layers[0].name = "Parent";
		layers[0].config = ParticleConfig::MakeDefault();
	}
	if ( static_cast< int >( layers.size() ) > kMaxLayers )
	{
		layers.resize(kMaxLayers);
	}

	for ( size_t i = 0; i < layers.size(); ++i )
	{
		ParticleLayer& layer = layers[i];
		layer.config.Sanitize();

		// 名前は1行の文字列(ファイルに書くので改行と、コメント開始の # を除く)
		layer.name.erase(std::remove_if(layer.name.begin(), layer.name.end(), [](char c) { return c == '\n' || c == '\r' || c == '#'; }), layer.name.end());
		if ( layer.name.empty() )
		{
			layer.name = ( i == 0 ) ? "Parent" : "Layer";
		}

		if ( i == 0 )
		{
			layer.parent = -1;
			layer.startTime = 0.0f; // ルートはエフェクトの開始と同時
		} else if ( layer.parent < 0 || layer.parent >= static_cast< int >( i ) )
		{
			layer.parent = 0; // 親は必ず自分より前の番号(ループを作らない)
		}
		layer.startTime = std::max(layer.startTime, 0.0f);
		layer.duration = std::max(layer.duration, 0.0f);
	}
	length = std::max(length, 0.0f);
}

int ParticleEffect::AddLayer(int parent, const std::string& name, const ParticleConfig& config)
{
	if ( static_cast< int >( layers.size() ) >= kMaxLayers )
	{
		return -1;
	}
	ParticleLayer layer;
	layer.name = name;
	layer.parent = std::clamp(parent, 0, static_cast< int >( layers.size() ) - 1);
	layer.config = config;
	layers.push_back(layer);
	Sanitize();
	return static_cast< int >( layers.size() ) - 1;
}

bool ParticleEffect::RemoveLayer(int index)
{
	if ( index <= 0 || index >= static_cast< int >( layers.size() ) )
	{
		return false;
	}
	const int newParentForChildren = layers[index].parent;
	layers.erase(layers.begin() + index);

	// 番号がずれるので、親の参照を直す。消したレイヤーの子は、消したレイヤーの親につなぎ直す
	for ( size_t i = 1; i < layers.size(); ++i )
	{
		int& parent = layers[i].parent;
		if ( parent == index )
		{
			parent = newParentForChildren;
		} else if ( parent > index )
		{
			--parent;
		}
	}
	Sanitize();
	return true;
}

int ParticleEffect::GetDepth(int index) const
{
	int depth = 0;
	// 親は必ず前の番号なので、必ず終わる(念のため上限を付ける)
	for ( int guard = 0; guard < kMaxLayers && index > 0 && index < static_cast< int >( layers.size() ); ++guard )
	{
		index = layers[index].parent;
		++depth;
	}
	return depth;
}

bool ParticleEffect::IsDescendantOf(int index, int ancestor) const
{
	for ( int guard = 0; guard <= kMaxLayers && index >= 0 && index < static_cast< int >( layers.size() ); ++guard )
	{
		if ( index == ancestor )
		{
			return true;
		}
		index = layers[index].parent;
	}
	return false;
}

//===================================================================
// タイムライン
//===================================================================
namespace ParticleTimeline
{
	std::vector<ParticleLayerWindow> ComputeWindows(const ParticleEffect& effect)
	{
		std::vector<ParticleLayerWindow> windows(effect.layers.size());
		for ( size_t i = 0; i < effect.layers.size(); ++i )
		{
			const ParticleLayer& layer = effect.layers[i];
			ParticleLayerWindow& w = windows[i];

			const bool hasParent = ( i > 0 && layer.parent >= 0 && layer.parent < static_cast< int >( i ) );
			const ParticleLayerWindow* parent = hasParent ? &windows[layer.parent] : nullptr;

			w.start = ( parent ? parent->start + std::max(layer.startTime, 0.0f) : 0.0f );
			if ( layer.duration > 0.0f )
			{
				w.infinite = false;
				w.end = w.start + layer.duration;
			} else
			{
				w.infinite = true;
				w.end = kParticleInfinite;
			}

			// 親が有限なら、子はその終了で打ち切られる
			if ( parent && !parent->infinite )
			{
				w.end = std::min(w.end, parent->end);
				w.infinite = false;
				w.end = std::max(w.end, w.start); // 親が終わったあとに始まる子は、空の期間になる
			}
		}
		return windows;
	}

	float ComputeLength(const ParticleEffect& effect)
	{
		float length = 0.0f;
		for ( const ParticleLayerWindow& w : ComputeWindows(effect) )
		{
			if ( !w.infinite )
			{
				length = std::max(length, w.end);
			}
		}
		return length;
	}

	bool IsActive(const ParticleLayerWindow& window, float t)
	{
		return t >= window.start && ( window.infinite || t < window.end );
	}
}

//===================================================================
// テキスト入出力
//===================================================================
namespace
{
	std::string Trim(const std::string& text)
	{
		const char* kSpaces = " \t\r\n";
		const size_t first = text.find_first_not_of(kSpaces);
		if ( first == std::string::npos )
		{
			return std::string();
		}
		const size_t last = text.find_last_not_of(kSpaces);
		return text.substr(first, last - first + 1);
	}

	// 区間のレイヤー用キーだけを読む。1つでも読めたら matched が増える
	struct LayerKeys
	{
		std::map<std::string, std::string> values;
		int matched = 0;

		explicit LayerKeys(const std::string& chunk)
		{
			std::istringstream lines(chunk);
			std::string line;
			while ( std::getline(lines, line) )
			{
				const size_t hash = line.find('#');
				if ( hash != std::string::npos )
				{
					line.erase(hash);
				}
				const size_t equal = line.find('=');
				if ( equal == std::string::npos )
				{
					continue;
				}
				const std::string key = Trim(line.substr(0, equal));
				if ( !key.empty() )
				{
					values[key] = Trim(line.substr(equal + 1));
				}
			}
		}

		const std::string* Find(const char* key)
		{
			auto it = values.find(key);
			if ( it == values.end() )
			{
				return nullptr;
			}
			++matched;
			return &it->second;
		}
		void Float(const char* key, float& out)
		{
			if ( const std::string* s = Find(key) )
			{
				std::istringstream stream(*s);
				float v = out;
				if ( stream >> v )
				{
					out = v;
				}
			}
		}
		void Int(const char* key, int& out)
		{
			if ( const std::string* s = Find(key) )
			{
				std::istringstream stream(*s);
				int v = out;
				if ( stream >> v )
				{
					out = v;
				}
			}
		}
		void Bool(const char* key, bool& out)
		{
			int v = out ? 1 : 0;
			Int(key, v);
			if ( Find(key) ) // Int で数えた分と二重にならないよう、読めたかだけ見直す
			{
				--matched;
			}
			out = ( v != 0 );
		}
		void Vec3(const char* key, Vector3& out)
		{
			if ( const std::string* s = Find(key) )
			{
				std::istringstream stream(*s);
				float v[3] = { out.x, out.y, out.z };
				if ( stream >> v[0] >> v[1] >> v[2] )
				{
					out = { v[0], v[1], v[2] };
				}
			}
		}
		void String(const char* key, std::string& out)
		{
			if ( const std::string* s = Find(key) )
			{
				if ( !s->empty() )
				{
					out = *s;
				}
			}
		}
	};

	void ReadLayerKeys(LayerKeys& keys, ParticleLayer& layer)
	{
		keys.String("layerName", layer.name);
		keys.Int("layerParent", layer.parent);
		keys.Vec3("layerOffset", layer.offset);
		keys.Bool("layerFollow", layer.followParent);
		keys.Bool("layerEnabled", layer.enabled);
		keys.Float("layerStart", layer.startTime);
		keys.Float("layerDuration", layer.duration);
	}

	void WriteLayerKeys(std::string& out, const ParticleLayer& layer, bool isRoot)
	{
		out += std::format("layerName = {}\n", layer.name);
		if ( !isRoot )
		{
			out += std::format("layerParent = {}\n", layer.parent);
			out += std::format("layerOffset = {} {} {}\n", layer.offset.x, layer.offset.y, layer.offset.z);
			out += std::format("layerFollow = {}\n", layer.followParent ? 1 : 0);
			out += std::format("layerStart = {}\n", layer.startTime);
		}
		out += std::format("layerEnabled = {}\n", layer.enabled ? 1 : 0);
		out += std::format("layerDuration = {}\n", layer.duration);
	}
}

std::string ParticleEffectSerializer::ToText(const ParticleEffect& effect)
{
	ParticleEffect copy = effect;
	copy.Sanitize();

	std::string out;
	out += "# Particle effect (layer 0 = parent, [layer] sections = children)\n";
	out += std::format("effectLoop = {}\n", copy.loop ? 1 : 0);
	out += std::format("effectLength = {}\n", copy.length);
	for ( size_t i = 0; i < copy.layers.size(); ++i )
	{
		if ( i > 0 )
		{
			out += "\n[layer]\n";
		}
		WriteLayerKeys(out, copy.layers[i], i == 0);
		out += ParticleConfigSerializer::ToText(copy.layers[i].config);
	}
	return out;
}

bool ParticleEffectSerializer::FromText(const std::string& text, ParticleEffect& out)
{
	// [layer] の行で区間に分ける。最初の区間が親
	std::vector<std::string> chunks(1);
	{
		std::istringstream lines(text);
		std::string line;
		while ( std::getline(lines, line) )
		{
			if ( Trim(line) == "[layer]" )
			{
				chunks.emplace_back();
				continue;
			}
			chunks.back() += line;
			chunks.back() += '\n';
		}
	}

	ParticleEffect result = out;
	if ( result.layers.empty() )
	{
		result = ParticleEffect::FromConfig(ParticleConfig::MakeDefault());
	}
	// 構造(子の数)はファイルが決める。子は既定値から読む
	result.layers.resize(1);

	// 親の区間: エフェクト全体のキー + レイヤーのキー + 設定(従来の書式)
	bool anyRead = false;
	{
		LayerKeys keys(chunks[0]);
		keys.Bool("effectLoop", result.loop);
		keys.Float("effectLength", result.length);
		ReadLayerKeys(keys, result.layers[0]);
		anyRead = ( keys.matched > 0 );
		anyRead |= ParticleConfigSerializer::FromText(chunks[0], result.layers[0].config);
	}

	for ( size_t i = 1; i < chunks.size() && static_cast< int >( result.layers.size() ) < ParticleEffect::kMaxLayers; ++i )
	{
		ParticleLayer layer;
		layer.name = "Layer";
		layer.parent = 0;
		layer.config = ParticleConfig::MakeDefault();
		LayerKeys keys(chunks[i]);
		ReadLayerKeys(keys, layer);
		ParticleConfigSerializer::FromText(chunks[i], layer.config);
		result.layers.push_back(layer);
		anyRead = true;
	}

	if ( !anyRead )
	{
		return false;
	}
	result.Sanitize();
	out = result;
	return true;
}

//===================================================================
// 見本
//===================================================================
namespace ParticleEffectPresets
{
	namespace
	{
		// 1回きり(Looping なし)にして、count 個を1度だけ出す
		void MakeOneShot(ParticleConfig& c, int count)
		{
			c.looping = false;
			c.burstOnce = true;
			c.burstCount = count;
		}

		// 小さな火の粉(上へ漂う)
		ParticleConfig MakeEmbers()
		{
			ParticleConfig c = ParticleConfig::MakeDefault();
			c.blendMode = ParticleBlendMode::Add;
			c.maxParticles = 128;
			c.emitRate = 8.0f;
			c.spawnExtent = { 0.5f, 0.1f, 0.5f };
			c.lifetimeMin = 1.5f;
			c.lifetimeMax = 3.0f;
			c.speedMin = 0.6f;
			c.speedMax = 1.6f;
			c.spreadAngle = 35.0f;
			c.gravity = { 0.0f, 0.5f, 0.0f };
			c.turbulence = 1.2f;
			c.turbulenceFrequency = 0.8f;
			c.sizeStart = 0.14f;
			c.sizeEnd = 0.0f;
			c.sizeRandomness = 0.4f;
			c.randomColor = true;
			c.colorStart = { 1.0f, 0.7f, 0.2f, 1.0f };
			c.colorStartB = { 1.0f, 0.35f, 0.05f, 1.0f };
			c.colorEnd = { 0.8f, 0.1f, 0.0f, 0.0f };
			return c;
		}
	}

	ParticleEffect MakeExplosionCombo()
	{
		// 親: 閃光(大きく広がってすぐ消える)
		ParticleConfig flash = ParticleConfig::MakeDefault();
		flash.blendMode = ParticleBlendMode::Add;
		flash.maxParticles = 8;
		flash.spawnExtent = { 0.0f, 0.0f, 0.0f };
		flash.lifetimeMin = 0.18f;
		flash.lifetimeMax = 0.25f;
		flash.speedMin = 0.0f;
		flash.speedMax = 0.0f;
		flash.sizeStart = 0.6f;
		flash.sizeEnd = 7.0f;
		flash.sizeCurve = ParticleCurve::EaseOut;
		flash.colorStart = { 1.0f, 0.95f, 0.75f, 1.0f };
		flash.colorEnd = { 1.0f, 0.45f, 0.1f, 0.0f };
		MakeOneShot(flash, 3);

		ParticleEffect effect = ParticleEffect::FromConfig(flash);
		effect.layers[0].name = "Flash";

		// 子1: 火の玉(球の表面から外へ)
		ParticleConfig fireball = ParticleConfig::MakeExplosion();
		MakeOneShot(fireball, 120);
		const int fire = effect.AddLayer(0, "Fireball", fireball);
		effect.layers[fire].startTime = 0.0f;

		// 子2: 火花(地面で跳ねる)
		ParticleConfig sparks = ParticleConfig::MakeSparks();
		MakeOneShot(sparks, 70);
		sparks.speedMin = 6.0f;
		sparks.speedMax = 12.0f;
		sparks.spreadAngle = 70.0f;
		const int spark = effect.AddLayer(0, "Sparks", sparks);
		effect.layers[spark].startTime = 0.02f;

		// 子3: 煙(少し遅れて立ちのぼる)
		ParticleConfig smoke = ParticleConfig::MakeSmoke();
		MakeOneShot(smoke, 36);
		smoke.shape = ParticleEmitShape::Sphere;
		smoke.shapeRadius = 0.6f;
		smoke.shapeThickness = 1.0f;
		const int smk = effect.AddLayer(0, "Smoke", smoke);
		effect.layers[smk].startTime = 0.15f;
		effect.layers[smk].offset = { 0.0f, 0.3f, 0.0f };

		// 孫: 残り火(煙のあとに、煙の位置から)
		ParticleConfig embers = MakeEmbers();
		MakeOneShot(embers, 30);
		embers.spawnExtent = { 0.8f, 0.3f, 0.8f };
		const int emb = effect.AddLayer(smk, "Embers", embers);
		effect.layers[emb].startTime = 0.35f;
		effect.layers[emb].offset = { 0.0f, 0.6f, 0.0f };

		effect.Sanitize();
		return effect;
	}

	ParticleEffect MakeCampfire()
	{
		ParticleEffect effect = ParticleEffect::FromConfig(ParticleConfig::MakeFire());
		effect.layers[0].name = "Fire";

		const int smoke = effect.AddLayer(0, "Smoke", ParticleConfig::MakeSmoke());
		effect.layers[smoke].offset = { 0.0f, 1.8f, 0.0f };
		effect.layers[smoke].startTime = 0.6f; // 炎が立ち上がってから煙が出る

		const int embers = effect.AddLayer(0, "Embers", MakeEmbers());
		effect.layers[embers].offset = { 0.0f, 0.4f, 0.0f };
		effect.layers[embers].startTime = 0.3f;

		effect.Sanitize();
		return effect;
	}

	ParticleEffect MakeMagicOrb()
	{
		// 親: 光の玉の芯
		ParticleConfig core = ParticleConfig::MakeDefault();
		core.blendMode = ParticleBlendMode::Add;
		core.maxParticles = 64;
		core.emitRate = 24.0f;
		core.spawnExtent = { 0.05f, 0.05f, 0.05f };
		core.lifetimeMin = 0.5f;
		core.lifetimeMax = 0.8f;
		core.speedMin = 0.0f;
		core.speedMax = 0.1f;
		core.sizeStart = 1.4f;
		core.sizeEnd = 0.6f;
		core.sizeCurve = ParticleCurve::Pulse;
		core.fadeIn = 0.3f;
		core.colorStart = { 0.4f, 0.8f, 1.0f, 0.9f };
		core.colorEnd = { 0.2f, 0.3f, 1.0f, 0.0f };

		ParticleEffect effect = ParticleEffect::FromConfig(core);
		effect.layers[0].name = "Core";

		// 子1: 周回する光(輪の上に出して、中心まわりを回す)
		ParticleConfig orbit = ParticleConfig::MakeDefault();
		orbit.blendMode = ParticleBlendMode::Add;
		orbit.maxParticles = 160;
		orbit.emitRate = 50.0f;
		orbit.shape = ParticleEmitShape::Circle;
		orbit.shapeRadius = 1.3f;
		orbit.shapeThickness = 0.0f;
		orbit.lifetimeMin = 1.0f;
		orbit.lifetimeMax = 1.4f;
		orbit.speedMin = 0.0f;
		orbit.speedMax = 0.0f;
		orbit.localSpace = true; // 親に付いて回る
		orbit.orbitSpeed = 200.0f;
		orbit.sizeStart = 0.22f;
		orbit.sizeEnd = 0.0f;
		orbit.colorStart = { 0.8f, 0.95f, 1.0f, 1.0f };
		orbit.colorEnd = { 0.3f, 0.5f, 1.0f, 0.0f };
		const int orbitLayer = effect.AddLayer(0, "Orbit", orbit);
		effect.layers[orbitLayer].startTime = 0.2f;

		// 子2: 飛び散る光の尾(速度に沿って伸びる)
		ParticleConfig trail = ParticleConfig::MakeDefault();
		trail.blendMode = ParticleBlendMode::Add;
		trail.maxParticles = 128;
		trail.emitRate = 18.0f;
		trail.lifetimeMin = 0.5f;
		trail.lifetimeMax = 0.9f;
		trail.speedMin = 2.0f;
		trail.speedMax = 3.5f;
		trail.spreadAngle = 180.0f;
		trail.drag = 1.2f;
		trail.alignToVelocity = true;
		trail.stretchScale = 0.4f;
		trail.stretchMax = 5.0f;
		trail.sizeStart = 0.2f;
		trail.sizeEnd = 0.02f;
		trail.colorStart = { 0.6f, 0.9f, 1.0f, 1.0f };
		trail.colorEnd = { 0.2f, 0.4f, 1.0f, 0.0f };
		const int trailLayer = effect.AddLayer(0, "Trail", trail);
		effect.layers[trailLayer].startTime = 0.4f;

		effect.Sanitize();
		return effect;
	}
}
