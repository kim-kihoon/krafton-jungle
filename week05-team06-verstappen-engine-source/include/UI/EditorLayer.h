#pragma once

#include <UI/EditorTypes.h>
#include <UI/IEditorPanel.h>
#include <UI/Panels/SConsolePanel.h>
#include <UI/Panels/SControlPanel.h>
#include <UI/Panels/SPerformanceOverlay.h>
#include <UI/Panels/SPropertyWindowPanel.h>
#include <UI/Panels/SSceneFileSystemPanel.h>
#include <UI/Panels/SSceneManagerPanel.h>
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <string>
#include <windows.h>
#include <DirectXMath.h>

namespace UI
{
    class UEditorLayer
    {
    public:
        UEditorLayer();
        ~UEditorLayer();

        bool Initialize(const FEditorModuleDependencies& InDependencies);
        void Update(float DeltaTime);
        void BeginFrame();
        void DrawPanels();
        void EndFrame();
        void Cleanup();

        void SetFramePerformanceMetrics(const Core::FFramePerformanceMetrics& InMetrics);
        bool HandleWindowMessage(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam);
        bool WantsKeyboardCapture() const;
        bool IsPointInsideViewport(int X, int Y) const;
        bool IsViewportFocused() const;
        bool HandleLeftMouseDown(int X, int Y);
        void HandleLeftMouseUp(int X, int Y);
        void HandleMouseMove(int X, int Y);
        void CycleGizmoMode();
        int GetViewportX() const;
        int GetViewportY() const;
        int GetViewportWidth() const;
        int GetViewportHeight() const;

        void SetCameraState(Graphics::FCameraState* InCameraState)
        {
            CameraState = InCameraState;
        }

        enum class EGizmoAxis : uint8_t
        {
            None = 0,
            X,
            Y,
            Z
        };

    private:
        static constexpr size_t PANEL_COUNT = static_cast<size_t>(EEditorPanelType::Count);

        struct FGizmoInteractionState
        {
            EGizmoAxis HoveredAxis = EGizmoAxis::None;
            EGizmoAxis ActiveAxis = EGizmoAxis::None;
            bool bIsDragging = false;
            int MouseX = 0;
            int MouseY = 0;
            DirectX::XMFLOAT3 DragStartTranslation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 DragStartRotationDegrees = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 DragStartRotationQuaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT3 DragStartScale = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 DragPlaneNormal = { 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT3 DragStartIntersection = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 DragStartAxisDirection = { 1.0f, 0.0f, 0.0f };
            float DragStartAxisDistance = 0.0f;
        };

        struct FEditorSharedState
        {
            FEditorSpawnSettings SpawnSettings = {};
            FSceneFileSystemState SceneFileSystemState = {};
            Graphics::FDebugRenderSettings DebugRenderSettings = {};
            FConsoleState ConsoleState = {};
            FEditorViewportState ViewportState = {};
        };

        FEditorContext BuildContext();
        void BuildPanelRegistry();
        void RefreshFrameSnapshot();
        void DrawDockSpaceHost();
        void BuildDefaultDockLayout(unsigned int DockSpaceId);
        void DrawViewportWindow(const FEditorContext& Context);
        void DrawViewportToolbar(const FEditorContext& Context);
        void DrawViewportStatsOverlay(const FEditorContext& Context);
        void DrawGizmoOverlay(const FEditorContext& Context);
        EGizmoAxis HitTestGizmo(const FEditorContext& Context, int MouseX, int MouseY) const;
        bool BeginGizmoDrag(const FEditorContext& Context, EGizmoAxis Axis, int MouseX, int MouseY);
        void UpdateGizmoDrag(const FEditorContext& Context, int MouseX, int MouseY);
        void EndGizmoDrag();

    private:
        FEditorModuleDependencies Dependencies = {};
        FEditorSharedState SharedState = {};
        FEditorFrameData FrameData = {};
        FGizmoInteractionState GizmoState = {};

        std::array<IEditorPanel*, PANEL_COUNT> Panels = {};
        SPerformanceOverlay PerformanceOverlay;
        SControlPanel ControlPanel;
        SSceneFileSystemPanel SceneFileSystemPanel;
        SSceneManagerPanel SceneManagerPanel;
        SPropertyWindowPanel PropertyWindowPanel;
        SConsolePanel ConsolePanel;

        /** App 으로부터 Update 받을 데이터 */
        Graphics::FCameraState* CameraState = nullptr;

        bool bIsInitialized = false;
        bool bDockLayoutInitialized = false;
    };
}
