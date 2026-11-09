#define NOMINMAX
#include "Particle.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>

#include "DirectXCommon.h"
#include "Easing.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace
{
	constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

	float ApplyCurve(ParticleCurve curve, float start, float end, float t)
	{
		return curve == ParticleCurve::EaseInOut ? EaseInOut(start, end, t) : Lerp(start, end, t);
	}

	Vector3 Cross(const Vector3& a, const Vector3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	// 正規化(長さがほぼ0なら fallback を返す)
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
	library_ = &library;
	name_ = name;
	entry_ = library.Register(name, defaultConfig);
	libraryIndex_ = library.IndexOf(name);

	// ライブラリ(ファイルがあればその内容)の設定で始める
	config_ = entry_->config;
	syncedRevision_ = entry_->revision;

	CreateGpuResources(dx);
}

void ParticleEmitter::Initialize(DirectXCommon& dx)
{
	library_ = nullptr;
	entry_ = nullptr;
	libraryIndex_ = -1;
	name_ = "Preview";

	config_ = ParticleConfig::MakeDefault();
	config_.Sanitize();

	CreateGpuResources(dx);
}

void ParticleEmitter::CreateGpuResources(DirectXCommon& dx)
{
	std::random_device device;
	rng_.seed(device());

	particles_.reserve(ParticleConfig::kMaxParticlesLimit);

	// 板ポリ(三角形2枚)。-1〜1の正方形で、サイズは行列側の拡大で決める。
	// UVは「左上が(0,0)」になる向き(InstancingObjectの板ポリは左右反転しているので別に持つ)
	const VertexData vertices[] = {
		{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 左上
		{ { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
		{ { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 右下
	};

	vertexResource_ = dx.CreateBufferResource(sizeof(vertices));
	VertexData* mappedVertices = nullptr;
	HRESULT hr = vertexResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mappedVertices ));
	assert(SUCCEEDED(hr) && mappedVertices != nullptr);
	( void ) hr;
	std::memcpy(mappedVertices, vertices, sizeof(vertices));

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.SizeInBytes = static_cast< UINT >( sizeof(vertices) );
	vbv_.StrideInBytes = sizeof(VertexData);

	// 未使用のルートパラメータ[0]用マテリアル(白)
	materialResource_ = dx.CreateBufferResource(sizeof(Material));
	Material* material = nullptr;
	materialResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &material ));
	*material = {};
	material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->lightingMode = LightingMode::None;
	material->uvTransform = MakeIdentityMatrix();

	// インスタンスバッファ。上限ぶんを最初に1回だけ確保する(設定のmaxParticlesはCPU側の発生上限)
	constexpr uint32_t kCapacity = ParticleConfig::kMaxParticlesLimit;
	instanceBuffer_ = dx.CreateBufferResource(sizeof(ParticleInstanceData) * kCapacity);
	instanceBuffer_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &instanceData_ ));

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
	ApplyConfig(config);
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

	Particle p;
	p.age = 0.0f;
	p.lifetime = RandomRange(config_.lifetimeMin, config_.lifetimeMax);

	// 位置: エミッタを中心にした箱の中のランダムな点
	p.position = {
		position.x + RandomRange(-config_.spawnExtent.x, config_.spawnExtent.x),
		position.y + RandomRange(-config_.spawnExtent.y, config_.spawnExtent.y),
		is2D ? 0.0f : position.z + RandomRange(-config_.spawnExtent.z, config_.spawnExtent.z),
	};

	// 速度: 発射方向を中心にした円錐(2Dは扇形)の中のランダムな向き
	const float speed = RandomRange(config_.speedMin, config_.speedMax);
	const float spread = config_.spreadAngle * kDegToRad;

	if ( is2D )
	{
		// 設定のYは「上が+」。画面のYは下向きなので、最後にYを反転する
		const float baseAngle = std::atan2(config_.direction.y, config_.direction.x);
		const float angle = baseAngle + RandomRange(-spread, spread);
		p.velocity = { std::cos(angle) * speed, -std::sin(angle) * speed, 0.0f };
		p.gravity = { config_.gravity.x, -config_.gravity.y, 0.0f };
	} else
	{
		const Vector3 forward = NormalizeOr(config_.direction, { 0.0f, 1.0f, 0.0f });
		// forward に直交する2軸を作る
		const Vector3 helper = ( std::fabs(forward.y) < 0.99f ) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
		const Vector3 right = NormalizeOr(Cross(helper, forward), { 1.0f, 0.0f, 0.0f });
		const Vector3 up = Cross(forward, right);

		// 円錐の中で一様になるよう、cosθ を一様にサンプリングする
		const float cosTheta = RandomRange(std::cos(spread), 1.0f);
		const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = RandomRange(0.0f, 2.0f * std::numbers::pi_v<float>);
		const float cx = std::cos(phi) * sinTheta;
		const float cy = std::sin(phi) * sinTheta;

		p.velocity = {
			( forward.x * cosTheta + right.x * cx + up.x * cy ) * speed,
			( forward.y * cosTheta + right.y * cx + up.y * cy ) * speed,
			( forward.z * cosTheta + right.z * cx + up.z * cy ) * speed,
		};
		p.gravity = config_.gravity;
	}

	p.drag = config_.drag;
	p.rotation = RandomRange(config_.rotationMin, config_.rotationMax) * kDegToRad;
	p.rotationSpeed = RandomRange(config_.rotationSpeedMin, config_.rotationSpeedMax) * kDegToRad;

	p.sizeStart = config_.sizeStart;
	p.sizeEnd = config_.sizeEnd;
	p.sizeCurve = config_.sizeCurve;
	p.colorStart = config_.colorStart;
	p.colorEnd = config_.colorEnd;
	p.colorCurve = config_.colorCurve;

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
	emitAccumulator_ = 0.0f;
}

