#include "ParticleEditor.h"

#include "AssetCache.h"
#include "BlendModes.h"
#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void ParticleEditor::Initialize(DirectXCommon& dx, ParticleLibrary& library)
{
	library_ = &library;

	// エディタ用のカメラ設定(ゲーム側とは別。F1でデバッグカメラに切り替えて、Fで注視点まわりを回せる)
	CameraSettings cameraSettings;
	cameraSettings.pose.translation = { 0.0f, 0.0f, -10.0f };
	cameraSettings.debugPose.translation = { 0.0f, 0.0f, -10.0f };
	cameraSettings.pivotPoint = { 0.0f, 0.0f, 0.0f }; // グリッドの中心(原点)
	camera_.Initialize(cameraSettings);
	light_.Initialize(dx);
	grid_.Initialize(dx);
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
	return library_->GetAt(static_cast< size_t >(selected_));
}

void ParticleEditor::SelectEntry(int index)
{
	ParticleLibrary::Entry* entry = library_ ? library_->GetAt(static_cast< size_t >(index < 0 ? library_->GetCount() : index)) : nullptr;
	if ( entry == nullptr )
	{
		return;
	}
	selected_ = index;
	working_ = entry->config;
	loadedRevision_ = entry->revision;
	dirty_ = false;
	status_.clear();

	preview_.SetConfig(working_);
	preview_.Clear();
}

void ParticleEditor::OnWorkingChanged()
{
	dirty_ = true;

	// プレビューには補正済みの値を渡す(作業コピー自体は、入力途中の値を壊さないようそのまま)
	ParticleConfig safe = working_;
	safe.Sanitize();
	preview_.SetConfig(safe);

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
	working_ = entry->config;            // 補正後の値に揃える
	dirty_ = false;
	status_ = "Applied to game";
}

void ParticleEditor::UpdatePreviewPosition(float clientWidth, float clientHeight)
{
	if ( working_.renderMode == ParticleRenderMode::Screen2D )
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
			working_ = entry->config;
			loadedRevision_ = entry->revision;
			preview_.SetConfig(working_);
			status_ = "Reloaded (changed outside the editor)";
		}
	}

	UpdatePreviewPosition(clientWidth, clientHeight);

	if ( !paused_ )
	{
		preview_.Update(deltaTime);
	}
	preview_.BuildInstances(camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), clientWidth, clientHeight);

	// グリッド(3Dのときだけ。2Dプレビューは画面にそのまま置くのでグリッドは意味が無い)
	grid_.Begin(camera_.GetViewProjectionMatrix());
	if ( showGrid_ && working_.renderMode != ParticleRenderMode::Screen2D )
	{
		// 元のDrawGridと同じ配色: 原点を通る線だけ薄い灰色(0xAAAAAAFF)、他は暗めにして粒を邪魔しない
		const Vector4 lineColor{ 0.35f, 0.35f, 0.38f, 1.0f };
		const Vector4 centerColor{ 0.667f, 0.667f, 0.667f, 1.0f };
		grid_.Grid(gridHalfWidth_, static_cast< uint32_t >( gridSubdivision_ ), lineColor, centerColor, gridHeight_);
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
}

void ParticleEditor::Shutdown()
{
	grid_.Shutdown();
	preview_.Shutdown();
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
		ImGui::TextWrapped("Declare a ParticleEmitter in GameScene.h and call Initialize(dx, library, name, config).");
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
				const bool isSelected = ( static_cast< int >(i) == selected_ );
				if ( ImGui::Selectable(entry->name.c_str(), isSelected) && !isSelected )
				{
					SelectEntry(static_cast< int >(i));
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
	if ( working_.renderMode == ParticleRenderMode::Billboard3D )
	{
		ImGui::DragFloat3("Preview Position", &preview3dPosition_.x, 0.1f);
	}
	ImGui::Text("Alive: %d", static_cast< int >( preview_.GetAliveCount() ));

	ImGui::Checkbox("Show Grid", &showGrid_);
	if ( showGrid_ )
	{
		ImGui::DragFloat("Grid Half Width", &gridHalfWidth_, 0.5f, 1.0f, 200.0f);
		ImGui::SliderInt("Grid Subdivision", &gridSubdivision_, 2, 100);
		ImGui::DragFloat("Grid Height", &gridHeight_, 0.1f);
	}
	ImGui::Separator();

	// --- プリセット(作業コピーに読み込むだけ。Applyするまでゲームには影響しない) ---
	ImGui::Text("Presets");
	bool changed = false;
	if ( ImGui::Button("Default") )
	{
		working_ = ParticleConfig::MakeDefault();
		changed = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Fire 3D") )
	{
		working_ = ParticleConfig::MakeFire();
		changed = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Sparkle 2D") )
	{
		working_ = ParticleConfig::MakeSparkle2D();
		changed = true;
	}
	if ( ImGui::Button("Explosion") )
	{
		working_ = ParticleConfig::MakeExplosion();
		changed = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Sparks") )
	{
		working_ = ParticleConfig::MakeSparks();
		changed = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Smoke") )
	{
		working_ = ParticleConfig::MakeSmoke();
		changed = true;
	}
	ImGui::Separator();

	// --- 値の編集 ---
	ParticleConfig& c = working_;
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
		changed |= ImGui::DragInt("Burst Count", &c.burstCount, 1.0f, 0, ParticleConfig::kMaxParticlesLimit);
		changed |= ImGui::DragFloat("Burst Interval (s)", &c.burstInterval, 0.05f, 0.05f, 60.0f);
		changed |= ImGui::DragInt("Burst Cycles (0 = forever)", &c.burstCycles, 1.0f, 0, 1000);
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