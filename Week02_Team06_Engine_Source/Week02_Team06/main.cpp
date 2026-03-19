#include "pch.h"

#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include "Window.h"
#include "Input.h"
#include "Graphics.h"
#include "Renderer.h"

#include "App.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	UApp app;

	if (!app.Initialize(hInstance))
		return -1;

	app.Run();
	app.Release();


	return 0;
}