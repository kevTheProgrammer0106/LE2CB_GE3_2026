#include "AppWindow.h"

#include "DirectXCommon.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void AppWindow::Initialize(HINSTANCE instance, DirectXCommon& dx, const wchar_t* title,
	uint32_t width, uint32_t height, WindowCloseBehavior closeBehavior, bool vsync)
{
	winApp_.Initialize(instance, title, width, height, closeBehavior);
	renderer_.Initialize(&winApp_, &dx);
	renderer_.SetVSync(vsync);
	input_.Initialize(&winApp_);
}

bool AppWindow::BeginFrame()
{
	// 最前面でないウィンドウでは、DirectInputが「何も押されていない」状態を返す
	input_.Update();
	return renderer_.BeginFrame();
}

void AppWindow::BeginUi()
{
	renderer_.BeginImGuiFrame();
#ifdef USE_IMGUI
	// ImGuiがマウス・キーボードを使っている間は、カメラ操作などのゲーム入力を無効にする
	const ImGuiIO& io = ImGui::GetIO();
	input_.SetUiCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
#endif
}

void AppWindow::PreDraw()
{
	renderer_.PreDraw();
}

void AppWindow::PostDraw()
{
	renderer_.RenderImGui();
	renderer_.PostDraw();
}