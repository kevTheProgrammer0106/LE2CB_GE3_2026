#define NOMINMAX
#include "ParticleConfig.h"

#include <algorithm>
#include <format>
#include <map>
#include <sstream>
#include <utility>

//===================================================================
// 範囲の補正
//===================================================================
void ParticleConfig::Sanitize()
{
	// 列挙型が範囲外ならデフォルトに戻す
	if ( static_cast< int >( renderMode ) < 0 || static_cast< int >( renderMode ) >= static_cast< int >( ParticleRenderMode::Count_ ) )
	{
		renderMode = ParticleRenderMode::Billboard3D;
	}
	if ( static_cast< int >( blendMode ) < 0 || static_cast< int >( blendMode ) >= kBlendModeCount )
	{
		blendMode = ParticleBlendMode::Add;
	}
	if ( static_cast< int >( sizeCurve ) < 0 || static_cast< int >( sizeCurve ) >= static_cast< int >( ParticleCurve::Count_ ) )
	{
		sizeCurve = ParticleCurve::Linear;
	}
	if ( static_cast< int >( colorCurve ) < 0 || static_cast< int >( colorCurve ) >= static_cast< int >( ParticleCurve::Count_ ) )
	{
		colorCurve = ParticleCurve::Linear;
	}

	maxParticles = std::clamp(maxParticles, 1, kMaxParticlesLimit);
	emitRate = std::max(emitRate, 0.0f);

	spawnExtent.x = std::max(spawnExtent.x, 0.0f);
	spawnExtent.y = std::max(spawnExtent.y, 0.0f);
	spawnExtent.z = std::max(spawnExtent.z, 0.0f);

	// 最小 > 最大 になっていたら入れ替える
	if ( lifetimeMin > lifetimeMax )
	{
		std::swap(lifetimeMin, lifetimeMax);
	}
	lifetimeMin = std::max(lifetimeMin, 0.01f); // 0秒だと寿命の割り算が壊れる
	lifetimeMax = std::max(lifetimeMax, lifetimeMin);

	if ( speedMin > speedMax )
	{
		std::swap(speedMin, speedMax);
	}
	speedMin = std::max(speedMin, 0.0f);
	speedMax = std::max(speedMax, speedMin);

	spreadAngle = std::clamp(spreadAngle, 0.0f, 180.0f);
	drag = std::max(drag, 0.0f);

	sizeStart = std::max(sizeStart, 0.0f);
	sizeEnd = std::max(sizeEnd, 0.0f);

	if ( rotationMin > rotationMax )
	{
		std::swap(rotationMin, rotationMax);
	}
	if ( rotationSpeedMin > rotationSpeedMax )
	{
		std::swap(rotationSpeedMin, rotationSpeedMax);
	}
}

//===================================================================
// プリセット
//===================================================================
ParticleConfig ParticleConfig::MakeDefault()
{
	// メンバーの初期値そのまま
	return ParticleConfig{};
}

ParticleConfig ParticleConfig::MakeFire()
{
	ParticleConfig c;
	c.renderMode = ParticleRenderMode::Billboard3D;
	c.blendMode = ParticleBlendMode::Add;
	c.maxParticles = 512;

	c.emitRate = 80.0f;
	c.looping = true;
	c.spawnExtent = { 0.6f, 0.1f, 0.6f };

	c.lifetimeMin = 1.2f;
	c.lifetimeMax = 2.2f;
	c.speedMin = 1.5f;
	c.speedMax = 2.5f;
	c.direction = { 0.0f, 1.0f, 0.0f };
	c.spreadAngle = 25.0f;
	c.gravity = { 0.0f, 1.0f, 0.0f }; // 炎は上へ加速する
	c.drag = 0.6f;

	c.sizeStart = 1.5f;
	c.sizeEnd = 0.0f;
	c.sizeCurve = ParticleCurve::Linear;

	c.rotationMin = 0.0f;
	c.rotationMax = 360.0f;
	c.rotationSpeedMin = -45.0f;
	c.rotationSpeedMax = 45.0f;

	// 旧ParticleEmitter: 生成時は黄色(高温)→消滅時は赤(低温・透明)
	c.colorStart = { 255.0f / 255.0f, 232.0f / 255.0f, 40.0f / 255.0f, 1.0f };
	c.colorEnd = { 255.0f / 255.0f, 0.0f, 0.0f, 0.0f };
	c.colorCurve = ParticleCurve::Linear;
	return c;
}

