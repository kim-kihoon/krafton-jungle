#include "pch.h"
#include "Input.h"

bool UInput::Initialize()
{
	PreKeys.fill(false);
	CurKeys.fill(false);
	MousePosition = { 0, 0 };
	MousePositionDelta = { 0, 0 };
	PendingMousePositionDelta = { 0, 0 };
	PendingMouseWheelDelta = 0.0f;
	CurrentMouseWheelDelta = 0.0f;

	return true;
}

void UInput::Update()
{
	ImGuiIO& io = ImGui::GetIO();

	HWND hWnd = GetActiveWindow();
	bool isAppFocused = (hWnd == GetForegroundWindow());

	for (int i = 0; i < 256; ++i)
	{
		PreKeys[i] = CurKeys[i];

		bool isUiBusy = (i < 0x07) ? io.WantCaptureMouse : io.WantCaptureKeyboard;

		if (isUiBusy || !isAppFocused)
		{
			CurKeys[i] = false;
		}
		else
		{
			CurKeys[i] = ((GetAsyncKeyState(i) & 0x8000) != 0);
		}
	}

	if (io.WantCaptureMouse || !isAppFocused)
	{
		MousePositionDelta = { 0, 0 };
		PendingMousePositionDelta = { 0, 0 };
		CurrentMouseWheelDelta = 0.0f;
		PendingMouseWheelDelta = 0.0f;
	}
	else
	{
		MousePositionDelta = PendingMousePositionDelta;
		PendingMousePositionDelta = { 0, 0 };
		CurrentMouseWheelDelta = PendingMouseWheelDelta;
		PendingMouseWheelDelta = 0.0f;
	}
}
void UInput::Release()
{

}

bool UInput::IsKeyDown(int vKey)
{
	return (!PreKeys[vKey] && CurKeys[vKey]);
}

bool UInput::IsKeyPressing(int vKey)
{
	return  (PreKeys[vKey] && CurKeys[vKey]);
}

bool UInput::IsKeyUp(int vKey)
{
	return (PreKeys[vKey] && !CurKeys[vKey]);
}

// Called from Window::WndProc when handling WM_MOUSEMOVE message
void UInput::UpdateMousePosition(POINT MousePos)
{
	PendingMousePositionDelta.x += MousePos.x - MousePosition.x;
	PendingMousePositionDelta.y += MousePos.y - MousePosition.y;
	MousePosition = MousePos;
}
