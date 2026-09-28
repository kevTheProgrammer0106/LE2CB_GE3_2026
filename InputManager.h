#pragma once

#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#include "WorldTransform.h"

#include<wrl.h>

class InputManager
{
public:
	void Initialize(HWND hwnd, HINSTANCE instance);
	void Update();

	bool PushKey(BYTE key) const;
	bool TriggerKey(BYTE key) const;
	bool ReleaseKey(BYTE key) const;

	//==================================================
	// マウス
	//==================================================
	// フレーム間のマウス移動量(px相当)。カメラ回転に使う
	Vector2 GetMouseDelta() const { return { static_cast<float>(mouseState_.lX), static_cast<float>(mouseState_.lY) }; }
	// フレーム間のホイール回転量。ズームに使う(前に回すと正、手前に回すと負が一般的)
	float GetWheelDelta() const { return static_cast<float>(mouseState_.lZ); }
	bool PushMouseButton(int button) const { return (mouseState_.rgbButtons[button] & 0x80) != 0; }
	bool TriggerMouseButton(int button) const
	{
		return (mouseState_.rgbButtons[button] & 0x80) && !(preMouseState_.rgbButtons[button] & 0x80);
	}

private:
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

	BYTE key_[256]{};
	BYTE preKey_[256]{};

	DIMOUSESTATE2 mouseState_{};
	DIMOUSESTATE2 preMouseState_{};
};