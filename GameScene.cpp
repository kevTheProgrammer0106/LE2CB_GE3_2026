#define NOMINMAX
#include "GameScene.h"

#include "AssetCache.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "PipelineManager.h"

#include <algorithm>
#include <iterator>
#include <string>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void GameScene::Initialize(DirectXCommon& dx, const AssetCache& assets, ParticleLibrary& library)
{
	dx_ = &dx;
	assets_ = &assets;
	debug_.Initialize(dx);

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

	// 図形: コードから追加する例。Hierarchyの「Add」でも同じように追加できる(不要なら消してよい)
	const ShapeType demoTypes[] = { ShapeType::Box, ShapeType::Line, ShapeType::Triangle, ShapeType::Sphere, ShapeType::Cone };
	float x = -3.2f;
	for ( ShapeType type : demoTypes )
	{
		ShapeObject* shape = AddShape(type);
		shape->transform.scale = { 0.8f, 0.8f, 0.8f, 0.0f };
		shape->transform.translate = { x, -1.6f, 0.0f, 0.0f };
		x += 1.6f;
	}
}

ShapeObject* GameScene::AddShape(ShapeType type, const std::string& name, bool flat)
{
	const int typeIndex = static_cast< int >( type );
	flat = flat && ShapeFactory::HasFlatVersion(type); // 2D版が無い図形では無視する

	std::string objectName = name;
	if ( objectName.empty() )
	{
		objectName = std::string(ShapeFactory::GetDisplayName(type, flat)) + " " + std::to_string(++shapeCounters_[typeIndex][flat ? 1 : 0]);
	}

	auto shape = std::make_unique<ShapeObject>();
	shape->Initialize(*dx_, type, objectName, flat);
	shapes_.push_back(std::move(shape));
	return shapes_.back().get();
}

void GameScene::DeleteShape(int index)
{
	if ( index < 0 || index >= static_cast< int >(shapes_.size()) )
	{
		return;
	}
	// DrawUi() はこのフレームのコマンドを積む前に呼ばれ、前フレームのGPU処理はウィンドウごとの
	// 待機で終わっているので、ここでバッファを解放してよい
	shapes_.erase(shapes_.begin() + index);
	selection_ = {};
}

void GameScene::Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight)
{
	camera_.Update(input);

	const Matrix4x4& viewProjection = camera_.GetViewProjectionMatrix();
	object1_.Update(viewProjection);
	object2_.Update(viewProjection);
	sprite_.Update(clientWidth, clientHeight);
	instancing_.Update(viewProjection);

	for ( auto& shape : shapes_ )
	{
		shape->Update(viewProjection);
	}

	for ( ParticleEmitter* emitter : emitters_ )
	{
		emitter->Update(deltaTime); // ライブラリの更新(ホットリロード)もここで取り込む
		emitter->BuildInstances(camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), clientWidth, clientHeight);
	}

	// --- コライダー・クリック選択 ---
	if ( input.TriggerKey(DIK_F4) )
	{
		showColliders_ = !showColliders_;
	}
	// モデルのコライダーは、今のモデルの範囲(読み込み設定の適用後)に自動で合わせる
	for ( int i = 0; i < kModelCount; ++i )
	{
		if ( modelColliders_[i].autoFit )
		{
			const Bounds& bounds = assets_->GetModel(models_[i]->modelNode).bounds;
			if ( bounds.valid )
			{
				modelColliders_[i].FitToBounds({ bounds.min, bounds.max }, 0.05f);
			}
		}
	}
	UpdatePicking(input, clientWidth, clientHeight);
	BuildDebugLines();
}

