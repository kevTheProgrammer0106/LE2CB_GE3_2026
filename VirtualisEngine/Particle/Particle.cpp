#include "VirtualisEngine/Particle/Particle.h"
#include "VirtualisEngine/Graphics/GpuBuffers.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>

#include "VirtualisEngine/Graphics/DirectXCommon.h"
#include "VirtualisEngine/Math/Easing.h"
#include "VirtualisEngine/Graphics/PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace
{

	// カーブ・色の計算は ParticleSim にある
	using ParticleSim::ApplyCurve;

	// 0〜2π / ±π 付近まで安全に使える正規化(長さ0なら fallback)
	Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback)
	{
		const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if ( length < 1e-6f )
		{
			return fallback;
		}
		return { v.x / length, v.y / length, v.z / length };
	}
}

//===================================================================
// 初期化
//===================================================================
void ParticleEmitter::Initialize(DirectXCommon& dx, ParticleLibrary& library, const std::string& name, const ParticleConfig& defaultConfig)
{
	Initialize(dx, library, name, ParticleEffect::FromConfig(defaultConfig));
}

void ParticleEmitter::Initialize(DirectXCommon& dx, ParticleLibrary& library, const std::string& name, const ParticleEffect& defaultEffect)
{
	dx_ = &dx;
	library_ = &library;
	name_ = name;
	entry_ = library.Register(name, defaultEffect);
	libraryIndex_ = library.IndexOf(name);

	// ライブラリ(ファイルがあればその内容)のエフェクトで始める
	effect_ = entry_->effect;
	config_ = effect_.Root();
	syncedRevision_ = entry_->revision;

	CreateGpuResources(dx);
	SyncLayers();
	RestartTimeline();
}

void ParticleEmitter::Initialize(DirectXCommon& dx)
{
	library_ = nullptr;
	entry_ = nullptr;
	libraryIndex_ = -1;
	name_ = "Preview";

	dx_ = &dx;
	effect_ = ParticleEffect::FromConfig(ParticleConfig::MakeDefault());
	effect_.Sanitize();
	config_ = effect_.Root();

	CreateGpuResources(dx);
	SyncLayers();
	RestartTimeline();
}