ParticleConfig ParticleConfig::MakeSparkle2D()
{
	ParticleConfig c;
	c.renderMode = ParticleRenderMode::Screen2D;
	c.blendMode = ParticleBlendMode::Add;
	c.maxParticles = 256;

	// 2Dでは位置・サイズ・速度の単位がピクセル
	c.emitRate = 40.0f;
	c.looping = true;
	c.spawnExtent = { 10.0f, 10.0f, 0.0f };

	c.lifetimeMin = 0.6f;
	c.lifetimeMax = 1.2f;
	c.speedMin = 80.0f;
	c.speedMax = 200.0f;
	c.direction = { 0.0f, 1.0f, 0.0f }; // 画面の上
	c.spreadAngle = 70.0f;
	c.gravity = { 0.0f, -300.0f, 0.0f }; // 画面の下へ落ちる
	c.drag = 0.0f;

	c.sizeStart = 40.0f;
	c.sizeEnd = 0.0f;
	c.sizeCurve = ParticleCurve::EaseInOut;

	c.rotationMin = 0.0f;
	c.rotationMax = 360.0f;
	c.rotationSpeedMin = -180.0f;
	c.rotationSpeedMax = 180.0f;

	c.colorStart = { 1.0f, 0.85f, 0.3f, 1.0f };
	c.colorEnd = { 1.0f, 0.3f, 0.1f, 0.0f };
	c.colorCurve = ParticleCurve::Linear;
	return c;
}

//===================================================================
// テキスト入出力
//
// 1つの VisitConfig() に全項目を並べ、書き出し(TextWriter)と読み込み(TextReader)で共用する。
// 項目を足すときは VisitConfig() に1行足すだけでよい。
//===================================================================
namespace
{
	template <class Visitor>
	void VisitConfig(ParticleConfig& c, Visitor& v)
	{
		v.Enum("renderMode", c.renderMode, kRenderModeNames, static_cast< int >( ParticleRenderMode::Count_ ));
		v.Enum("blendMode", c.blendMode, kBlendModeNames, kBlendModeCount);
		v.Int("maxParticles", c.maxParticles);

		v.Float("emitRate", c.emitRate);
		v.Bool("looping", c.looping);
		v.Vec3("spawnExtent", c.spawnExtent);

		v.Float("lifetimeMin", c.lifetimeMin);
		v.Float("lifetimeMax", c.lifetimeMax);
		v.Float("speedMin", c.speedMin);
		v.Float("speedMax", c.speedMax);
		v.Vec3("direction", c.direction);
		v.Float("spreadAngle", c.spreadAngle);
		v.Vec3("gravity", c.gravity);
		v.Float("drag", c.drag);

		v.Float("sizeStart", c.sizeStart);
		v.Float("sizeEnd", c.sizeEnd);
		v.Enum("sizeCurve", c.sizeCurve, kCurveNames, static_cast< int >( ParticleCurve::Count_ ));

		v.Float("rotationMin", c.rotationMin);
		v.Float("rotationMax", c.rotationMax);
		v.Float("rotationSpeedMin", c.rotationSpeedMin);
		v.Float("rotationSpeedMax", c.rotationSpeedMax);

		v.Vec4("colorStart", c.colorStart);
		v.Vec4("colorEnd", c.colorEnd);
		v.Enum("colorCurve", c.colorCurve, kCurveNames, static_cast< int >( ParticleCurve::Count_ ));
	}

	// 設定 -> テキスト
	struct TextWriter
	{
		std::string out;

		void Float(const char* key, float& value)
		{
			out += std::format("{} = {}\n", key, value);
		}
		void Int(const char* key, int& value)
		{
			out += std::format("{} = {}\n", key, value);
		}
		void Bool(const char* key, bool& value)
		{
			out += std::format("{} = {}\n", key, value ? 1 : 0);
		}
		void Vec3(const char* key, Vector3& value)
		{
			out += std::format("{} = {} {} {}\n", key, value.x, value.y, value.z);
		}
		void Vec4(const char* key, Vector4& value)
		{
			out += std::format("{} = {} {} {} {}\n", key, value.x, value.y, value.z, value.w);
		}
		template <class E>
		void Enum(const char* key, E& value, const char* const* names, int count)
		{
			int index = static_cast< int >( value );
			if ( index < 0 || index >= count )
			{
				index = 0;
			}
			out += std::format("{} = {}\n", key, names[index]);
		}
	};

