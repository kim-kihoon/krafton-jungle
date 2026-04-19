#pragma once
#include <Core/AppTypes.h>
#include <Graphics/RendererTypes.h>
#include <windows.h>
#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Graphics { class URenderer; }
namespace Scene { class USceneManager; }

namespace UI
{
    /**
     * 에디터 패널 종류를 구분하는 열거형.
     */
    enum class EEditorPanelType : uint8_t
    {
        Overlay = 0,
        ControlPanel,
        SceneFileSystem,
        SceneManager,
        PropertyWindow,
        Console,
        Count
    };

    /**
     * 콘솔 메시지 심각도 구분용 열거형.
     */
    enum class EConsoleMessageSeverity : uint8_t
    {
        Info = 0,
        Warning,
        Error
    };

    /**
     * Control Panel이 발행하는 Spawn 관련 요청 상태.
     */
    struct FEditorSpawnSettings
    {
        uint32_t DefaultMeshID = 0;
        uint32_t DefaultMaterialID = 0;
        uint32_t SingleSpawnCount = 1;
        uint32_t GridWidth = 50;
        uint32_t GridHeight = 50;
        uint32_t GridDepth = 20;
        float GridSpacing = 100.0f;
        bool bPendingSingleSpawn = false;
        bool bPendingGridSpawn = false;
    };

    /**
     * Scene File System 패널이 사용하는 파일 경로 상태.
     */
    struct FSceneFileSystemState
    {
        static constexpr size_t PATH_BUFFER_LENGTH = 260;

        std::array<wchar_t, PATH_BUFFER_LENGTH> SaveFilePath = {};
        std::array<wchar_t, PATH_BUFFER_LENGTH> LoadFilePath = {};
        bool bPendingResetScene = false;
        bool bPendingSave = false;
        bool bPendingLoad = false;
    };

    /**
     * 단일 콘솔 메시지 저장 구조체.
     */
    struct FConsoleMessage
    {
        EConsoleMessageSeverity Severity = EConsoleMessageSeverity::Info;
        std::array<char, 128> Text = {};
    };

    /**
     * 고정 크기 콘솔 메시지 버퍼.
     */
    struct FConsoleState
    {
        static constexpr uint32_t MAX_MESSAGES = 64;

        std::array<FConsoleMessage, MAX_MESSAGES> Messages = {};
        uint32_t MessageCount = 0;
        uint32_t NextWriteIndex = 0;
        bool bAutoScroll = true;

        void PushMessage(const char* InText, EConsoleMessageSeverity InSeverity)
        {
            FConsoleMessage& Message = Messages[NextWriteIndex];
            Message.Severity = InSeverity;
            std::snprintf(Message.Text.data(), Message.Text.size(), "%s", InText);

            NextWriteIndex = (NextWriteIndex + 1) % MAX_MESSAGES;
            if (MessageCount < MAX_MESSAGES)
            {
                ++MessageCount;
            }
        }

        void ClearMessages()
        {
            Messages = {};
            MessageCount = 0;
            NextWriteIndex = 0;
        }
    };

    struct FEditorViewportRect
    {
        int X = 0;
        int Y = 0;
        int Width = 0;
        int Height = 0;

        bool IsValid() const
        {
            return Width > 0 && Height > 0;
        }
    };

    enum class EGizmoMode : uint8_t
    {
        Translate = 0,
        Rotate,
        Scale
    };

    enum class EGizmoCoordinateSpace : uint8_t
    {
        World = 0,
        Local
    };

    struct FEditorViewportState
    {
        FEditorViewportRect Rect = {};
        bool bHovered = false;
        bool bFocused = false;
        bool bShowStats = false;
        bool bShowFPS = false;
        EGizmoMode GizmoMode = EGizmoMode::Translate;
        EGizmoCoordinateSpace GizmoSpace = EGizmoCoordinateSpace::World;
    };

    /**
     * 프레임마다 패널이 참조하는 읽기 전용 스냅샷 데이터.
     */
    struct FEditorFrameData
    {
        Core::FFramePerformanceMetrics PerformanceMetrics = {};
        uint32_t TotalObjectCount = 0;
        uint32_t VisibleObjectCount = 0;
        bool bHasSelection = false;
        uint32_t SelectedObjectCount = 0;
        uint32_t SelectedObjectIndex = 0;
        uint32_t SelectedMeshID = 0;
        uint32_t SelectedMaterialID = 0;
    };

    /**
     * 에디터 모듈 초기화에 필요한 외부 시스템 포인터 집합.
     */
    struct FEditorModuleDependencies
    {
        HWND WindowHandle = nullptr;
        Graphics::URenderer* Renderer = nullptr;
        Graphics::FCameraState* CameraState = nullptr;
        Scene::USceneManager* SceneManager = nullptr;
        ID3D11Device* Device = nullptr;
        ID3D11DeviceContext* DeviceContext = nullptr;
    };

    /**
     * 모든 패널이 공통으로 참조하는 컨텍스트.
     */
    struct FEditorContext
    {
        const FEditorModuleDependencies* Dependencies = nullptr;
        FEditorSpawnSettings* SpawnSettings = nullptr;
        FSceneFileSystemState* SceneFileSystemState = nullptr;
        Graphics::FDebugRenderSettings* DebugRenderSettings = nullptr;
        FConsoleState* ConsoleState = nullptr;
        FEditorViewportState* ViewportState = nullptr;
        FEditorFrameData* FrameData = nullptr;
        Graphics::FCameraState* CameraState = nullptr;
    };
}
