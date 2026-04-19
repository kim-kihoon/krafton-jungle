#pragma once

extern UINT g_width;
extern UINT g_height;
extern bool bResized;

class UWindow
{
public:
	UWindow() = default;
	~UWindow() = default;

public:
	bool Initialize(HINSTANCE hInstance, uint32 width, uint32 height);
	void Release();

	HWND GetHWnd() const { return hWnd; }

private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND      hWnd = {};
	HINSTANCE hInstance = {};
};