	// テキスト -> 設定(キーが無い項目は今の値のまま)
	struct TextReader
	{
		std::map<std::string, std::string> values;
		int matched = 0; // 読めたキーの数

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

		// 値の文字列から n 個の数値を読む。失敗したら false
		static bool ParseNumbers(const std::string& text, float* out, int n)
		{
			std::istringstream stream(text);
			for ( int i = 0; i < n; ++i )
			{
				if ( !( stream >> out[i] ) )
				{
					return false;
				}
			}
			return true;
		}

		void Float(const char* key, float& value)
		{
			if ( const std::string* s = Find(key) )
			{
				float parsed = value;
				if ( ParseNumbers(*s, &parsed, 1) )
				{
					value = parsed;
				}
			}
		}
		void Int(const char* key, int& value)
		{
			if ( const std::string* s = Find(key) )
			{
				std::istringstream stream(*s);
				int parsed = value;
				if ( stream >> parsed )
				{
					value = parsed;
				}
			}
		}
		void Bool(const char* key, bool& value)
		{
			if ( const std::string* s = Find(key) )
			{
				if ( *s == "true" )
				{
					value = true;
				} else if ( *s == "false" )
				{
					value = false;
				} else
				{
					std::istringstream stream(*s);
					int parsed = value ? 1 : 0;
					if ( stream >> parsed )
					{
						value = ( parsed != 0 );
					}
				}
			}
		}
		void Vec3(const char* key, Vector3& value)
		{
			if ( const std::string* s = Find(key) )
			{
				float parsed[3] = { value.x, value.y, value.z };
				if ( ParseNumbers(*s, parsed, 3) )
				{
					value = { parsed[0], parsed[1], parsed[2] };
				}
			}
		}
		void Vec4(const char* key, Vector4& value)
		{
			if ( const std::string* s = Find(key) )
			{
				float parsed[4] = { value.x, value.y, value.z, value.w };
				if ( ParseNumbers(*s, parsed, 4) )
				{
					value = { parsed[0], parsed[1], parsed[2], parsed[3] };
				}
			}
		}
		template <class E>
		void Enum(const char* key, E& value, const char* const* names, int count)
		{
			if ( const std::string* s = Find(key) )
			{
				// 名前で一致するものを探す。無ければ数値として読む
				for ( int i = 0; i < count; ++i )
				{
					if ( *s == names[i] )
					{
						value = static_cast< E >(i);
						return;
					}
				}
				std::istringstream stream(*s);
				int number = 0;
				if ( ( stream >> number ) && number >= 0 && number < count )
				{
					value = static_cast< E >(number);
				}
			}
		}
	};

	// 前後の空白を取り除く
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
}

std::string ParticleConfigSerializer::ToText(const ParticleConfig& config)
{
	ParticleConfig copy = config; // VisitConfigは参照で受けるのでコピーを渡す
	TextWriter writer;
	writer.out += "# Particle config (key = value)\n";
	VisitConfig(copy, writer);
	return writer.out;
}

bool ParticleConfigSerializer::FromText(const std::string& text, ParticleConfig& out)
{
	TextReader reader;

	// 行ごとに「キー = 値」へ分ける
	std::istringstream lines(text);
	std::string line;
	while ( std::getline(lines, line) )
	{
		const size_t hash = line.find('#');
		if ( hash != std::string::npos )
		{
			line.erase(hash); // コメントを捨てる
		}
		const size_t equal = line.find('=');
		if ( equal == std::string::npos )
		{
			continue;
		}
		const std::string key = Trim(line.substr(0, equal));
		const std::string value = Trim(line.substr(equal + 1));
		if ( !key.empty() )
		{
			reader.values[key] = value;
		}
	}

	ParticleConfig result = out;
	VisitConfig(result, reader);
	if ( reader.matched == 0 )
	{
		return false;
	}

	result.Sanitize();
	out = result;
	return true;
}