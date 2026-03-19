#include "pch.h"
#include <windowsx.h> 
#include "Window.h"
#include "Input.h"
#include "Renderer.h"
#include "App.h"

UINT g_width = 0;
UINT g_height = 0;
bool bResized = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool UWindow::Initialize(HINSTANCE _hInstance, uint32 _width, uint32 _height)
{
	hInstance = _hInstance;

	WNDCLASSW wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"JungleWindowClass";

	if (!RegisterClassW(&wc))
		return false;

	DWORD Style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

	RECT rect = { 0, 0, (LONG)_width, (LONG)_height };
	AdjustWindowRect(&rect, Style, FALSE);

	hWnd = CreateWindowExW(0,
		L"JungleWindowClass",
		L"Game Tech Lab",
		Style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		rect.right - rect.left,
		rect.bottom - rect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	if (!hWnd)
		return false;

	ShowWindow(hWnd, SW_SHOW);

	return true;
}

void UWindow::Release()
{
	if (hWnd)
	{
		DestroyWindow(hWnd);
		hWnd = nullptr;
	}
}


LRESULT CALLBACK UWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	switch (message)
	{
	case WM_MOUSEMOVE:
	{
		POINT MousePos = {};
		MousePos.x = GET_X_LPARAM(lParam);
		MousePos.y = GET_Y_LPARAM(lParam);
		UInput::GetInstance().UpdateMousePosition(MousePos);
		break;
	}
	case WM_MOUSEWHEEL:
	{
		float MouseWheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA);
		UInput::GetInstance().AddMouseWheelDelta(MouseWheelDelta);
		break;
	}
	case WM_SIZE:
	{
		g_width = LOWORD(lParam);
		g_height = HIWORD(lParam);
		bResized = true;

		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
		
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}