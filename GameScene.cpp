#include "GameScene.h"

#include "AssetCache.h"
#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void GameScene::Initialize(DirectXCommon& dx, const AssetCache& assets, ParticleLibrary& library)
{
	camera_.Initialize();
	light_.Initialize(dx);

	object1_.Initialize(dx, ModelNode::Sphere);

	object2_.Initialize(dx, ModelNode::Plane);
	object2_.transform.translate = { 3.0f, 0.0f, 0.0f, 0.0f };

	sprite_.Initialize(dx, assets.GetSpriteTexture().srvGpu);
	instancing_.Initialize(dx);

	// パーティクル: 名前 + 初期設定。resources/particles/名前.particle があればそちらが優先される
	fire_.Initialize(dx, library, "Fire", ParticleConfig::MakeFire());
	fire_.position = { 0.0f, -2.0f, 10.0f };

	sparkle_.Initialize(dx, library, "Sparkle2D", ParticleConfig::MakeSparkle2D());
	sparkle_.position = { 1000.0f, 600.0f, 0.0f }; // 2Dはピクセル座標(左上が原点)
}

void GameScene::Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight)
{
	camera_.Update(input);

	const Matrix4x4& viewProjection = camera_.GetViewProjectionMatrix();
	object1_.Update(viewProjection);
	object2_.Update(viewProjection);
	sprite_.Update(clientWidth, clientHeight);
	instancing_.Update(viewProjection);

	for ( ParticleEmitter* emitter : emitters_ )
	{
		emitter->Update(deltaTime); // ライブラリの更新(ホットリロード)もここで取り込む
		emitter->BuildInstances(camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), clientWidth, clientHeight);
	}
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

	// --- パーティクル: 3Dビルボード → 2Dスクリーン(2Dは最後に重ねる) ---
	DrawEmitters(context, ParticleRenderMode::Billboard3D, lightAddress);
	DrawEmitters(context, ParticleRenderMode::Screen2D, lightAddress);
}

void GameScene::DrawEmitters(const RenderContext& context, ParticleRenderMode mode, D3D12_GPU_VIRTUAL_ADDRESS lightAddress)
{
	for ( ParticleEmitter* emitter : emitters_ )
	{
		if ( emitter->GetRenderMode() == mode )
		{
			emitter->Draw(context.commandList, *context.pipelines, context.assets->GetParticleTexture().srvGpu, lightAddress);
		}
	}
}

void GameScene::Shutdown()
{
	for ( ParticleEmitter* emitter : emitters_ )
	{
		emitter->Shutdown();
	}
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
	ImGui::Separator();

	ImGui::Text("Particles");
	for ( ParticleEmitter* emitter : emitters_ )
	{
		ImGui::Separator();
		emitter->DrawUi();
	}

	ImGui::End();
#endif
}

bool GameScene::ConsumeOpenEditorRequest()
{
	const bool requested = openEditorRequested_;
	openEditorRequested_ = false;
	return requested;
}