void ParticleEmitter::Update(float deltaTime)
{
	// --- ホットリロード: ライブラリの設定が更新されていたら取り込む(次に発生する粒から反映) ---
	if ( entry_ != nullptr && entry_->revision != syncedRevision_ )
	{
		ApplyConfig(entry_->config);
		syncedRevision_ = entry_->revision;
	}

	// --- 発生 ---
	if ( enabled && config_.looping )
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

		p.velocity.x += p.gravity.x * deltaTime;
		p.velocity.y += p.gravity.y * deltaTime;
		p.velocity.z += p.gravity.z * deltaTime;

		const float damping = std::max(0.0f, 1.0f - p.drag * deltaTime);
		p.velocity *= damping;

		p.position.x += p.velocity.x * deltaTime;
		p.position.y += p.velocity.y * deltaTime;
		p.position.z += p.velocity.z * deltaTime;

		p.rotation += p.rotationSpeed * deltaTime;
		++i;
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

	const size_t count = std::min<size_t>(particles_.size(), ParticleConfig::kMaxParticlesLimit);
	for ( size_t i = 0; i < count; ++i )
	{
		const Particle& p = particles_[i];
		const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);

		// 板ポリは -1〜1 なので、見た目の幅 = size にするには半分を掛ける
		const float half = ApplyCurve(p.sizeCurve, p.sizeStart, p.sizeEnd, t) * 0.5f;
		const float c = std::cos(p.rotation);
		const float s = std::sin(p.rotation);

		Matrix4x4 world = MakeIdentityMatrix();
		if ( is2D )
		{
			// 拡大(Yは画面が下向きなので反転) → Z回転 → 平行移動
			world.m[0][0] = half * c;   world.m[0][1] = half * s;
			world.m[1][0] = half * s;   world.m[1][1] = -half * c;
			world.m[3][0] = p.position.x;
			world.m[3][1] = p.position.y;
			world.m[3][2] = 0.0f;
		} else
		{
			// 拡大 → Z回転(画面内の回転) → ビルボード → 平行移動
			const float rz[3][3] = {
				{ c, s, 0.0f },
				{ -s, c, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
			};
			for ( int row = 0; row < 3; ++row )
			{
				for ( int col = 0; col < 3; ++col )
				{
					float sum = 0.0f;
					for ( int k = 0; k < 3; ++k )
					{
						sum += rz[row][k] * billboard[k][col];
					}
					world.m[row][col] = sum * half;
				}
			}
			world.m[3][0] = p.position.x;
			world.m[3][1] = p.position.y;
			world.m[3][2] = p.position.z;
		}

		ParticleInstanceData& out = instanceData_[i];
		out.WVP = Multiply(world, viewProjection);
		out.color = {
			ApplyCurve(p.colorCurve, p.colorStart.x, p.colorEnd.x, t),
			ApplyCurve(p.colorCurve, p.colorStart.y, p.colorEnd.y, t),
			ApplyCurve(p.colorCurve, p.colorStart.z, p.colorEnd.z, t),
			ApplyCurve(p.colorCurve, p.colorStart.w, p.colorEnd.w, t),
		};
	}
	instanceCount_ = static_cast< uint32_t >( count );
}

//===================================================================
// 描画(1回のインスタンシング描画)
//===================================================================
void ParticleEmitter::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( !enabled || instanceCount_ == 0 )
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

	ImGui::Checkbox("Enabled", &enabled);
	ImGui::DragFloat3("Position", &position.x, config_.renderMode == ParticleRenderMode::Screen2D ? 1.0f : 0.1f);

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
	particles_.clear();
	instanceCount_ = 0;
	instanceData_ = nullptr;
	entry_ = nullptr;
	library_ = nullptr;
	vertexResource_.Reset();
	instanceBuffer_.Reset();
	materialResource_.Reset();
}