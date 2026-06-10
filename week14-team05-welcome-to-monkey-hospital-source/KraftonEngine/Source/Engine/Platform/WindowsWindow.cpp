#include "Engine/Platform/WindowsWindow.h"

void FWindowsWindow::Initialize(HWND InHWindow)
{
	HWindow = InHWindow;
	WindowedStyle = static_cast<LONG>(GetWindowLong(HWindow, GWL_STYLE));
	WindowedExStyle = static_cast<LONG>(GetWindowLong(HWindow, GWL_EXSTYLE));
	GetWindowRect(HWindow, &WindowedRect);
	RefreshClientSize();
}

void FWindowsWindow::RefreshClientSize()
{
	if (!HWindow)
	{
		return;
	}

	RECT Rect = {};
	GetClientRect(HWindow, &Rect);
	Width = static_cast<float>(Rect.right - Rect.left);
	Height = static_cast<float>(Rect.bottom - Rect.top);
}

void FWindowsWindow::OnResized(unsigned int InWidth, unsigned int InHeight)
{
	Width = static_cast<float>(InWidth);
	Height = static_cast<float>(InHeight);
}

void FWindowsWindow::SetFullscreen(bool bInFullscreen)
{
	if (!HWindow || bFullscreen == bInFullscreen)
	{
		return;
	}

	if (bInFullscreen)
	{
		if (!bFullscreen)
		{
			WindowedStyle = static_cast<LONG>(GetWindowLong(HWindow, GWL_STYLE));
			WindowedExStyle = static_cast<LONG>(GetWindowLong(HWindow, GWL_EXSTYLE));
			GetWindowRect(HWindow, &WindowedRect);
		}

		MONITORINFO MonitorInfo = {};
		MonitorInfo.cbSize = sizeof(MONITORINFO);
		HMONITOR Monitor = MonitorFromWindow(HWindow, MONITOR_DEFAULTTONEAREST);
		if (Monitor && GetMonitorInfo(Monitor, &MonitorInfo))
		{
			const int MonitorWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
			const int MonitorHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;

			SetWindowLong(HWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE);
			SetWindowLong(HWindow, GWL_EXSTYLE, WS_EX_APPWINDOW);
			SetWindowPos(
				HWindow,
				HWND_TOP,
				MonitorInfo.rcMonitor.left,
				MonitorInfo.rcMonitor.top,
				MonitorWidth,
				MonitorHeight,
				SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}
	}
	else
	{
		SetWindowLong(HWindow, GWL_STYLE, WindowedStyle);
		SetWindowLong(HWindow, GWL_EXSTYLE, WindowedExStyle);
		const int WindowWidth = WindowedRect.right - WindowedRect.left;
		const int WindowHeight = WindowedRect.bottom - WindowedRect.top;
		SetWindowPos(
			HWindow,
			HWND_NOTOPMOST,
			WindowedRect.left,
			WindowedRect.top,
			WindowWidth,
			WindowHeight,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	}

	bFullscreen = bInFullscreen;
	RefreshClientSize();
}

POINT FWindowsWindow::ScreenToClientPoint(POINT ScreenPoint) const
{
	ScreenToClient(HWindow, &ScreenPoint);
	return ScreenPoint;
}
