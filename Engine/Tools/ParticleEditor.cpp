#define NOMINMAX
#include "Engine/Tools/ParticleEditor.h"

#include "Engine/Graphics/AssetCache.h"
#include "Engine/Graphics/BlendModes.h"
#include "Engine/Graphics/DirectXCommon.h"
#include "Engine/Graphics/PipelineManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void ParticleEditor::Initialize(DirectXCommon& dx, ParticleLibrary& library)
{
	library_ = &library;

	// エディタ用のカメラ設定(ゲーム側とは別。F1でデバッグカメラに切り替えて、Fで注視点まわりを回せる)
	CameraSettings cameraSettings;
	cameraSettings.pose.translation = { 0.0f, 0.0f, -5.0f };
	cameraSettings.debugPose.translation = { 0.0f, 0.0f, -5.0f };
	cameraSettings.pivotPoint = { 0.0f, 0.0f, 0.0f }; // グリッドの中心(原点)
	camera_.Initialize(cameraSettings);
	light_.Initialize(dx);
	grid_.Initialize(dx);
	gizmo3d_.Initialize(dx);
	gizmo2d_.Initialize(dx);
	preview_.Initialize(dx);

	// 何かあれば先頭を選んでおく(GameScene::Initialize の後に呼ばれる前提)
	if ( library_->GetCount() > 0 )
	{
		SelectEntry(0);
	}
}

ParticleLibrary::Entry* ParticleEditor::GetSelected() const
{
	if ( library_ == nullptr || selected_ < 0 )
	{
		return nullptr;
	}
	return library_->GetAt(static_cast< size_t >( selected_ ));
}

void ParticleEditor::SelectEntry(int index)
{
	ParticleLibrary::Entry* entry = library_ ? library_->GetAt(static_cast< size_t >( index < 0 ? library_->GetCount() : index )) : nullptr;
	if ( entry == nullptr )
	{
		return;
	}
	selected_ = index;
	working_ = entry->effect;
	working_.Sanitize();
	selectedLayer_ = 0;
	loadedRevision_ = entry->revision;
	dirty_ = false;
	status_.clear();
	idleTimer_ = 0.0f;

	preview_.SetEffect(working_);
	preview_.Clear(); // 時刻も0に戻るので、Burst Once のレイヤーはここから鳴る
}

void ParticleEditor::OnWorkingChanged()
{
	dirty_ = true;

	// プレビューには補正済みの値を渡す(作業コピー自体は、入力途中の値を壊さないようそのまま)
	ParticleEffect safe = working_;
	safe.Sanitize();
	preview_.SetEffect(safe);

	if ( autoApply_ )
	{
		ApplyToGame();
	}
}

void ParticleEditor::ApplyToGame()
{
	ParticleLibrary::Entry* entry = GetSelected();
	if ( entry == nullptr )
	{
		return;
	}
	library_->Apply(*entry, working_);   // revisionが進み、GameSceneのエミッタが次のUpdateで取り込む
	loadedRevision_ = entry->revision;
	working_ = entry->effect;            // 補正後の値に揃える
	ClampSelectedLayer();
	dirty_ = false;
	status_ = "Applied to game";
}

void ParticleEditor::UpdatePreviewPosition(float clientWidth, float clientHeight)
{
	if ( working_.Root().renderMode == ParticleRenderMode::Screen2D )
	{
		preview_.position = { clientWidth * 0.5f, clientHeight * 0.65f, 0.0f };
	} else
	{
		preview_.position = preview3dPosition_;
	}
}

void ParticleEditor::Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight)
{
	camera_.Update(input, clientWidth, clientHeight, deltaTime);

	// ファイル更新などでゲーム側の設定が変わった場合: 未保存の編集が無ければ自動で追従する
	if ( ParticleLibrary::Entry* entry = GetSelected() )
	{
		if ( entry->revision != loadedRevision_ && !dirty_ )
		{
			working_ = entry->effect;
			ClampSelectedLayer();
			loadedRevision_ = entry->revision;
			preview_.SetEffect(working_);
			status_ = "Reloaded (changed outside the editor)";
		}
	}

	UpdatePreviewPosition(clientWidth, clientHeight);

	if ( !paused_ )
	{
		preview_.Update(deltaTime);

		// 自動リプレイ: 継続して出し続けるレイヤーが無く、粒も全部消えたら、少し待ってもう一度鳴らす
		if ( autoReplay_ )
		{
			bool anyLooping = working_.loop;
			for ( const ParticleLayer& layer : working_.layers )
			{
				anyLooping = anyLooping || ( layer.enabled && layer.config.looping );
			}
			if ( !anyLooping && preview_.GetTotalAliveCount() == 0 && preview_.GetEffectTime() > 0.05f )
			{
				idleTimer_ += deltaTime;
				if ( idleTimer_ >= replayDelay_ )
				{
					idleTimer_ = 0.0f;
					preview_.Play();
				}
			} else
			{
				idleTimer_ = 0.0f;
			}
		}
	}
	preview_.BuildInstances(camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), clientWidth, clientHeight);

	// グリッド(3Dのときだけ。2Dプレビューは画面にそのまま置くのでグリッドは意味が無い)
	grid_.Begin(camera_.GetViewProjectionMatrix());
	if ( showGrid_ && working_.Root().renderMode != ParticleRenderMode::Screen2D )
	{
		// 元のDrawGridと同じ配色: 原点を通る線だけ薄い灰色(0xAAAAAAFF)、他は暗めにして粒を邪魔しない
		const Vector4 lineColor{ 0.35f, 0.35f, 0.38f, 1.0f };
		const Vector4 centerColor{ 0.667f, 0.667f, 0.667f, 1.0f };
		grid_.Grid(gridHalfWidth_, static_cast< uint32_t >( gridSubdivision_ ), lineColor, centerColor, gridHeight_);
	}

	BuildGizmos(clientWidth, clientHeight);
}

