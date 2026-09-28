#include "InputManager.h"

void InputManager::Initialize(HWND hwnd, HINSTANCE instance)
{
	// DirectInputの初期化
	HRESULT hr = DirectInput8Create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8,
		(void**)&directInput_, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
	assert(SUCCEEDED(hr));
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));
	hr = keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	// マウスデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysMouse, &mouse_, nullptr);
	assert(SUCCEEDED(hr));
	hr = mouse_->SetDataFormat(&c_dfDIMouse2); // lX, lY, lZ, rgbButtons[4] が使える形式
	assert(SUCCEEDED(hr));
	// マウスは非排他・フォアグラウンド。カーソルを隠したくなったらDISCL_EXCLUSIVEに変更を検討
	hr = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));
}

void InputManager::Update()
{
	memcpy(preKey_, key_, sizeof(key_));
	preMouseState_ = mouseState_;

	keyboard_->Acquire();
	HRESULT hr = keyboard_->GetDeviceState(sizeof(key_), key_);
	if (FAILED(hr))
	{
		ZeroMemory(key_, sizeof(key_));
	}

	mouse_->Acquire();
	hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
	if (FAILED(hr))
	{
		ZeroMemory(&mouseState_, sizeof(mouseState_));
	}
}

bool InputManager::PushKey(BYTE key) const
{
	return key_[key];
}

bool InputManager::TriggerKey(BYTE key) const
{
	return key_[key] && !preKey_[key];
}

bool InputManager::ReleaseKey(BYTE key) const
{
	return !key_[key] && preKey_[key];
}