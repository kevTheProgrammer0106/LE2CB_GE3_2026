#pragma once

#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#include "WorldTransform.h"
#include "WinApp.h"

#include<wrl.h>

// 1ウィンドウに1つ作る。DISCL_FOREGROUNDなので、そのウィンドウが最前面のときだけ入力を受け取る
class InputManager
{
public:
	void Initialize(WinApp* winApp);
	void Update();

	//==================================================
	// UI(ImGui)が入力を使っているときは、ゲーム側の入力を無効にする。
	// 毎フレーム ImGui::NewFrame() の後に io.WantCaptureMouse / io.WantCaptureKeyboard を渡す
	//==================================================
	void SetUiCapture(bool mouseCaptured, bool keyboardCaptured)
	{
		mouseCaptured_ = mouseCaptured;
		keyboardCaptured_ = keyboardCaptured;
	}

	bool PushKey(BYTE key) const;
	bool TriggerKey(BYTE key) const;
	bool ReleaseKey(BYTE key) const;

	//==================================================
	// マウス
	//==================================================
	// フレーム間のマウス移動量(px相当)。カメラ回転に使う
	Vector2 GetMouseDelta() const
	{
		if ( mouseCaptured_ )
		{
			return { 0.0f, 0.0f };
		}
		return { static_cast< float >( mouseState_.lX ), static_cast< float >( mouseState_.lY ) };
	}
	// フレーム間のホイール回転量。ズームに使う(前に回すと正、手前に回すと負が一般的)
	float GetWheelDelta() const
	{
		return mouseCaptured_ ? 0.0f : static_cast< float >( mouseState_.lZ );
	}
	bool PushMouseButton(int button) const
	{
		return !mouseCaptured_ && ( mouseState_.rgbButtons[button] & 0x80 ) != 0;
	}
	bool TriggerMouseButton(int button) const
	{
		return !mouseCaptured_ && ( mouseState_.rgbButtons[button] & 0x80 ) && !( preMouseState_.rgbButtons[button] & 0x80 );
	}

	// ImGuiがマウスを使っている(UIの上にカーソルがある・ドラッグ中)ときは true。
	// このときはクリックしてもゲーム側の物を選ばない
	bool IsMouseCaptured() const
	{
		return mouseCaptured_;
	}

	// カーソルの位置を、このウィンドウのクライアント領域のピクセル座標(左上が原点)で返す。
	// ウィンドウの外にあるとき・ウィンドウが最前面でないときは false(outは変更しない)。
	// クリックで物を選ぶ(ピッキング)のに使う
	bool GetCursorClientPos(Vector2& out) const;

	// このウィンドウのクライアント領域の中央にカーソルを移動する(画面座標に変換して設定する)。
	// ウィンドウが最前面でないときは何もしない
	void CenterCursor() const;

private:
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

	BYTE key_[256]{};
	BYTE preKey_[256]{};

	DIMOUSESTATE2 mouseState_{};
	DIMOUSESTATE2 preMouseState_{};

	bool mouseCaptured_ = false;
	bool keyboardCaptured_ = false;

	// WindowsAPI
	WinApp* winApp = nullptr;
};