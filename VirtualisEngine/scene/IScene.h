#pragma once
#include <d3d12.h>

class InputManager;
class AssetCache;
class PipelineManager;

/// <summary>
/// 描画時にシーンへ渡す情報(コマンドリスト + 共有リソース + ウィンドウサイズ)
/// </summary>
struct RenderContext
{
	ID3D12GraphicsCommandList* commandList = nullptr;
	const PipelineManager* pipelines = nullptr;
	const AssetCache* assets = nullptr;
	float width = 0.0f;
	float height = 0.0f;
};

/// <summary>
/// 1ウィンドウに表示する中身(GameScene / ParticleEditor)の共通インターフェース。
/// 初期化は必要な依存が違うので、各クラスがそれぞれ Initialize() を持つ
/// </summary>
class IScene
{
public:
	virtual ~IScene() = default;

	/// ImGuiのUIを組み立てる(NewFrame後・Update前に呼ばれる)
	virtual void DrawUi() = 0;
	/// 更新(入力は、ImGuiが使っている間は自動的に無効になっている)
	virtual void Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight) = 0;
	/// コマンドの積み込み(RTV設定・クリア済みの状態で呼ばれる)
	virtual void Draw(const RenderContext& context) = 0;
	/// GPU待ちが済んだ後、リソースを解放する前に呼ばれる
	virtual void Shutdown()
	{
	}
};