std::vector<GameScene::ColliderItem> GameScene::CollectColliders() const
{
	std::vector<ColliderItem> items;

	// モデル
	for ( int i = 0; i < kModelCount; ++i )
	{
		if ( !modelVisible_[i] )
		{
			continue;
		}
		const TransformData& t = models_[i]->transform;
		items.push_back({ { SceneItemKind::Model, i }, modelColliders_[i], MakeAffineMatrix(t.scale, t.rotate, t.translate), false });
	}

	// 図形
	for ( size_t i = 0; i < shapes_.size(); ++i )
	{
		if ( shapes_[i]->visible )
		{
			items.push_back({ { SceneItemKind::Shape, static_cast< int >(i) }, shapes_[i]->collider, shapes_[i]->GetWorldMatrix(), false });
		}
	}

	// インスタンシングのデモ(全インスタンスをまとめた箱)
	if ( instancing_.enabled )
	{
		BoxCollider box;
		box.FitToBounds(instancing_.GetWorldBounds());
		items.push_back({ { SceneItemKind::InstancingDemo, 0 }, box, MakeIdentityMatrix(), false });
	}

	// 3Dのパーティクルエミッタ(発生範囲の箱。2Dは画面座標なので3Dのレイでは選べない)
	for ( int i = 0; i < static_cast< int >(std::size(emitters_)); ++i )
	{
		const ParticleEmitter& emitter = *emitters_[i];
		if ( !emitter.enabled || emitter.GetRenderMode() != ParticleRenderMode::Billboard3D )
		{
			continue;
		}
		const Vector3& extent = emitter.GetConfig().spawnExtent;
		BoxCollider box;
		box.size = { std::max(extent.x * 2.0f, 0.5f), std::max(extent.y * 2.0f, 0.5f), std::max(extent.z * 2.0f, 0.5f) };
		items.push_back({ { SceneItemKind::Emitter, i }, box, MakeTranslateMatrix({ emitter.position.x, emitter.position.y, emitter.position.z, 0.0f }), true });
	}

	return items;
}

SceneSelection GameScene::PickAt(const Vector2& cursor, float clientWidth, float clientHeight) const
{
	// スプライトは画面の最前面に描かれるので、3Dより先に調べる(画面上の四角形。回転は無視)
	if ( sprite_.enabled )
	{
		const float x0 = sprite_.transform.translate.x;
		const float y0 = sprite_.transform.translate.y;
		const float x1 = x0 + 640.0f * sprite_.transform.scale.x;
		const float y1 = y0 + 360.0f * sprite_.transform.scale.y;
		if ( cursor.x >= std::min(x0, x1) && cursor.x <= std::max(x0, x1) && cursor.y >= std::min(y0, y1) && cursor.y <= std::max(y0, y1) )
		{
			return { SceneItemKind::Sprite, 0 };
		}
	}

	// 3D: カーソルからのレイが、いちばん近くで当たった物
	const Ray ray = ScreenPointToRay(camera_.GetViewProjectionMatrix(), cursor.x, cursor.y, clientWidth, clientHeight);
	SceneSelection best;
	float bestDistance = 1e30f;
	for ( const ColliderItem& item : CollectColliders() )
	{
		float distance = 0.0f;
		if ( RayIntersectsBox(ray, item.collider, item.world, &distance) && distance < bestDistance )
		{
			bestDistance = distance;
			best = item.id;
		}
	}
	return best;
}

void GameScene::UpdatePicking(InputManager& input, float clientWidth, float clientHeight)
{
	hover_ = {};

	// UIの上・右クリックでカメラを回している間は、物を選ばない
	Vector2 cursor{};
	if ( input.IsMouseCaptured() || input.PushMouseButton(1) || !input.GetCursorClientPos(cursor) )
	{
		return;
	}

	hover_ = PickAt(cursor, clientWidth, clientHeight);
	if ( input.TriggerMouseButton(0) )
	{
		selection_ = hover_; // 何もない所をクリックしたら、選択が外れる
	}
}

