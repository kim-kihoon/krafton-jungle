#pragma once

#include <windows.h>
#include <functional>
#include "EngineTypes.h"
#include "Math/Vector.h"

class URenderer;
class Editor;
class UCameraComponent;
class UPicker;
class UTextBatch;
struct FViewport;

class EngineApp
{
public:
	EngineApp();
	~EngineApp();

public:
	bool Initialize(HINSTANCE hInstance);
	void Run();
	void Finalize();

private:
	void CreateEngineWindow();
	void ReleaseEngineWindow();

	void CreateDefaultResources();

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND HWnd = nullptr;
	URenderer* Renderer = nullptr;
	Editor* EditorInst = nullptr;
	UPicker* Picker = nullptr;
	FViewport* Viewport = nullptr;
};

