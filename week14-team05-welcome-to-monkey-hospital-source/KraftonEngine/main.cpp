#include <Windows.h>
#include "Engine/Runtime/Launch.h"

#include <crtdbg.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nShowCmd)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	return Launch(hInstance, nShowCmd);
}
