#include "InputManager.h"

void InputManager::Initialize(WinApp* winApp)
{
	// 借りてきたWinAppのインスタンスを記録
	this->winApp = winApp;

	// DirectInputの初期化
	HRESULT hr = DirectInput8Create(winApp->GetWindowClass().hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
		( void** ) &directInput_, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
	assert(SUCCEEDED(hr));
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));
	hr = keyboard_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	// マウスデバイスの生成
	hr = directInput_->CreateDevice(GUID_SysMouse, &mouse_, nullptr);
	assert(SUCCEEDED(hr));
	hr = mouse_->SetDataFormat(&c_dfDIMouse2); // lX, lY, lZ, rgbButtons[4] が使える形式
	assert(SUCCEEDED(hr));
	// マウスは非排他・フォアグラウンド。カーソルを隠したくなったらDISCL_EXCLUSIVEに変更を検討
	hr = mouse_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));
}

void InputManager::Update()
{
	memcpy(preKey_, key_, sizeof(key_));
	preMouseState_ = mouseState_;

	// フォーカスが別ウィンドウにあるとAcquire/GetDeviceStateが失敗する。
	// その場合は「何も押されていない」状態にする(別ウィンドウの操作を拾わない)
	keyboard_->Acquire();
	HRESULT hr = keyboard_->GetDeviceState(sizeof(key_), key_);
	if ( FAILED(hr) )
	{
		ZeroMemory(key_, sizeof(key_));
	}

	mouse_->Acquire();
	hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
	if ( FAILED(hr) )
	{
		ZeroMemory(&mouseState_, sizeof(mouseState_));
	}
}

bool InputManager::PushKey(BYTE key) const
{
	return !keyboardCaptured_ && key_[key] != 0;
}

bool InputManager::TriggerKey(BYTE key) const
{
	return !keyboardCaptured_ && key_[key] && !preKey_[key];
}

bool InputManager::ReleaseKey(BYTE key) const
{
	return !keyboardCaptured_ && !key_[key] && preKey_[key];
}

void InputManager::CenterCursor() const
{
	HWND hwnd = winApp ? winApp->GetHwnd() : nullptr;
	if ( !hwnd || GetForegroundWindow() != hwnd )
	{
		return;
	}

	// 固定座標ではなく、このウィンドウのクライアント領域の中央を画面座標に変換して使う
	RECT clientRect{};
	GetClientRect(hwnd, &clientRect);
	POINT center = { ( clientRect.right - clientRect.left ) / 2, ( clientRect.bottom - clientRect.top ) / 2 };
	ClientToScreen(hwnd, &center);
	SetCursorPos(center.x, center.y);
}