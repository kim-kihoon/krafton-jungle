#include "Engine/Runtime/WindowsWindow.h"

namespace
{
using FDwmSetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

constexpr DWORD DwmUseImmersiveDarkMode = 20;
constexpr DWORD DwmUseImmersiveDarkModeLegacy = 19;

bool IsDarkAppThemeEnabled()
{
	DWORD AppsUseLightTheme = 1;
	DWORD ValueSize = sizeof(AppsUseLightTheme);

	const LSTATUS Result = RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_REG_DWORD,
		nullptr,
		&AppsUseLightTheme,
		&ValueSize);

	return Result == ERROR_SUCCESS && AppsUseLightTheme == 0;
}

FDwmSetWindowAttribute GetDwmSetWindowAttribute()
{
	HMODULE DwmApi = GetModuleHandleW(L"dwmapi.dll");
	if (!DwmApi)
	{
		DwmApi = LoadLibraryW(L"dwmapi.dll");
	}

	if (!DwmApi)
	{
		return nullptr;
	}

	return reinterpret_cast<FDwmSetWindowAttribute>(GetProcAddress(DwmApi, "DwmSetWindowAttribute"));
}

void SetWindowImmersiveDarkMode(HWND HWindow, BOOL bUseDarkMode)
{
	FDwmSetWindowAttribute DwmSetWindowAttribute = GetDwmSetWindowAttribute();
	if (!DwmSetWindowAttribute)
	{
		return;
	}

	HRESULT Result = DwmSetWindowAttribute(
		HWindow,
		DwmUseImmersiveDarkMode,
		&bUseDarkMode,
		sizeof(bUseDarkMode));

	if (FAILED(Result))
	{
		DwmSetWindowAttribute(
			HWindow,
			DwmUseImmersiveDarkModeLegacy,
			&bUseDarkMode,
			sizeof(bUseDarkMode));
	}
}
}

void FWindowsWindow::Initialize(HWND InHWindow)
{
	HWindow = InHWindow;

	RECT Rect;
	GetClientRect(HWindow, &Rect);
	Width = static_cast<float>(Rect.right - Rect.left);
	Height = static_cast<float>(Rect.bottom - Rect.top);
}

void FWindowsWindow::ApplySystemTheme()
{
	if (!HWindow)
	{
		return;
	}

	const BOOL bUseDarkMode = IsDarkAppThemeEnabled() ? TRUE : FALSE;
	SetWindowImmersiveDarkMode(HWindow, bUseDarkMode);

	SetWindowPos(
		HWindow,
		nullptr,
		0,
		0,
		0,
		0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	RedrawWindow(HWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

void FWindowsWindow::OnResized(unsigned int InWidth, unsigned int InHeight)
{
	Width = static_cast<float>(InWidth);
	Height = static_cast<float>(InHeight);
}

POINT FWindowsWindow::ScreenToClientPoint(POINT ScreenPoint) const
{
	ScreenToClient(HWindow, &ScreenPoint);
	return ScreenPoint;
}
