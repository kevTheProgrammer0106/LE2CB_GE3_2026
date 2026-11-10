#pragma once

#include "InputManager.h"
#include "WinApp.h"
#include "WindowRenderer.h"

class DirectXCommon;

/// <summary>
/// 1つのOSウィンドウに必要なものをまとめたクラス(WinApp + WindowRenderer + InputManager)。
/// メンバーの宣言順 = 生成順。破棄は逆順(input → renderer → winApp)になる
/// </summary>
class AppWindow
{
public:
	void Initialize(HINSTANCE instance, DirectXCommon& dx, const wchar_t* title,
		uint32_t width, uint32_t height, WindowCloseBehavior closeBehavior, bool vsync,
		const Vector4& clearColor = kDefaultClearColor);

	/// 背景色の既定(青っぽい色)
	static constexpr Vector4 kDefaultClearColor{ 0.1f, 0.25f, 0.5f, 1.0f };

	/// フレーム先頭で呼ぶ。入力を更新し、保留中のリサイズを適用する。
	/// false が返ったら(最小化中・非表示中)このフレームの更新・描画はスキップする
	bool BeginFrame();
	/// ImGuiのフレームを開始し、UIが使っている入力をゲーム側から隠す
	void BeginUi();
	/// 描画開始(バリア / RTV・DSV設定 / クリア)
	void PreDraw();
	/// ImGuiを積んで、実行・Presentまで行う
	void PostDraw();

	WinApp& GetWinApp()
	{
		return winApp_;
	}
	WindowRenderer& GetRenderer()
	{
		return renderer_;
	}
	InputManager& GetInput()
	{
		return input_;
	}

private:
	WinApp winApp_;
	WindowRenderer renderer_;
	InputManager input_;
};