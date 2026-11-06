#include "ParticleEditor.h"

#include "AssetCache.h"
#include "BlendModes.h"
#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void ParticleEditor::Initialize(DirectXCommon& dx)
{
	camera_.Initialize();
	light_.Initialize(dx);

	emitter_.Initialize(dx.GetDevice());
	emitter_.config.position = { 0.0f, 0.0f, 35.0f, 1.0f };
}

void ParticleEditor::Update(float deltaTime, InputManager& input, float /*clientWidth*/, float /*clientHeight*/)
{
	camera_.Update(input);

	if ( !paused_ )
	{
		emitter_.Update(deltaTime);
	}
}

void ParticleEditor::Draw(const RenderContext& context)
{
	ID3D12GraphicsCommandList* commandList = context.commandList;
	const PipelineManager& pipelines = *context.pipelines;
	const AssetCache& assets = *context.assets;

	commandList->SetGraphicsRootSignature(pipelines.GetRootSignature());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->SetGraphicsRootConstantBufferView(3, light_.GetGpuAddress());

	emitter_.Draw(
		commandList,
		camera_.GetViewMatrix(),
		camera_.GetProjectionMatrix(),
		assets.GetParticleTexture().srvGpu,
		pipelines.GetEmitterPsoTable());
}

void ParticleEditor::Shutdown()
{
	emitter_.Shutdown();
}

void ParticleEditor::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::Begin("Particle Editor");

	camera_.DrawUi();
	ImGui::Separator();

	ParticleEmitterConfig& config = emitter_.config;

	ImGui::Text("Particle Emitter");
	ImGui::Checkbox("Paused", &paused_);

	int blendMode = static_cast< int >( config.blendMode );
	if ( ImGui::Combo("Blend Mode", &blendMode, kBlendModeItems) )
	{
		config.blendMode = static_cast< ParticleBlendMode >( blendMode );
	}

	ImGui::DragFloat3("Emitter Position", &config.position.x, 0.1f);
	ImGui::ColorEdit4("Color Start", &config.colorStart.x);
	ImGui::ColorEdit4("Color End", &config.colorEnd.x);
	ImGui::DragFloat("Emit Rate", &config.emitRate, 0.5f, 0.1f, 256.0f);
	ImGui::DragFloat("Lifetime Min", &config.lifetimeMin, 0.1f, 0.1f, 60.0f);
	ImGui::DragFloat("Lifetime Max", &config.lifetimeMax, 0.1f, 0.1f, 60.0f);
	ImGui::DragFloat("Speed Min", &config.speedMin, 0.1f, 0.0f, 20.0f);
	ImGui::DragFloat("Speed Max", &config.speedMax, 0.1f, 0.0f, 20.0f);
	ImGui::DragFloat("Size Start", &config.sizeStart, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Size End", &config.sizeEnd, 0.01f, 0.0f, 10.0f);
	ImGui::Checkbox("Looping", &config.looping);
	if ( ImGui::Button("Burst (64)") )
	{
		emitter_.Emit(64);
	}

	ImGui::End();
#endif
}