//===================================================================
// ギズモ
//===================================================================
namespace
{
	const Vector4 kShapeColor{ 1.0f, 0.85f, 0.2f, 1.0f };     // 発生の形(選択中)
	const Vector4 kShapeDimColor{ 0.55f, 0.48f, 0.2f, 1.0f }; // 発生の形(選択していないレイヤー)
	const Vector4 kDirectionColor{ 0.3f, 0.9f, 1.0f, 1.0f };  // 発射方向・広がり
	const Vector4 kGroundColor{ 0.3f, 0.9f, 0.4f, 1.0f };     // 床
	const Vector4 kOriginColor{ 1.0f, 1.0f, 1.0f, 1.0f };     // レイヤーの原点
	const Vector4 kLinkColor{ 0.6f, 0.6f, 0.7f, 1.0f };       // 親子の線
	const Vector4 kMutedColor{ 0.4f, 0.4f, 0.4f, 1.0f };      // ミュート・発生していないレイヤー
}

ParticleConfig& ParticleEditor::SelectedConfig()
{
	ClampSelectedLayer();
	return working_.layers[static_cast< size_t >( selectedLayer_ )].config;
}

void ParticleEditor::ClampSelectedLayer()
{
	if ( working_.layers.empty() )
	{
		working_ = ParticleEffect::FromConfig(ParticleConfig::MakeDefault());
	}
	selectedLayer_ = std::clamp(selectedLayer_, 0, static_cast< int >( working_.layers.size() ) - 1);
}

void ParticleEditor::BuildLayerGizmo(DebugDraw& draw, const ParticleConfig& config, const Vector3& origin, bool selected)
{
	const bool is2D = ( config.renderMode == ParticleRenderMode::Screen2D );
	const Vector4 shapeColor = selected ? kShapeColor : kShapeDimColor;
	const Vector3& extent = config.spawnExtent;

	//--- 発生の形 ---
	if ( gizmoShape_ )
	{
		switch ( config.shape )
		{
			case ParticleEmitShape::Sphere:
			case ParticleEmitShape::Hemisphere:
				if ( is2D )
				{
					draw.Circle(origin, config.shapeRadius, shapeColor);
				} else
				{
					draw.WireSphere(Sphere{ origin, config.shapeRadius }, shapeColor, 12);
				}
				break;
			case ParticleEmitShape::Circle:
				if ( is2D )
				{
					draw.Circle(origin, config.shapeRadius, shapeColor);
				} else
				{
					// XZ平面の円(エミッタのCircleは地面に寝かせた輪)
					const int kSegments = 40;
					for ( int i = 0; i < kSegments; ++i )
					{
						const float a0 = 2.0f * kPi * static_cast< float >( i ) / kSegments;
						const float a1 = 2.0f * kPi * static_cast< float >( i + 1 ) / kSegments;
						draw.Line(origin + Vector3{ std::cos(a0), 0.0f, std::sin(a0) } * config.shapeRadius,
							origin + Vector3{ std::cos(a1), 0.0f, std::sin(a1) } * config.shapeRadius, shapeColor);
					}
				}
				break;
			case ParticleEmitShape::Line:
				draw.Line(origin + Vector3{ -extent.x, 0.0f, 0.0f }, origin + Vector3{ extent.x, 0.0f, 0.0f }, shapeColor);
				break;
			case ParticleEmitShape::Box:
			default:
				draw.BoxAt(origin, extent.x, extent.y, is2D ? 0.0f : extent.z, shapeColor);
				break;
		}
	}

	//--- 発射方向と広がり ---
	if ( gizmoDirection_ && selected && !config.emitOutward )
	{
		const float spread = config.spreadAngle * kDegToRad;
		if ( is2D )
		{
			// 設定のYは上が+。画面のYは下向きなので反転して置く
			const float length = 80.0f;
			const float base = std::atan2(config.direction.y, config.direction.x);
			auto ray = [&](float angle)
			{
				return origin + Vector3{ std::cos(angle), -std::sin(angle), 0.0f } * length;
			};
			draw.Arrow(origin, ray(base), kDirectionColor);
			draw.Line(origin, ray(base - spread), kDirectionColor);
			draw.Line(origin, ray(base + spread), kDirectionColor);
			const int kArc = 12;
			for ( int i = 0; i < kArc; ++i )
			{
				const float t0 = base - spread + 2.0f * spread * static_cast< float >( i ) / kArc;
				const float t1 = base - spread + 2.0f * spread * static_cast< float >( i + 1 ) / kArc;
				draw.Line(ray(t0), ray(t1), kDirectionColor);
			}
		} else
		{
			const float length = 1.5f;
			const Vector3 forward = Normalize(config.direction);
			const Vector3 helper = ( std::fabs(forward.y) < 0.99f ) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
			const Vector3 right = Normalize(Cross(helper, forward));
			const Vector3 up = Cross(forward, right);
			draw.Arrow(origin, origin + forward * length, kDirectionColor);

			// 円錐: 先端の円(中心 = forward * cos、半径 = sin)と、そこへ向かう線
			const Vector3 tipCenter = origin + forward * ( length * std::cos(spread) );
			const float tipRadius = length * std::sin(spread);
			const int kSegments = 24;
			Vector3 previous{};
			for ( int i = 0; i <= kSegments; ++i )
			{
				const float a = 2.0f * kPi * static_cast< float >( i ) / kSegments;
				const Vector3 point = tipCenter + ( right * std::cos(a) + up * std::sin(a) ) * tipRadius;
				if ( i > 0 )
				{
					draw.Line(previous, point, kDirectionColor);
				}
				if ( i % ( kSegments / 4 ) == 0 && i < kSegments )
				{
					draw.Line(origin, point, kDirectionColor);
				}
				previous = point;
			}
		}
	}

	//--- 床 ---
	if ( gizmoGround_ && selected && config.groundCollision )
	{
		if ( is2D )
		{
			draw.Line({ origin.x - 300.0f, config.groundHeight, 0.0f }, { origin.x + 300.0f, config.groundHeight, 0.0f }, kGroundColor);
		} else
		{
			// 床はワールドの高さ(groundHeight は絶対値)。エミッタの真下あたりに板を出す
			draw.WirePlane(Plane{ { 0.0f, 1.0f, 0.0f }, config.groundHeight }, kGroundColor, 2.0f);
		}
	}
}

