#include <Core/App.h>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    // DPI 배율 설정을 엔진이 직접 관리하도록 설정 (Picking 오차 방지)
    SetProcessDPIAware();

    std::unique_ptr<UApp> AppInstance = std::make_unique<UApp>();

    if (!AppInstance->Initialize(hInstance, nCmdShow))
    {
        return -1;
    }

    return AppInstance->Run();
}