void ParticleEmitter::CreateGpuResources(DirectXCommon& dx)
{
	std::random_device device;
	rng_.seed(device());

	particles_.reserve(ParticleConfig::kMaxParticlesLimit);

	// 板ポリ(三角形2枚)。-1〜1の正方形で、サイズは行列側の拡大で決める。
	// UVは「左上が(0,0)」になる向き(InstancingObjectの板ポリは左右反転しているので別に持つ)
	const VertexData vertices[] = {
		{ { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 左上
		{ {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
		{ {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 右下
	};

	VertexData* mappedVertices = nullptr;
	dx.CreateMappedBuffer(vertexResource_, sizeof(vertices), &mappedVertices);
	std::memcpy(mappedVertices, vertices, sizeof(vertices));

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.SizeInBytes = static_cast< UINT >( sizeof(vertices) );
	vbv_.StrideInBytes = sizeof(VertexData);

	// 未使用のルートパラメータ[0]用マテリアル(白)
	CreateMaterialBuffer(dx, materialResource_, LightingMode::None);

	// インスタンスバッファ。上限ぶんを最初に1回だけ確保する(設定のmaxParticlesはCPU側の発生上限)
	constexpr uint32_t kCapacity = ParticleConfig::kMaxParticlesLimit;
	dx.CreateMappedBuffer(instanceBuffer_, sizeof(ParticleInstanceData) * kCapacity, &instanceData_);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = kCapacity;
	srvDesc.Buffer.StructureByteStride = sizeof(ParticleInstanceData);

	// SRVスロットはエミッタ1つにつき1つ消費する(ヒープは128個)
	const uint32_t srvIndex = dx.AllocateSrvIndex();
	dx.GetDevice()->CreateShaderResourceView(instanceBuffer_.Get(), &srvDesc, dx.GetSRVCPUDescriptorHandle(srvIndex));
	srvGpu_ = dx.GetSRVGPUDescriptorHandle(srvIndex);
}

//===================================================================
// 設定
//===================================================================
void ParticleEmitter::ApplyConfig(const ParticleConfig& config)
{
	const ParticleRenderMode oldMode = config_.renderMode;
	config_ = config;
	config_.Sanitize();

	// 3Dと2Dでは座標の意味が違うので、描画方式が変わったら残っている粒は捨てる
	if ( config_.renderMode != oldMode )
	{
		particles_.clear();
		instanceCount_ = 0;
	}
}

void ParticleEmitter::SetConfig(const ParticleConfig& config)
{
	effect_.layers[0].config = config;
	effect_.Sanitize();
	ApplyConfig(effect_.Root());
	SyncLayers();
}

void ParticleEmitter::SetEffect(const ParticleEffect& effect)
{
	effect_ = effect;
	effect_.Sanitize();
	ApplyConfig(effect_.Root());
	SyncLayers();
}

//===================================================================
// レイヤー(エフェクトの木)
//===================================================================
void ParticleEmitter::SyncLayers()
{
	if ( isLayerChild_ || dx_ == nullptr )
	{
		return; // 子は自分の木を持たない
	}

	const size_t childCount = effect_.layers.size() - 1;

	// 足りない子エミッタを作る(SRVスロットは解放できないので、作ったら使い回す)
	while ( children_.size() < childCount )
	{
		auto child = std::make_unique<ParticleEmitter>();
		child->Initialize(*dx_);
		child->isLayerChild_ = true;
		children_.push_back(std::move(child));
	}

	// 使わなくなった子は粒を消して止める
	for ( size_t i = childCount; i < children_.size(); ++i )
	{
		children_[i]->Clear();
		children_[i]->emitting_ = false;
	}
	activeChildCount_ = childCount;

	// 設定を流し込む(すでに飛んでいる粒は、発生時のスナップショットのまま)
	for ( size_t i = 0; i < childCount; ++i )
	{
		children_[i]->SetConfig(effect_.layers[i + 1].config);
	}

	windows_ = ParticleTimeline::ComputeWindows(effect_);

	// 状態は作り直さず、数だけ合わせる(再生中の編集で、位置や「開始済み」が飛ばないように)
	layerStates_.resize(effect_.layers.size());
}

void ParticleEmitter::RestartTimeline()
{
	effectTime_ = 0.0f;
	playing_ = true;
	emitting_ = true;
	scheduler_.Reset();
	emitAccumulator_ = 0.0f;
	for ( LayerState& state : layerStates_ )
	{
		state.wasActive = false;
		state.emitting = false;
	}
	for ( size_t i = 0; i < activeChildCount_; ++i )
	{
		children_[i]->scheduler_.Reset();
		children_[i]->emitAccumulator_ = 0.0f;
		children_[i]->emitting_ = false;
	}
}

void ParticleEmitter::Play(bool clearParticles)
{
	if ( clearParticles )
	{
		particles_.clear();
		instanceCount_ = 0;
		for ( size_t i = 0; i < children_.size(); ++i )
		{
			children_[i]->particles_.clear();
			children_[i]->instanceCount_ = 0;
		}
	}
	RestartTimeline();
}

void ParticleEmitter::UpdateTimeline(float deltaTime)
{
	if ( playing_ )
	{
		effectTime_ += deltaTime;
	}

	// ループ: 長さを超えたら最初から(全レイヤーのバーストを撃ち直す)
	if ( effect_.loop )
	{
		const float length = ( effect_.length > 0.0f ) ? effect_.length : ParticleTimeline::ComputeLength(effect_);
		if ( length > 0.0f && effectTime_ >= length )
		{
			const float wrapped = std::fmod(effectTime_, length);
			RestartTimeline();
			effectTime_ = wrapped;
		}
	}

	if ( layerStates_.size() != effect_.layers.size() || windows_.size() != effect_.layers.size() )
	{
		return; // SyncLayers 前(通常は起きない)
	}

	// レイヤーごとに: 発生中か(窓の中 + 自分も親もミュートされていない)・ワールド位置
	std::vector<char> enabledChain(effect_.layers.size(), 1);
	for ( size_t i = 0; i < effect_.layers.size(); ++i )
	{
		const ParticleLayer& layer = effect_.layers[i];
		LayerState& state = layerStates_[i];

		bool parentEnabled = true;
		Vector3 parentPosition = position;
		if ( i > 0 )
		{
			parentEnabled = ( enabledChain[layer.parent] != 0 );
			parentPosition = layerStates_[layer.parent].position;
		}
		enabledChain[i] = ( layer.enabled && parentEnabled ) ? 1 : 0;

		const bool active = ( enabledChain[i] != 0 ) && ParticleTimeline::IsActive(windows_[i], effectTime_);
		const Vector3 target = ( i == 0 ) ? position : parentPosition + layer.offset;

		if ( i == 0 || layer.followParent )
		{
			state.position = target;
		} else
		{
			// 追従しない子: 発生を始めた瞬間の位置に置いたまま
			if ( active && !state.wasActive )
			{
				state.captured = target;
			}
			state.position = state.wasActive || active ? state.captured : target;
		}

		// 窓に入った瞬間: スケジューラ(startDelay・バースト)を最初から
		const bool started = active && !state.wasActive;
		state.wasActive = active;
		state.emitting = active;

		if ( i == 0 )
		{
			if ( started )
			{
				scheduler_.Reset();
				emitAccumulator_ = 0.0f;
			}
			emitting_ = active;
		} else
		{
			ParticleEmitter& child = *children_[i - 1];
			if ( started )
			{
				child.scheduler_.Reset();
				child.emitAccumulator_ = 0.0f;
			}
			child.emitting_ = active;
			child.position = state.position;
			child.enabled = enabled; // 親を隠せば子も隠れる
		}
	}
}

size_t ParticleEmitter::GetTotalAliveCount() const
{
	size_t total = particles_.size();
	for ( size_t i = 0; i < activeChildCount_; ++i )
	{
		total += children_[i]->particles_.size();
	}
	return total;
}

Vector3 ParticleEmitter::GetLayerPosition(int layerIndex) const
{
	if ( layerIndex >= 0 && layerIndex < static_cast< int >( layerStates_.size() ) )
	{
		return layerStates_[layerIndex].position;
	}
	return position;
}

bool ParticleEmitter::IsLayerEmitting(int layerIndex) const
{
	return layerIndex >= 0 && layerIndex < static_cast< int >( layerStates_.size() ) && layerStates_[layerIndex].emitting;
}

//===================================================================
// 終点エミッタ
//===================================================================
void ParticleEmitter::SetEndEmitter(const ParticleEmitter* endEmitter)
{
	endEmitter_ = endEmitter;
}

void ParticleEmitter::ClearEndEmitter()
{
	endEmitter_ = nullptr;
}

bool ParticleEmitter::HasActiveEndPoint() const
{
	return endEmitter_ != nullptr
		&& endEmitter_ != this
		&& endEmitter_->config_.renderMode == config_.renderMode;
}

//===================================================================
// 更新
//===================================================================
float ParticleEmitter::RandomRange(float minValue, float maxValue)
{
	if ( maxValue <= minValue )
	{
		return minValue;
	}
	std::uniform_real_distribution<float> distribution(minValue, maxValue);
	return distribution(rng_);
}

void ParticleEmitter::Spawn()
{
	if ( static_cast< int >( particles_.size() ) >= config_.maxParticles )
	{
		return; // 満杯
	}

	const bool is2D = ( config_.renderMode == ParticleRenderMode::Screen2D );
	// 終点エミッタを使うときは道のりで動くので、ローカル空間にはしない
	const bool local = config_.localSpace && !HasActiveEndPoint();

	Particle p;
	p.age = 0.0f;
	p.lifetime = RandomRange(config_.lifetimeMin, config_.lifetimeMax);
	p.localSpace = local;

	// 位置: 発生範囲の形(Box / Sphere / ...)の中のランダムな点。形の空間はYが上なので、2DではYを反転して画面に置く
	const ParticleSim::ShapeSample shape = ParticleSim::SampleEmitShape(config_, is2D, rng_);
	const Vector3 base = local ? Vector3{ 0.0f, 0.0f, 0.0f } : position;
	if ( is2D )
	{
		p.position = { base.x + shape.offset.x, base.y - shape.offset.y, 0.0f };
	} else
	{
		p.position = { base.x + shape.offset.x, base.y + shape.offset.y, base.z + shape.offset.z };
	}

	// 発射方向: 設定の direction、または emitOutward のとき形の中心から外向き
	Vector3 direction = config_.direction;
	const float outwardLength = std::sqrt(shape.outward.x * shape.outward.x + shape.outward.y * shape.outward.y + shape.outward.z * shape.outward.z);
	if ( config_.emitOutward && outwardLength > 1e-6f )
	{
		direction = shape.outward;
	}

	// 速度: 発射方向を中心にした円錐(2Dは扇形)の中のランダムな向き
	const float speed = RandomRange(config_.speedMin, config_.speedMax);
	const float spread = config_.spreadAngle * kDegToRad;

	if ( is2D )
	{
		// 設定のYは「上が+」。画面のYは下向きなので、最後にYを反転する
		const float baseAngle = std::atan2(direction.y, direction.x);
		const float angle = baseAngle + RandomRange(-spread, spread);
		p.velocity = { std::cos(angle) * speed, -std::sin(angle) * speed, 0.0f };
		p.gravity = { config_.gravity.x, -config_.gravity.y, 0.0f };
	} else
	{
		const Vector3 forward = NormalizeOr(direction, { 0.0f, 1.0f, 0.0f });
		// forward に直交する2軸を作る
		const Vector3 helper = ( std::fabs(forward.y) < 0.99f ) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
		const Vector3 right = NormalizeOr(Cross(helper, forward), { 1.0f, 0.0f, 0.0f });
		const Vector3 up = Cross(forward, right);

		// 円錐の中で一様になるよう、cosθ を一様にサンプリングする
		const float cosTheta = RandomRange(std::cos(spread), 1.0f);
		const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = RandomRange(0.0f, 2.0f * kPi);
		const float cx = std::cos(phi) * sinTheta;
		const float cy = std::sin(phi) * sinTheta;

		p.velocity = {
			( forward.x * cosTheta + right.x * cx + up.x * cy ) * speed,
			( forward.y * cosTheta + right.y * cx + up.y * cy ) * speed,
			( forward.z * cosTheta + right.z * cx + up.z * cy ) * speed,
		};
		p.gravity = config_.gravity;
	}

	// エミッタの移動速度を引き継ぐ(ローカル空間では粒ごとエミッタと一緒に動くので不要)
	if ( !local && config_.inheritVelocity > 0.0f )
	{
		p.velocity.x += emitterVelocity_.x * config_.inheritVelocity;
		p.velocity.y += emitterVelocity_.y * config_.inheritVelocity;
		if ( !is2D )
		{
			p.velocity.z += emitterVelocity_.z * config_.inheritVelocity;
		}
	}

	p.drag = config_.drag;
	p.rotation = RandomRange(config_.rotationMin, config_.rotationMax) * kDegToRad;
	p.rotationSpeed = RandomRange(config_.rotationSpeedMin, config_.rotationSpeedMax) * kDegToRad;

	// サイズ: 粒ごとに ±sizeRandomness の倍率を掛ける(開始と終了に同じ倍率なので、縮む割合は変わらない)
	const float sizeScale = ( config_.sizeRandomness > 0.0f ) ? 1.0f + RandomRange(-config_.sizeRandomness, config_.sizeRandomness) : 1.0f;
	p.sizeStart = config_.sizeStart * sizeScale;
	p.sizeEnd = config_.sizeEnd * sizeScale;
	p.sizeCurve = config_.sizeCurve;

	// 色: randomColor のときは colorStart と colorStartB の間のランダムな色
	p.colorStart = config_.colorStart;
	if ( config_.randomColor )
	{
		const float mix = RandomRange(0.0f, 1.0f);
		p.colorStart = {
			Lerp(config_.colorStart.x, config_.colorStartB.x, mix),
			Lerp(config_.colorStart.y, config_.colorStartB.y, mix),
			Lerp(config_.colorStart.z, config_.colorStartB.z, mix),
			Lerp(config_.colorStart.w, config_.colorStartB.w, mix),
		};
	}
	p.colorEnd = config_.colorEnd;
	p.colorCurve = config_.colorCurve;
	p.useColorMid = config_.useColorMid;
	p.colorMid = config_.colorMid;
	p.colorMidTime = config_.colorMidTime;
	p.fadeIn = config_.fadeIn;

	// 力・衝突・向き
	p.radialAccel = config_.radialAcceleration;
	p.orbitSpeed = config_.orbitSpeed * kDegToRad;
	p.turbulence = config_.turbulence;
	p.turbulenceFreq = config_.turbulenceFrequency;
	p.turbulenceSpeed = config_.turbulenceSpeed;
	p.seed = RandomRange(0.0f, 100.0f);
	p.groundCollision = config_.groundCollision;
	p.groundHeight = config_.groundHeight;
	p.bounciness = config_.bounciness;
	p.bounceLifeLoss = config_.bounceLifeLoss;
	p.alignToVelocity = config_.alignToVelocity;
	p.stretchScale = config_.stretchScale;
	p.stretchMax = config_.stretchMax;

	// 終点エミッタがあれば、速度ではなく「始点 → 終点」の道のりで動く粒にする
	if ( HasActiveEndPoint() )
	{
		p.followPath = true;
		p.pathStart = { position.x, position.y, is2D ? 0.0f : position.z };
		p.pathEnd = { endEmitter_->position.x, endEmitter_->position.y, is2D ? 0.0f : endEmitter_->position.z };
		p.offsetUnit = { RandomRange(-1.0f, 1.0f), RandomRange(-1.0f, 1.0f), is2D ? 0.0f : RandomRange(-1.0f, 1.0f) };
		p.startExtent = config_.spawnExtent;
		p.endExtent = endEmitter_->config_.spawnExtent; // 終点側の広がり = 終点エミッタの spawnExtent
		p.pathCurve = endTravelCurve;
		p.velocity = { 0.0f, 0.0f, 0.0f };
		p.gravity = { 0.0f, 0.0f, 0.0f };
		p.drag = 0.0f;
		// t = 0 の位置(上で決めた位置と同じ式)
		p.position = Add(p.pathStart, Multiply(p.offsetUnit, p.startExtent));
	}

	particles_.push_back(p);
}

void ParticleEmitter::Emit(int count)
{
	for ( int i = 0; i < count; ++i )
	{
		Spawn();
	}
}

void ParticleEmitter::Clear()
{
	particles_.clear();
	instanceCount_ = 0;
	for ( size_t i = 0; i < children_.size(); ++i )
	{
		children_[i]->particles_.clear();
		children_[i]->instanceCount_ = 0;
	}
	RestartTimeline();
}

void ParticleEmitter::Update(float deltaTime)
{
	// --- ホットリロード: ライブラリの設定が更新されていたら取り込む(次に発生する粒から反映) ---
	if ( entry_ != nullptr && entry_->revision != syncedRevision_ )
	{
		SetEffect(entry_->effect); // 親・子レイヤーとも取り込む(次に発生する粒から反映)
		syncedRevision_ = entry_->revision;
	}

	const bool is2D = ( config_.renderMode == ParticleRenderMode::Screen2D );

	// --- エミッタの移動速度(inheritVelocity用) ---
	if ( hasLastPosition_ && deltaTime > 1e-5f )
	{
		emitterVelocity_ = {
			( position.x - lastPosition_.x ) / deltaTime,
			( position.y - lastPosition_.y ) / deltaTime,
			( position.z - lastPosition_.z ) / deltaTime,
		};
	} else
	{
		emitterVelocity_ = { 0.0f, 0.0f, 0.0f };
	}
	lastPosition_ = position;
	hasLastPosition_ = true;

	// --- エフェクトの時刻・各レイヤーの位置と発生のオン/オフ(子レイヤー自身は親が管理する) ---
	if ( !isLayerChild_ )
	{
		UpdateTimeline(deltaTime);
	}

	// --- 発生: バースト + 自動発生(どちらも startDelay が過ぎてから。タイムラインの窓の中だけ) ---
	if ( enabled && emitting_ )
	{
		Emit(scheduler_.Advance(deltaTime, config_));

		if ( config_.looping && scheduler_.HasStarted(config_) )
		{
			emitAccumulator_ += config_.emitRate * deltaTime;
			while ( emitAccumulator_ >= 1.0f )
			{
				Spawn();
				emitAccumulator_ -= 1.0f;
			}
		} else
		{
			emitAccumulator_ = 0.0f;
		}
	} else
	{
		emitAccumulator_ = 0.0f;
	}

	// --- 移動・寿命 ---
	for ( size_t i = 0; i < particles_.size(); )
	{
		Particle& p = particles_[i];
		p.age += deltaTime;
		if ( p.age >= p.lifetime )
		{
			// 順序は描画に影響しない(加算ブレンド前提)ので、末尾と入れ替えて消す
			p = particles_.back();
			particles_.pop_back();
			continue;
		}

		if ( p.followPath )
		{
			// 道のり travel(0〜1)。位置 = 始点と終点の補間 + 乱数のずれ × 広がり(始点の広がり → 終点の広がり)
			const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
			const float travel = ApplyCurve(p.pathCurve, 0.0f, 1.0f, t);
			const Vector3 center = Lerp(p.pathStart, p.pathEnd, travel);
			const Vector3 extent = Lerp(p.startExtent, p.endExtent, travel);
			const Vector3 previous = p.position;
			p.position = Add(center, Multiply(p.offsetUnit, extent));

			// 進行方向への伸ばし(alignToVelocity)のために、位置の変化から速度を出しておく
			if ( deltaTime > 1e-5f )
			{
				p.velocity = ( p.position - previous ) / deltaTime;
			}

			p.rotation += p.rotationSpeed * deltaTime;
			++i;
			continue;
		}

		// 力の中心: ワールド空間ならエミッタの位置、ローカル空間なら原点
		Vector3 origin = { 0.0f, 0.0f, 0.0f };
		if ( !p.localSpace )
		{
			origin = { position.x, position.y, is2D ? 0.0f : position.z };
		}

		// 加速度 = 重力 + 放射 + 乱流
		Vector3 accel = p.gravity;
		if ( p.radialAccel != 0.0f )
		{
			const Vector3 offset = p.position - origin;
			const float length = Length(offset);
			if ( length > 1e-5f )
			{
				accel += offset * ( p.radialAccel / length );
			}
		}
		if ( p.turbulence > 0.0f )
		{
			Vector3 noise = ParticleSim::TurbulenceAccel(p.position, p.age, p.seed, p.turbulence, p.turbulenceFreq, p.turbulenceSpeed);
			if ( is2D )
			{
				noise.z = 0.0f;
			}
			accel += noise;
		}
		p.velocity += accel * deltaTime;

		const float damping = (std::max)(0.0f, 1.0f - p.drag * deltaTime);
		p.velocity *= damping;

		p.position += p.velocity * deltaTime;

		// 中心まわりの回転(3DはY軸、2Dは画面)
		if ( p.orbitSpeed != 0.0f )
		{
			const float angle = p.orbitSpeed * deltaTime;
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			const Vector3 offset = p.position - origin;
			if ( is2D )
			{
				p.position = { origin.x + offset.x * c - offset.y * s, origin.y + offset.x * s + offset.y * c, 0.0f };
			} else
			{
				p.position = { origin.x + offset.x * c + offset.z * s, p.position.y, origin.z - offset.x * s + offset.z * c };
			}
		}

		// 地面との衝突(水平な無限平面)
		if ( p.groundCollision )
		{
			const float emitterY = p.localSpace ? position.y : 0.0f; // ローカル空間の粒のワールドYは、相対Y + エミッタのY
			const float worldY = p.position.y + emitterY;
			bool bounced = false;
			if ( is2D )
			{
				// 画面のYは下向き: 床より下(値が大きい)に入ったら衝突
				if ( worldY > p.groundHeight )
				{
					p.position.y = p.groundHeight - emitterY;
					if ( p.velocity.y > 0.0f )
					{
						p.velocity.y = -p.velocity.y * p.bounciness;
						bounced = true;
					}
				}
			} else if ( worldY < p.groundHeight )
			{
				p.position.y = p.groundHeight - emitterY;
				if ( p.velocity.y < 0.0f )
				{
					p.velocity.y = -p.velocity.y * p.bounciness;
					bounced = true;
				}
			}
			if ( bounced )
			{
				// 跳ねるたびに残り寿命を削る(いつまでも跳ね続けないように)
				p.age += ( p.lifetime - p.age ) * p.bounceLifeLoss;
			}
		}

		p.rotation += p.rotationSpeed * deltaTime;
		++i;
	}

	// --- 子レイヤー(位置・発生のオン/オフは UpdateTimeline で設定済み) ---
	for ( size_t childIndex = 0; childIndex < activeChildCount_; ++childIndex )
	{
		children_[childIndex]->Update(deltaTime);
	}
}

//===================================================================
// GPUバッファへの書き込み
//===================================================================
void ParticleEmitter::BuildInstances(const Matrix4x4& view, const Matrix4x4& projection, float clientWidth, float clientHeight)
{
	const bool is2D = ( config_.renderMode == ParticleRenderMode::Screen2D );

	// 3D: ビュー射影 / 2D: ピクセル座標の正射影(Spriteと同じ)
	const Matrix4x4 viewProjection = is2D
		? MakeOrthoGraphicMatrix(0.0f, 0.0f, clientWidth, clientHeight, 0.0f, 100.0f)
		: Multiply(view, projection);

	// ビルボード行列 B = ビュー行列の左上3x3の転置(カメラのワールド回転)。
	// 行ベクトル規約なので、B の第0行 = カメラの右、第1行 = 上、第2行 = 前
	float billboard[3][3];
	for ( int row = 0; row < 3; ++row )
	{
		for ( int col = 0; col < 3; ++col )
		{
			billboard[row][col] = view.m[col][row];
		}
	}

	const size_t count = (std::min)(particles_.size(), static_cast< size_t >( ParticleConfig::kMaxParticlesLimit ));
	for ( size_t i = 0; i < count; ++i )
	{
		const Particle& p = particles_[i];
		const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);

		// ローカル空間の粒は、描くときにエミッタの現在位置を足す
		Vector3 worldPosition = p.position;
		if ( p.localSpace )
		{
			worldPosition.x += position.x;
			worldPosition.y += position.y;
			worldPosition.z += is2D ? 0.0f : position.z;
		}

		// 板ポリは -1〜1 なので、見た目の幅 = size にするには半分を掛ける
		const float half = ApplyCurve(p.sizeCurve, p.sizeStart, p.sizeEnd, t) * 0.5f;

		// 回転と、進行方向への伸ばし(Stretched Billboard)
		float angle = p.rotation;
		float stretch = 1.0f;
		if ( p.alignToVelocity )
		{
			const float speed = Length(p.velocity);
			stretch = std::clamp(1.0f + p.stretchScale * speed, 1.0f, p.stretchMax);
			if ( is2D )
			{
				// 板ポリの「上」は画面の (s, -c)。これを速度の向きに合わせる → angle = atan2(vx, -vy)
				if ( std::fabs(p.velocity.x) + std::fabs(p.velocity.y) > 1e-4f )
				{
					angle += std::atan2(p.velocity.x, -p.velocity.y);
				}
			} else
			{
				// 速度をカメラの右・上へ射影し、画面上の向きを求める。板ポリの「上」は (-s)*右 + c*上 → angle = atan2(-dx, dy)
				const float dx = p.velocity.x * billboard[0][0] + p.velocity.y * billboard[0][1] + p.velocity.z * billboard[0][2];
				const float dy = p.velocity.x * billboard[1][0] + p.velocity.y * billboard[1][1] + p.velocity.z * billboard[1][2];
				if ( dx * dx + dy * dy > 1e-8f )
				{
					angle += std::atan2(-dx, dy);
				}
			}
		}
		const float c = std::cos(angle);
		const float s = std::sin(angle);

		Matrix4x4 world = MakeIdentityMatrix();
		if ( is2D )
		{
			// 拡大(Yは画面が下向きなので反転。伸ばしは板ポリの縦方向) → Z回転 → 平行移動
			world.m[0][0] = half * c;             world.m[0][1] = half * s;
			world.m[1][0] = half * s * stretch;   world.m[1][1] = -half * c * stretch;
			world.m[3][0] = worldPosition.x;
			world.m[3][1] = worldPosition.y;
			world.m[3][2] = 0.0f;
		} else
		{
			// 拡大 → Z回転(画面内の回転) → ビルボード → 平行移動
			const float rz[3][3] = {
				{  c,    s,    0.0f },
				{ -s,    c,    0.0f },
				{  0.0f, 0.0f, 1.0f },
			};
			const float rowScale[3] = { half, half * stretch, half }; // 板ポリの縦(第1行)だけ伸ばす
			for ( int row = 0; row < 3; ++row )
			{
				for ( int col = 0; col < 3; ++col )
				{
					float sum = 0.0f;
					for ( int k = 0; k < 3; ++k )
					{
						sum += rz[row][k] * billboard[k][col];
					}
					world.m[row][col] = sum * rowScale[row];
				}
			}
			world.m[3][0] = worldPosition.x;
			world.m[3][1] = worldPosition.y;
			world.m[3][2] = worldPosition.z;
		}

		ParticleInstanceData& out = instanceData_[i];
		out.WVP = Multiply(world, viewProjection);
		out.color = ParticleSim::EvaluateColor(p.colorStart, p.colorMid, p.useColorMid, p.colorMidTime, p.colorEnd,
			p.colorCurve, p.fadeIn, t);
	}
	instanceCount_ = static_cast< uint32_t >( count );

	for ( size_t childIndex = 0; childIndex < activeChildCount_; ++childIndex )
	{
		children_[childIndex]->BuildInstances(view, projection, clientWidth, clientHeight);
	}
}

//===================================================================
// 描画(1回のインスタンシング描画)
//===================================================================
void ParticleEmitter::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( !enabled )
	{
		return;
	}

	DrawSelf(commandList, pipelines, textureSrv, lightAddress);

	// 子レイヤーは親のあとに描く(上に重なる)
	for ( size_t childIndex = 0; childIndex < activeChildCount_; ++childIndex )
	{
		children_[childIndex]->DrawSelf(commandList, pipelines, textureSrv, lightAddress);
	}
}

void ParticleEmitter::DrawSelf(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( instanceCount_ == 0 )
	{
		return;
	}

	const bool is2D = ( config_.renderMode == ParticleRenderMode::Screen2D );
	commandList->SetPipelineState(is2D
		? pipelines.GetParticle2dPso(config_.blendMode)
		: pipelines.GetParticle3dPso(config_.blendMode));

	// 全ルートパラメータを設定済みにしておく(未設定のまま描くとデバッグレイヤーが止まるため)
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, instanceBuffer_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, textureSrv);
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);
	commandList->SetGraphicsRootDescriptorTable(4, srvGpu_);
	commandList->IASetVertexBuffers(0, 1, &vbv_);
	commandList->DrawInstanced(6, instanceCount_, 0, 0);
}

//===================================================================
// UI(ゲーム側のデバッグ表示。値の編集はParticleEditorで行う)
//===================================================================
void ParticleEmitter::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::PushID(this);

	const char* modeName = ( config_.renderMode == ParticleRenderMode::Screen2D ) ? "2D" : "3D";
	ImGui::Text("%s [%s]  alive: %d / %d", name_.c_str(), modeName,
		static_cast< int >( particles_.size() ), config_.maxParticles);
	if ( effect_.layers.size() > 1 )
	{
		ImGui::Text("Layers: %d   total alive: %d   time: %.2f s", static_cast< int >( effect_.layers.size() ),
			static_cast< int >( GetTotalAliveCount() ), effectTime_);
	}

	ImGui::Checkbox("Enabled", &enabled);
	ImGui::DragFloat3("Position", &position.x, config_.renderMode == ParticleRenderMode::Screen2D ? 1.0f : 0.1f);

	if ( endEmitter_ != nullptr )
	{
		ImGui::Text("End Point: %s%s", endEmitter_->name_.c_str(), HasActiveEndPoint() ? "" : "  (ignored: render mode differs)");
		int curve = static_cast< int >( endTravelCurve );
		if ( ImGui::Combo("Travel Curve", &curve, kCurveItems) )
		{
			endTravelCurve = static_cast< ParticleCurve >( curve );
		}
	}

	if ( ImGui::Button("Play") )
	{
		Play(); // Burst Once・タイムラインを最初から
	}
	ImGui::SameLine();
	if ( ImGui::Button("Burst (32)") )
	{
		Emit(32);
	}
	ImGui::SameLine();
	if ( ImGui::Button("Clear") )
	{
		Clear();
	}
	if ( library_ != nullptr )
	{
		ImGui::SameLine();
		if ( ImGui::Button("Edit in Particle Editor") )
		{
			library_->RequestEdit(libraryIndex_);
		}
	}

	ImGui::PopID();
#endif
}

void ParticleEmitter::Shutdown()
{
	for ( auto& child : children_ )
	{
		child->Shutdown();
	}
	children_.clear();
	activeChildCount_ = 0;
	dx_ = nullptr;

	particles_.clear();
	instanceCount_ = 0;
	instanceData_ = nullptr;
	entry_ = nullptr;
	library_ = nullptr;
	vertexResource_.Reset();
	instanceBuffer_.Reset();
	materialResource_.Reset();
}
