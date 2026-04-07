#pragma once
#include <Core/AppTypes.h>
#include <Graphics/RendererTypes.h>
#include <windows.h>
#include <objbase.h>
#include <memory>
#include <string>

namespace Math
{
    struct FRay;
}

namespace Graphics { class URenderer; }
namespace Scene { class USceneManager; }
namespace UI { class UEditorLayer; }

/**
 * Verstappen Engine 메인 애플리케이션 클래스로 각 서브시스템을 조립한다.
 */
class UApp
{
public:
    UApp();
    ~UApp();

    bool Initialize(HINSTANCE InHInstance, int InCmdShow);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Update(float DeltaTime);
    void UniformCullingAndRenderCollect();
    void Picking();
    bool CheckHit(const DirectX::XMFLOAT3& cameraPosition, const Math::FRay& pickRay, uint32_t& OutHitIndex);
    void Render(float DeltaTime);
    void UpdateFramePerformanceMetrics(float InDeltaTime);
    void UpdateCamera(float InDeltaTime);

private:
    HWND WindowHandle;
    HINSTANCE InstanceHandle;
    std::wstring AppName = L"Verstappen Engine";
    int ScreenWidth;
    int ScreenHeight;

    std::unique_ptr<Graphics::URenderer> Renderer;
    std::unique_ptr<Scene::USceneManager> SceneManager;
    std::unique_ptr<UI::UEditorLayer> EditorLayer;

    float DeltaTime = 0.0f;
    Graphics::FCameraState CameraState;
    float PendingWheelDelta = 0.0f;
    bool bIsRightMouseLooking = false;
    POINT LastMousePosition = {};
    bool bIsCOMInitialized = false;
    bool bPendingPick = false;
    POINT PickPosition = { 0, 0 };
    uint64_t PickStartCycles = 0; // 클릭 발생 시점 사이클 저장
};