void GameScene::BuildDebugLines()
{
	debug_.Begin(camera_.GetViewProjectionMatrix());

	const std::vector<ColliderItem> items = CollectColliders();
	for ( const ColliderItem& item : items )
	{
		const bool selected = ( item.id == selection_ );
		const bool hovered = ( item.id == hover_ );

		if ( selected )
		{
			debug_.ColliderBox(item.collider, item.world, DebugDraw::Color::kSelected);

			// 選択中の物のローカル軸(X赤・Y緑・Z青)。Unityの移動ギズモのように、向きが分かる
			const Vector3 origin = { item.world.m[3][0], item.world.m[3][1], item.world.m[3][2] };
			const Vector4* axisColors[3] = { &DebugDraw::Color::kAxisX, &DebugDraw::Color::kAxisY, &DebugDraw::Color::kAxisZ };
			for ( int axis = 0; axis < 3; ++axis )
			{
				const Vector3 dir = Normalize({ item.world.m[axis][0], item.world.m[axis][1], item.world.m[axis][2] });
				debug_.Arrow(origin, { origin.x + dir.x, origin.y + dir.y, origin.z + dir.z }, *axisColors[axis]);
			}
		} else if ( hovered )
		{
			debug_.ColliderBox(item.collider, item.world, DebugDraw::Color::kHover);
		} else if ( showColliders_ )
		{
			debug_.ColliderBox(item.collider, item.world, item.isEmitter ? DebugDraw::Color::kEmitter : DebugDraw::Color::kCollider);
		}
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
	for ( int i = 0; i < kModelCount; ++i )
	{
		if ( modelVisible_[i] )
		{
			models_[i]->Draw(commandList, assets, lightAddress);
		}
	}

	// --- 図形(PSOとトポロジは図形ごとにShapeObject::Drawが設定する) ---
	for ( const auto& shape : shapes_ )
	{
		shape->Draw(commandList, pipelines, assets, lightAddress, modelBlendMode_);
	}

	// --- スプライト ---
	sprite_.Draw(commandList, pipelines, lightAddress);

	// --- インスタンシング描画 ---
	instancing_.Draw(commandList, pipelines, assets.GetParticleTexture().srvGpu, lightAddress);

	// --- パーティクル: 3Dビルボード → 2Dスクリーン(2Dは最後に重ねる) ---
	DrawEmitters(context, ParticleRenderMode::Billboard3D, lightAddress);
	DrawEmitters(context, ParticleRenderMode::Screen2D, lightAddress);

	// --- デバッグ線(コライダーの枠・選択の強調)。深度テストなしで一番手前に重ねる ---
	debug_.Draw(commandList, pipelines, assets, lightAddress);
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
	debug_.Shutdown();

	for ( auto& shape : shapes_ )
	{
		shape->Shutdown();
	}

	for ( ParticleEmitter* emitter : emitters_ )
	{
		emitter->Shutdown();
	}
}

void GameScene::DrawUi()
{
#ifdef USE_IMGUI
	// --- 全体の設定 ---
	ImGui::Begin("Game Debug");

	if ( ImGui::Button("Open Particle Editor (F2)") )
	{
		openEditorRequested_ = true;
	}
	ImGui::Checkbox("Show Colliders (F4)", &showColliders_);
	ImGui::TextDisabled("Left click: select / click empty space: deselect");
	ImGui::Separator();

	ImGui::Text("Model / Shape");
	int blendIndex = static_cast< int >( modelBlendMode_ );
	if ( ImGui::Combo("Blend Mode", &blendIndex, kBlendModeItems) )
	{
		modelBlendMode_ = static_cast< ParticleBlendMode >( blendIndex );
	}

	ImGui::End();

	// --- 仮のオブジェクト管理UI ---
	DrawHierarchyUi();
	DrawPropertiesUi();
#endif
}

void GameScene::HierarchyRow(const char* label, SceneItemKind kind, int index, bool* visible)
{
#ifdef USE_IMGUI
	ImGui::PushID(static_cast< int >( kind ) * 1000 + index);

	if ( visible != nullptr )
	{
		ImGui::Checkbox("##visible", visible);
		ImGui::SameLine();
	}
	if ( ImGui::Selectable(label, selection_.Is(kind, index)) )
	{
		selection_ = { kind, index };
	}

	ImGui::PopID();
#else
	( void ) label;
	( void ) kind;
	( void ) index;
	( void ) visible;
#endif
}

void GameScene::DrawHierarchyUi()
{
#ifdef USE_IMGUI
	ImGui::Begin("Hierarchy");

	// 図形を追加する
	if ( ImGui::Button("+ Add Shape") )
	{
		ImGui::OpenPopup("AddShapePopup");
	}
	if ( ImGui::BeginPopup("AddShapePopup") )
	{
		// 3D版(Box / Line / Triangle / Sphere / Cone)の次に、2D版(Quad / Circle)
		for ( int i = 0; i < kShapeTypeCount * 2; ++i )
		{
			const ShapeType type = static_cast< ShapeType >(i % kShapeTypeCount);
			const bool flat = ( i >= kShapeTypeCount );
			if ( flat )
			{
				if ( !ShapeFactory::HasFlatVersion(type) )
				{
					continue; // Line / Triangle / Cone は2D版でも同じ形なので、並べない
				}
				if ( i == kShapeTypeCount )
				{
					ImGui::Separator();
				}
			}

			if ( ImGui::MenuItem(ShapeFactory::GetDisplayName(type, flat)) )
			{
				ShapeObject* shape = AddShape(type, "", flat);
				// 追加した物が重ならないよう、少しずつずらして置く
				const int n = static_cast< int >( shapes_.size() );
				shape->transform.translate = { static_cast< float >( ( n - 1 ) % 5 - 2 ) * 1.5f, 1.5f, 0.0f, 0.0f };
				selection_ = { SceneItemKind::Shape, n - 1 };
			}
		}
		ImGui::EndPopup();
	}
	ImGui::Separator();

	HierarchyRow("Main Camera", SceneItemKind::Camera, 0, nullptr);
	HierarchyRow("Directional Light", SceneItemKind::Light, 0, nullptr);

	ImGui::Separator();
	HierarchyRow("Object 1", SceneItemKind::Model, 0, &modelVisible_[0]);
	HierarchyRow("Object 2", SceneItemKind::Model, 1, &modelVisible_[1]);
	HierarchyRow("Sprite", SceneItemKind::Sprite, 0, &sprite_.enabled);
	HierarchyRow("Instancing Demo", SceneItemKind::InstancingDemo, 0, &instancing_.enabled);

	ImGui::Separator();
	ImGui::TextDisabled("Shapes");
	for ( size_t i = 0; i < shapes_.size(); ++i )
	{
		const std::string label = shapes_[i]->name + "  [" + shapes_[i]->GetDisplayName() + "]";
		HierarchyRow(label.c_str(), SceneItemKind::Shape, static_cast< int >(i), &shapes_[i]->visible);
	}

	ImGui::Separator();
	ImGui::TextDisabled("Particles");
	for ( int i = 0; i < static_cast< int >(std::size(emitters_)); ++i )
	{
		const char* mode = ( emitters_[i]->GetRenderMode() == ParticleRenderMode::Screen2D ) ? "2D" : "3D";
		const std::string label = emitters_[i]->GetName() + "  [" + mode + "]";
		HierarchyRow(label.c_str(), SceneItemKind::Emitter, i, &emitters_[i]->enabled);
	}

	ImGui::End();
#endif
}

void GameScene::DrawPropertiesUi()
{
#ifdef USE_IMGUI
	ImGui::Begin("Properties");

	int deleteShapeIndex = -1;

	switch ( selection_.kind )
	{
		case SceneItemKind::Camera:
			ImGui::Text("Main Camera");
			ImGui::Separator();
			camera_.DrawUi();
			break;

		case SceneItemKind::Light:
			ImGui::Text("Directional Light");
			ImGui::Separator();
			light_.DrawUi();
			break;

		case SceneItemKind::Model:
			if ( selection_.index >= 0 && selection_.index < kModelCount )
			{
				models_[selection_.index]->DrawUi(selection_.index == 0 ? "Object 1" : "Object 2");
				modelColliders_[selection_.index].DrawUi();
			}
			break;

		case SceneItemKind::Shape:
			if ( selection_.index >= 0 && selection_.index < static_cast< int >(shapes_.size()) )
			{
				shapes_[selection_.index]->DrawUi();
				ImGui::Separator();
				if ( ImGui::Button("Delete Shape") )
				{
					deleteShapeIndex = selection_.index;
				}
			}
			break;

		case SceneItemKind::Sprite:
			sprite_.DrawUi();
			break;

		case SceneItemKind::InstancingDemo:
			instancing_.DrawUi();
			break;

		case SceneItemKind::Emitter:
			if ( selection_.index >= 0 && selection_.index < static_cast< int >(std::size(emitters_)) )
			{
				emitters_[selection_.index]->DrawUi();
			}
			break;

		default:
			ImGui::TextDisabled("Select an item in the Hierarchy.");
			break;
	}

	ImGui::End();

	// 描画に使う前(このフレームのDrawの前)に消す
	if ( deleteShapeIndex >= 0 )
	{
		DeleteShape(deleteShapeIndex);
	}
#endif
}

bool GameScene::ConsumeOpenEditorRequest()
{
	const bool requested = openEditorRequested_;
	openEditorRequested_ = false;
	return requested;
}