void ParticleEditor::BuildGizmos(float clientWidth, float clientHeight)
{
	gizmo3d_.Begin(camera_.GetViewProjectionMatrix());
	gizmo2d_.Begin(MakeOrthoGraphicMatrix(0.0f, 0.0f, clientWidth, clientHeight, 0.0f, 100.0f));
	if ( !showGizmos_ )
	{
		return;
	}

	const ParticleEffect& effect = preview_.GetEffect();
	const int count = static_cast< int >( effect.layers.size() );
	for ( int i = 0; i < count; ++i )
	{
		const ParticleLayer& layer = effect.layers[static_cast< size_t >( i )];
		const bool is2D = ( layer.config.renderMode == ParticleRenderMode::Screen2D );
		DebugDraw& draw = is2D ? gizmo2d_ : gizmo3d_;
		const Vector3 origin = preview_.GetLayerPosition(i);
		const bool selected = ( i == selectedLayer_ );
		const bool active = preview_.IsLayerEmitting(i);

		if ( gizmoLayers_ )
		{
			// 原点の十字(発生中は白、待機・ミュートは灰色)
			const Vector4 color = selected ? kShapeColor : ( active ? kOriginColor : kMutedColor );
			const float s = is2D ? 8.0f : 0.15f;
			draw.Line(origin - Vector3{ s, 0.0f, 0.0f }, origin + Vector3{ s, 0.0f, 0.0f }, color);
			draw.Line(origin - Vector3{ 0.0f, s, 0.0f }, origin + Vector3{ 0.0f, s, 0.0f }, color);
			if ( !is2D )
			{
				draw.Line(origin - Vector3{ 0.0f, 0.0f, s }, origin + Vector3{ 0.0f, 0.0f, s }, color);
			}
			if ( layer.parent >= 0 && layer.parent < count )
			{
				const Vector3 parentOrigin = preview_.GetLayerPosition(layer.parent);
				draw.Line(parentOrigin, origin, kLinkColor);
			}
		}

		if ( selected || gizmoAllLayers_ )
		{
			BuildLayerGizmo(draw, layer.config, origin, selected);
		}
	}
}

void ParticleEditor::Draw(const RenderContext& context)
{
	ID3D12GraphicsCommandList* commandList = context.commandList;
	const PipelineManager& pipelines = *context.pipelines;
	const AssetCache& assets = *context.assets;

	commandList->SetGraphicsRootSignature(pipelines.GetRootSignature());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// グリッドが先(深度を書かない線なので、あとから描くパーティクルが上に重なる)
	grid_.Draw(commandList, pipelines, assets, light_.GetGpuAddress());
	preview_.Draw(commandList, pipelines, assets.GetParticleTexture().srvGpu, light_.GetGpuAddress());
	// ギズモは最後(深度を使わない線なので、粒の上に見える)
	gizmo3d_.Draw(commandList, pipelines, assets, light_.GetGpuAddress());
	gizmo2d_.Draw(commandList, pipelines, assets, light_.GetGpuAddress());
}

void ParticleEditor::Shutdown()
{
	grid_.Shutdown();
	gizmo3d_.Shutdown();
	gizmo2d_.Shutdown();
	preview_.Shutdown();
}

#ifdef USE_IMGUI
namespace
{
	// 深さ優先の並び(親の直後に子が来る)で番号を集める
	void CollectOrder(const ParticleEffect& effect, int index, std::vector<int>& out)
	{
		out.push_back(index);
		for ( int i = index + 1; i < static_cast< int >( effect.layers.size() ); ++i )
		{
			if ( effect.layers[static_cast< size_t >( i )].parent == index )
			{
				CollectOrder(effect, i, out);
			}
		}
	}
}
#endif

