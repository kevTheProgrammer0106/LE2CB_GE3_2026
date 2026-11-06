#include "GameScene.h"

#include "AssetCache.h"
#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void GameScene::Initialize(DirectXCommon& dx, const AssetCache& assets)
{
	camera_.Initialize();
	light_.Initialize(dx);

	object1_.Initialize(dx, ModelNode::Sphere);

	object2_.Initialize(dx, ModelNode::Plane);
	object2_.transform.translate = { 3.0f, 0.0f, 0.0f, 0.0f };

	sprite_.Initialize(dx, assets.GetSpriteTexture().srvGpu);
	instancing_.Initialize(dx);
}

void GameScene::Update(float /*deltaTime*/, InputManager& input, float clientWidth, float clientHeight)
{
	camera_.Update(input);

	const Matrix4x4& viewProjection = camera_.GetViewProjectionMatrix();
	object1_.Update(viewProjection);
	object2_.Update(viewProjection);
	sprite_.Update(clientWidth, clientHeight);
	instancing_.Update(viewProjection);
}

void GameScene::Draw(const RenderContext& context)
{
	ID3D12GraphicsCommandList* commandList = context.commandList;
	const PipelineManager& pipelines = *context.pipelines;
	const AssetCache& assets = *context.assets;
	const D3D12_GPU_VIRTUAL_ADDRESS lightAddress = light_.GetGpuAddress();

	commandList->SetGraphicsRootSignature(pipelines.GetRootSignature());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// --- モデル ---
	commandList->SetPipelineState(pipelines.GetModelPso(modelBlendMode_));
	object1_.Draw(commandList, assets, lightAddress);
	object2_.Draw(commandList, assets, lightAddress);

	// --- スプライト ---
	sprite_.Draw(commandList, pipelines, lightAddress);

	// --- インスタンシング描画 ---
	instancing_.Draw(commandList, pipelines, assets.GetParticleTexture().srvGpu, lightAddress);
}

void GameScene::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::Begin("Game Debug");

	if ( ImGui::Button("Open Particle Editor (F2)") )
	{
		openEditorRequested_ = true;
	}
	ImGui::Separator();

	camera_.DrawUi();
	ImGui::Separator();

	ImGui::Text("Model");
	int blendIndex = static_cast< int >( modelBlendMode_ );
	if ( ImGui::Combo("Blend Mode", &blendIndex, kBlendModeItems) )
	{
		modelBlendMode_ = static_cast< ParticleBlendMode >( blendIndex );
	}

	object1_.DrawUi("Object 1");
	ImGui::Separator();
	object2_.DrawUi("Object 2");
	ImGui::Separator();

	light_.DrawUi();
	ImGui::Separator();

	sprite_.DrawUi();
	ImGui::Separator();

	instancing_.DrawUi();

	ImGui::End();
#endif
}

bool GameScene::ConsumeOpenEditorRequest()
{
	const bool requested = openEditorRequested_;
	openEditorRequested_ = false;
	return requested;
}