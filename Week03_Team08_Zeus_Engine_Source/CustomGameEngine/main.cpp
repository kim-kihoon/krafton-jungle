#include "EngineApp.h"
#include <crtdbg.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR IpCmdLine, int nShowCmd) 
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(671);

	EngineApp* app = new EngineApp();
	
	if (!app->Initialize(hInstance))
		return -1;

	app->Run();
	app->Finalize();

	delete app;

	return 0;
}