bool ParticleEditor::DrawLayerTree()
{
	bool structureChanged = false;
#ifdef USE_IMGUI
	std::vector<int> order;
	CollectOrder(working_, 0, order);

	ImGui::BeginChild("LayerTree", ImVec2(0.0f, 130.0f), true);
	for ( int index : order )
	{
		ParticleLayer& layer = working_.layers[static_cast< size_t >( index )];
		ImGui::PushID(index);
		ImGui::Indent(static_cast< float >( working_.GetDepth(index) ) * 14.0f);

		// 左のチェック = 有効(外すとミュート。親をミュートすると子も止まる)
		bool enabled = layer.enabled;
		if ( ImGui::Checkbox("##on", &enabled) )
		{
			layer.enabled = enabled;
			structureChanged = true;
		}
		ImGui::SameLine();
		const bool live = preview_.IsLayerEmitting(index);
		std::string label = ( index == 0 ? "[Parent] " : "" ) + layer.name + ( live ? "  *" : "" );
		if ( ImGui::Selectable(label.c_str(), index == selectedLayer_) )
		{
			selectedLayer_ = index;
		}
		ImGui::Unindent(static_cast< float >( working_.GetDepth(index) ) * 14.0f);
		ImGui::PopID();
	}
	ImGui::EndChild();

	const bool canAdd = static_cast< int >( working_.layers.size() ) < ParticleEffect::kMaxLayers;
	if ( !canAdd )
	{
		ImGui::BeginDisabled();
	}
	if ( ImGui::Button("Add Child") )
	{
		const int created = working_.AddLayer(selectedLayer_, "Layer " + std::to_string(working_.layers.size()), ParticleConfig::MakeSparks());
		if ( created >= 0 )
		{
			// 親の0.2秒後から始まるようにしておく(0だと親と区別がつかない)
			working_.layers[static_cast< size_t >( created )].startTime = 0.2f;
			selectedLayer_ = created;
			structureChanged = true;
		}
	}
	ImGui::SameLine();
	if ( ImGui::Button("Duplicate") )
	{
		const ParticleLayer source = working_.layers[static_cast< size_t >( selectedLayer_ )];
		const int parent = ( selectedLayer_ == 0 ) ? 0 : source.parent; // 親は複製せず、その子として足す
		const int created = working_.AddLayer(parent, source.name + " copy", source.config);
		if ( created >= 0 )
		{
			ParticleLayer& copy = working_.layers[static_cast< size_t >( created )];
			copy.offset = source.offset;
			copy.followParent = source.followParent;
			copy.startTime = ( selectedLayer_ == 0 ) ? 0.2f : source.startTime;
			copy.duration = ( selectedLayer_ == 0 ) ? 0.0f : source.duration;
			selectedLayer_ = created;
			structureChanged = true;
		}
	}
	if ( !canAdd )
	{
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	const bool isRoot = ( selectedLayer_ == 0 );
	if ( isRoot )
	{
		ImGui::BeginDisabled();
	}
	if ( ImGui::Button("Remove") )
	{
		if ( working_.RemoveLayer(selectedLayer_) )
		{
			selectedLayer_ = std::max(selectedLayer_ - 1, 0);
			structureChanged = true;
		}
	}
	if ( isRoot )
	{
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%d / %d layers (each uses a GPU buffer)", static_cast< int >( working_.layers.size() ), ParticleEffect::kMaxLayers);
#endif
	return structureChanged;
}

bool ParticleEditor::DrawLayerSettings()
{
	bool changed = false;
#ifdef USE_IMGUI
	ClampSelectedLayer();
	ParticleLayer& layer = working_.layers[static_cast< size_t >( selectedLayer_ )];

	char nameBuffer[64];
	std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", layer.name.c_str());
	if ( ImGui::InputText("Layer Name", nameBuffer, sizeof(nameBuffer)) )
	{
		layer.name = nameBuffer;
		changed = true;
	}

	if ( selectedLayer_ == 0 )
	{
		ImGui::TextDisabled("Parent layer: sits at the emitter position and starts at 0 s.");
	} else
	{
		// 親は自分より前の番号の中から選ぶ(ループを作れないようにするため)
		const std::string parentName = working_.layers[static_cast< size_t >( layer.parent )].name;
		if ( ImGui::BeginCombo("Parent", parentName.c_str()) )
		{
			for ( int i = 0; i < selectedLayer_; ++i )
			{
				const bool isSelected = ( i == layer.parent );
				if ( ImGui::Selectable(working_.layers[static_cast< size_t >( i )].name.c_str(), isSelected) && !isSelected )
				{
					layer.parent = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		const bool is2D = ( layer.config.renderMode == ParticleRenderMode::Screen2D );
		changed |= ImGui::DragFloat3(is2D ? "Offset (px)" : "Offset", &layer.offset.x, is2D ? 1.0f : 0.05f);
		changed |= ImGui::Checkbox("Follow Parent", &layer.followParent);
		changed |= ImGui::DragFloat("Start (s after parent)", &layer.startTime, 0.01f, 0.0f, 600.0f);
	}
	changed |= ImGui::DragFloat("Duration (0 = open-ended)", &layer.duration, 0.01f, 0.0f, 600.0f);
	if ( layer.duration > 0.0f && layer.config.looping )
	{
		ImGui::TextDisabled("Looping layer: emits for Duration seconds, then stops.");
	}
#endif
	return changed;
}

void ParticleEditor::DrawTimeline()
{
#ifdef USE_IMGUI
	ParticleEffect safe = working_;
	safe.Sanitize();
	const std::vector<ParticleLayerWindow> windows = ParticleTimeline::ComputeWindows(safe);

	// 表示する時間の幅: 有限なレイヤーの終わり、または開始時刻の最大 + 余白
	float span = std::max(ParticleTimeline::ComputeLength(safe), 1.0f);
	for ( const ParticleLayerWindow& w : windows )
	{
		span = std::max(span, w.start + 1.0f);
	}
	span *= 1.1f;
	if ( working_.loop && working_.length > 0.0f )
	{
		span = std::max(span, working_.length * 1.05f);
	}

	const float rowHeight = 18.0f;
	const float labelWidth = 90.0f;
	const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f);
	const float barWidth = width - labelWidth;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	std::vector<int> order;
	CollectOrder(safe, 0, order);
	const float totalHeight = rowHeight * static_cast< float >( order.size() ) + 14.0f;
	ImGui::InvisibleButton("##timeline", ImVec2(width, totalHeight));
	const bool clicked = ImGui::IsItemClicked();
	const ImVec2 mouse = ImGui::GetMousePos();

	drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + totalHeight), IM_COL32(25, 25, 30, 255));

	// 目盛り(1秒ごと。長いときは間引く)
	const float tickStep = span > 20.0f ? 5.0f : 1.0f;
	for ( float t = 0.0f; t <= span; t += tickStep )
	{
		const float x = origin.x + labelWidth + barWidth * ( t / span );
		drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + totalHeight), IM_COL32(60, 60, 70, 255));
		char text[16];
		std::snprintf(text, sizeof(text), "%.0fs", t);
		drawList->AddText(ImVec2(x + 2.0f, origin.y), IM_COL32(150, 150, 160, 255), text);
	}

	for ( size_t row = 0; row < order.size(); ++row )
	{
		const int index = order[row];
		const ParticleLayer& layer = safe.layers[static_cast< size_t >( index )];
		const ParticleLayerWindow& w = windows[static_cast< size_t >( index )];
		const float y0 = origin.y + 14.0f + rowHeight * static_cast< float >( row );
		const float y1 = y0 + rowHeight - 2.0f;

		if ( index == selectedLayer_ )
		{
			drawList->AddRectFilled(ImVec2(origin.x, y0), ImVec2(origin.x + width, y1), IM_COL32(60, 60, 30, 120));
		}
		drawList->AddText(ImVec2(origin.x + 2.0f + static_cast< float >( safe.GetDepth(index) ) * 6.0f, y0 + 1.0f),
			IM_COL32(220, 220, 220, 255), layer.name.c_str());

		const float startX = origin.x + labelWidth + barWidth * ( std::min(w.start, span) / span );
		const float endX = w.infinite ? origin.x + width : origin.x + labelWidth + barWidth * ( std::min(w.end, span) / span );
		const ImU32 color = layer.enabled ? ( index == selectedLayer_ ? IM_COL32(255, 200, 60, 255) : IM_COL32(90, 150, 220, 255) )
			: IM_COL32(90, 90, 90, 255);
		drawList->AddRectFilled(ImVec2(startX, y0 + 2.0f), ImVec2(std::max(endX, startX + 3.0f), y1), color, 3.0f);
		if ( w.infinite )
		{
			// 終わらないレイヤーは右端に矢じり(→)で示す
			drawList->AddTriangleFilled(ImVec2(endX - 8.0f, y0 + 2.0f), ImVec2(endX, ( y0 + y1 ) * 0.5f + 1.0f), ImVec2(endX - 8.0f, y1), IM_COL32(20, 20, 20, 200));
		}

		if ( clicked && mouse.y >= y0 && mouse.y < y0 + rowHeight )
		{
			selectedLayer_ = index;
		}
	}

	// 再生位置(ループのときは折り返した時刻)
	const float playhead = std::min(preview_.GetEffectTime(), span);
	const float playX = origin.x + labelWidth + barWidth * ( playhead / span );
	drawList->AddLine(ImVec2(playX, origin.y), ImVec2(playX, origin.y + totalHeight), IM_COL32(255, 80, 80, 255), 2.0f);
#endif
}

