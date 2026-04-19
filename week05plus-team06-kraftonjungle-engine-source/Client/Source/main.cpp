#include "Core/GameEngine.h"
#include "Platform/Windows/WindowsEngineLaunch.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// Windows 런처에 넘길 클라이언트 실행 설정을 구성한다.
	FEngineLaunchConfig Config;
	Config.Title = L"Jungle Client";
	Config.Width = 1280;
	Config.Height = 720;
	Config.CreateEngine = []()
	{
		return std::make_unique<FGameEngine>();
	};

	// OS 진입점 이후의 제어를 Windows 전용 Launch 계층으로 넘긴다.
	FWindowsEngineLaunch Launch;
	return Launch.Run(hInstance, Config);
}