void ParticleEditor::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::Begin("Particle Editor");

	camera_.DrawUi();
	ImGui::Separator();

	if ( library_ == nullptr || library_->GetCount() == 0 )
	{
		ImGui::Text("No particle emitters registered.");
		ImGui::TextWrapped("Declare a ParticleEmitter in your scene and call Initialize(dx, library, name, config).");
		ImGui::End();
		return;
	}

	// --- 一覧(GameSceneで宣言されたエミッタ) ---
	{
		ParticleLibrary::Entry* current = GetSelected();
		const char* currentName = current ? current->name.c_str() : "(none)";
		if ( ImGui::BeginCombo("Emitter", currentName) )
		{
			for ( size_t i = 0; i < library_->GetCount(); ++i )
			{
				ParticleLibrary::Entry* entry = library_->GetAt(i);
				const bool isSelected = ( static_cast< int >( i ) == selected_ );
				if ( ImGui::Selectable(entry->name.c_str(), isSelected) && !isSelected )
				{
					SelectEntry(static_cast< int >( i ));
				}
			}
			ImGui::EndCombo();
		}
	}

	ParticleLibrary::Entry* entry = GetSelected();
	if ( entry == nullptr )
	{
		ImGui::End();
		return;
	}

	// --- Apply / 保存 / 読み込み ---
	if ( dirty_ )
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unapplied changes");
	} else
	{
		ImGui::TextDisabled("In sync with game");
	}

	if ( ImGui::Button("Apply to Game") )
	{
		ApplyToGame();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto Apply", &autoApply_);
	ImGui::SameLine();
	if ( ImGui::Button("Revert") )
	{
		SelectEntry(selected_);
		status_ = "Reverted to the game's values";
	}

	if ( ImGui::Button("Save File") )
	{
		// ゲームに適用していない編集があれば先に適用してから保存する(ファイル = ゲームの値)
		if ( dirty_ )
		{
			ApplyToGame();
		}
		status_ = library_->Save(*entry) ? "Saved: " + library_->GetFilePath(*entry).string() : "Save failed";
	}
	ImGui::SameLine();
	if ( ImGui::Button("Load File") )
	{
		if ( library_->Load(*entry) )
		{
			SelectEntry(selected_); // 読み込んだ値を作業コピーに取り込む
			status_ = "Loaded: " + library_->GetFilePath(*entry).string();
		} else
		{
			status_ = "Load failed (no file or unreadable)";
		}
	}
	if ( !status_.empty() )
	{
		ImGui::TextWrapped("%s", status_.c_str());
	}
	ImGui::TextDisabled("Files in resources/particles/ are watched: saving them elsewhere also hot-reloads.");
	ImGui::Separator();

	// --- プレビュー操作 ---
	ImGui::Text("Preview");
	if ( ImGui::Button("Play") )
	{
		preview_.Play(true); // 最初から(Burst Once・タイムラインも頭から)
		idleTimer_ = 0.0f;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Paused", &paused_);
	ImGui::SameLine();
	if ( ImGui::Button("Burst (64)") )
	{
		preview_.Emit(64);
	}
	ImGui::SameLine();
	if ( ImGui::Button("Clear") )
	{
		preview_.Clear();
	}
	ImGui::Checkbox("Auto Replay", &autoReplay_);
	if ( autoReplay_ )
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Delay (s)", &replayDelay_, 0.05f, 0.0f, 10.0f);
	}
	if ( working_.Root().renderMode == ParticleRenderMode::Billboard3D )
	{
		ImGui::DragFloat3("Preview Position", &preview3dPosition_.x, 0.1f);
	}
	ImGui::Text("Alive: %d   Time: %.2f s", static_cast< int >( preview_.GetTotalAliveCount() ), preview_.GetEffectTime());

	ImGui::Checkbox("Show Gizmos", &showGizmos_);
	if ( showGizmos_ )
	{
		ImGui::Indent();
		ImGui::Checkbox("Shape", &gizmoShape_);
		ImGui::SameLine();
		ImGui::Checkbox("Direction", &gizmoDirection_);
		ImGui::SameLine();
		ImGui::Checkbox("Ground", &gizmoGround_);
		ImGui::Checkbox("Layer Origins", &gizmoLayers_);
		ImGui::SameLine();
		ImGui::Checkbox("Shapes of All Layers", &gizmoAllLayers_);
		ImGui::Unindent();
	}

	ImGui::Checkbox("Show Grid", &showGrid_);
	if ( showGrid_ )
	{
		ImGui::DragFloat("Grid Half Width", &gridHalfWidth_, 0.5f, 1.0f, 200.0f);
		ImGui::SliderInt("Grid Subdivision", &gridSubdivision_, 2, 100);
		ImGui::DragFloat("Grid Height", &gridHeight_, 0.1f);
	}
	ImGui::Separator();

	// --- エフェクト(レイヤーの木・タイミング) ---
	bool changed = false;
	bool structureChanged = false;
	ClampSelectedLayer();

	if ( ImGui::CollapsingHeader("Effect (Layers & Timing)", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		// 重ね合わせ済みの見本(エフェクト全体を置き換える)
		ImGui::Text("Effect Presets");
		if ( ImGui::Button("Explosion Combo") )
		{
			working_ = ParticleEffectPresets::MakeExplosionCombo();
			structureChanged = true;
		}
		ImGui::SameLine();
		if ( ImGui::Button("Campfire") )
		{
			working_ = ParticleEffectPresets::MakeCampfire();
			structureChanged = true;
		}
		ImGui::SameLine();
		if ( ImGui::Button("Magic Orb") )
		{
			working_ = ParticleEffectPresets::MakeMagicOrb();
			structureChanged = true;
		}
		ImGui::Separator();

		structureChanged |= DrawLayerTree();
		DrawTimeline();
		changed |= DrawLayerSettings();

		changed |= ImGui::Checkbox("Loop Effect", &working_.loop);
		if ( working_.loop )
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			changed |= ImGui::DragFloat("Length (0 = auto)", &working_.length, 0.05f, 0.0f, 600.0f);
		}
	}
	if ( structureChanged )
	{
		ClampSelectedLayer();
		changed = true;
	}
	ImGui::Separator();

	// --- 選択中レイヤーの値の編集 ---
	ParticleConfig& c = SelectedConfig();
	ImGui::Text("Editing layer: %s", working_.layers[static_cast< size_t >( selectedLayer_ )].name.c_str());

	// プリセット(選択中レイヤーの設定だけを置き換える。Applyするまでゲームには影響しない)
	ImGui::Text("Layer Presets");
	auto presetButton = [&](const char* label, ParticleConfig preset, bool sameLine)
	{
		if ( sameLine )
		{
			ImGui::SameLine();
		}
		if ( ImGui::Button(label) )
		{
			c = preset;
			changed = true;
		}
	};
	presetButton("Default", ParticleConfig::MakeDefault(), false);
	presetButton("Fire 3D", ParticleConfig::MakeFire(), true);
	presetButton("Sparkle 2D", ParticleConfig::MakeSparkle2D(), true);
	presetButton("Explosion", ParticleConfig::MakeExplosion(), false);
	presetButton("Sparks", ParticleConfig::MakeSparks(), true);
	presetButton("Smoke", ParticleConfig::MakeSmoke(), true);
	ImGui::Separator();

	// --- 値の編集 ---
	const bool is2D = ( c.renderMode == ParticleRenderMode::Screen2D );
	// 2Dはピクセル単位なので、ドラッグの速さを変える
	const float posSpeed = is2D ? 1.0f : 0.05f;
	const float speedStep = is2D ? 1.0f : 0.05f;
	const float sizeStep = is2D ? 1.0f : 0.02f;

	if ( ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		int renderMode = static_cast< int >( c.renderMode );
		if ( ImGui::Combo("Billboard", &renderMode, kRenderModeItems) )
		{
			c.renderMode = static_cast< ParticleRenderMode >( renderMode );
			changed = true;
		}
		int blendMode = static_cast< int >( c.blendMode );
		if ( ImGui::Combo("Blend Mode", &blendMode, kBlendModeItems) )
		{
			c.blendMode = static_cast< ParticleBlendMode >( blendMode );
			changed = true;
		}
		changed |= ImGui::SliderInt("Max Particles", &c.maxParticles, 1, ParticleConfig::kMaxParticlesLimit);
	}

	if ( ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		changed |= ImGui::Checkbox("Looping", &c.looping);
		changed |= ImGui::DragFloat("Emit Rate (/s)", &c.emitRate, 0.5f, 0.0f, 1000.0f);
		changed |= ImGui::DragFloat3("Spawn Extent", &c.spawnExtent.x, posSpeed, 0.0f, 1000.0f);
		changed |= ImGui::DragFloat("Start Delay (s)", &c.startDelay, 0.05f, 0.0f, 60.0f);
	}

	if ( ImGui::CollapsingHeader("Shape") )
	{
		int shape = static_cast< int >( c.shape );
		if ( ImGui::Combo("Emit Shape", &shape, kEmitShapeItems) )
		{
			c.shape = static_cast< ParticleEmitShape >( shape );
			changed = true;
		}
		if ( c.shape == ParticleEmitShape::Box )
		{
			ImGui::TextDisabled("Box uses Spawn Extent above.");
		} else if ( c.shape == ParticleEmitShape::Line )
		{
			ImGui::TextDisabled("Line length = Spawn Extent X x 2.");
		} else
		{
			changed |= ImGui::DragFloat("Shape Radius", &c.shapeRadius, posSpeed, 0.0f, 1000.0f);
			changed |= ImGui::SliderFloat("Shape Thickness", &c.shapeThickness, 0.0f, 1.0f); // 0 = 表面だけ
		}
		changed |= ImGui::Checkbox("Emit Outward", &c.emitOutward); // 形の中心から外向きに飛ばす
	}

	if ( ImGui::CollapsingHeader("Bursts") )
	{
		// Burst Once: Looping を切ったまま、Burst Count 個を1回だけ出す(爆発・被弾など)
		if ( ImGui::Checkbox("Burst Once", &c.burstOnce) )
		{
			if ( c.burstOnce && c.burstCount <= 0 )
			{
				c.burstCount = 32; // 0個のままだと何も出ないので、初期値を入れる
			}
			changed = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(fires Burst Count once, works without Looping)");
		changed |= ImGui::DragInt("Burst Count", &c.burstCount, 1.0f, 0, ParticleConfig::kMaxParticlesLimit);
		if ( !c.burstOnce )
		{
			changed |= ImGui::DragFloat("Burst Interval (s)", &c.burstInterval, 0.05f, 0.05f, 60.0f);
			changed |= ImGui::DragInt("Burst Cycles (0 = forever)", &c.burstCycles, 1.0f, 0, 1000);
		}
	}

	if ( ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		changed |= ImGui::DragFloat("Lifetime Min", &c.lifetimeMin, 0.05f, 0.01f, 60.0f);
		changed |= ImGui::DragFloat("Lifetime Max", &c.lifetimeMax, 0.05f, 0.01f, 60.0f);
		changed |= ImGui::DragFloat("Speed Min", &c.speedMin, speedStep, 0.0f, 2000.0f);
		changed |= ImGui::DragFloat("Speed Max", &c.speedMax, speedStep, 0.0f, 2000.0f);
		changed |= ImGui::DragFloat3("Direction", &c.direction.x, 0.01f, -1.0f, 1.0f);
		changed |= ImGui::SliderFloat("Spread Angle", &c.spreadAngle, 0.0f, 180.0f);
		changed |= ImGui::DragFloat3("Gravity", &c.gravity.x, speedStep, -2000.0f, 2000.0f);
		changed |= ImGui::DragFloat("Drag", &c.drag, 0.01f, 0.0f, 20.0f);
	}

	if ( ImGui::CollapsingHeader("Forces & Space") )
	{
		changed |= ImGui::Checkbox("Local Space", &c.localSpace); // エミッタに付いて動く
		changed |= ImGui::SliderFloat("Inherit Velocity", &c.inheritVelocity, 0.0f, 1.0f);
		changed |= ImGui::DragFloat("Radial Accel", &c.radialAcceleration, speedStep, -2000.0f, 2000.0f); // 負で中心へ
		changed |= ImGui::DragFloat("Orbit Speed (deg/s)", &c.orbitSpeed, 1.0f, -1440.0f, 1440.0f);
		changed |= ImGui::DragFloat("Turbulence", &c.turbulence, 0.05f, 0.0f, 1000.0f);
		if ( c.turbulence > 0.0f )
		{
			changed |= ImGui::DragFloat("Turbulence Frequency", &c.turbulenceFrequency, 0.05f, 0.01f, 50.0f);
			changed |= ImGui::DragFloat("Turbulence Speed", &c.turbulenceSpeed, 0.05f, 0.0f, 50.0f);
		}
	}

	if ( ImGui::CollapsingHeader("Ground Collision") )
	{
		changed |= ImGui::Checkbox("Enable Ground", &c.groundCollision);
		if ( c.groundCollision )
		{
			changed |= ImGui::DragFloat(is2D ? "Ground Y (px)" : "Ground Height", &c.groundHeight, posSpeed);
			changed |= ImGui::SliderFloat("Bounciness", &c.bounciness, 0.0f, 1.0f);
			changed |= ImGui::SliderFloat("Bounce Life Loss", &c.bounceLifeLoss, 0.0f, 1.0f);
		}
	}

	if ( ImGui::CollapsingHeader("Size", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		changed |= ImGui::DragFloat("Size Start", &c.sizeStart, sizeStep, 0.0f, 2000.0f);
		changed |= ImGui::DragFloat("Size End", &c.sizeEnd, sizeStep, 0.0f, 2000.0f);
		int curve = static_cast< int >( c.sizeCurve );
		if ( ImGui::Combo("Size Curve", &curve, kCurveItems) )
		{
			c.sizeCurve = static_cast< ParticleCurve >( curve );
			changed = true;
		}
		changed |= ImGui::SliderFloat("Size Randomness", &c.sizeRandomness, 0.0f, 1.0f);
	}

	if ( ImGui::CollapsingHeader("Rotation") )
	{
		changed |= ImGui::DragFloat("Rotation Min (deg)", &c.rotationMin, 1.0f, -720.0f, 720.0f);
		changed |= ImGui::DragFloat("Rotation Max (deg)", &c.rotationMax, 1.0f, -720.0f, 720.0f);
		changed |= ImGui::DragFloat("Rot Speed Min (deg/s)", &c.rotationSpeedMin, 1.0f, -1440.0f, 1440.0f);
		changed |= ImGui::DragFloat("Rot Speed Max (deg/s)", &c.rotationSpeedMax, 1.0f, -1440.0f, 1440.0f);
	}

	if ( ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		changed |= ImGui::ColorEdit4("Color Start", &c.colorStart.x);
		changed |= ImGui::ColorEdit4("Color End", &c.colorEnd.x);
		int curve = static_cast< int >( c.colorCurve );
		if ( ImGui::Combo("Color Curve", &curve, kCurveItems) )
		{
			c.colorCurve = static_cast< ParticleCurve >( curve );
			changed = true;
		}
		changed |= ImGui::Checkbox("Random Start Color", &c.randomColor);
		if ( c.randomColor )
		{
			changed |= ImGui::ColorEdit4("Color Start B", &c.colorStartB.x);
		}
		changed |= ImGui::Checkbox("3-Color Gradient", &c.useColorMid);
		if ( c.useColorMid )
		{
			changed |= ImGui::ColorEdit4("Color Mid", &c.colorMid.x);
			changed |= ImGui::SliderFloat("Mid Position", &c.colorMidTime, 0.0f, 1.0f);
		}
		changed |= ImGui::SliderFloat("Fade In", &c.fadeIn, 0.0f, 0.5f);
	}

	if ( ImGui::CollapsingHeader("Stretched Billboard") )
	{
		changed |= ImGui::Checkbox("Align To Velocity", &c.alignToVelocity);
		if ( c.alignToVelocity )
		{
			changed |= ImGui::DragFloat("Stretch Scale", &c.stretchScale, 0.005f, 0.0f, 10.0f);
			changed |= ImGui::DragFloat("Stretch Max", &c.stretchMax, 0.05f, 1.0f, 50.0f);
		}
	}

	if ( changed )
	{
		OnWorkingChanged();
	}

	ImGui::End();
#endif
}
