#include <UI/EditorLayer.h>

#include <Core/PathManager.h>
#include <Core/PlatformTime.h>
#include <Graphics/Renderer.h>
#include <Scene/SceneManager.h>
#include <UI/EditorSceneEditing.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cwchar>

#ifdef WITH_IMGUI
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace UI
{
namespace
{
constexpr float GIZMO_AXIS_LENGTH_PIXELS = 90.0f;
constexpr float GIZMO_ROTATE_RADIUS_PIXELS = 64.0f;
constexpr float GIZMO_PICK_DISTANCE_PIXELS = 10.0f;
constexpr float GIZMO_MIN_SCALE = 0.01f;

DirectX::XMVECTOR LoadFloat3(const DirectX::XMFLOAT3& InValue)
{
    return DirectX::XMLoadFloat3(&InValue);
}

DirectX::XMFLOAT3 StoreFloat3(const DirectX::XMVECTOR& InValue)
{
    DirectX::XMFLOAT3 Result = {};
    DirectX::XMStoreFloat3(&Result, InValue);
    return Result;
}

DirectX::XMVECTOR NormalizeSafe(const DirectX::XMVECTOR& InValue, const DirectX::XMVECTOR& InFallback)
{
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(InValue)) <= 1.0e-8f)
    {
        return InFallback;
    }
    return DirectX::XMVector3Normalize(InValue);
}

DirectX::XMVECTOR GetWorldAxis(UEditorLayer::EGizmoAxis Axis)
{
    switch (Axis)
    {
    case UEditorLayer::EGizmoAxis::X:
        return DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    case UEditorLayer::EGizmoAxis::Y:
        return DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    case UEditorLayer::EGizmoAxis::Z:
        return DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    default:
        return DirectX::XMVectorZero();
    }
}

DirectX::XMVECTOR GetCameraForward(const Graphics::FCameraState& InCameraState)
{
    const float CosPitch = std::cos(InCameraState.PitchRadians);
    const float SinPitch = std::sin(InCameraState.PitchRadians);
    const float CosYaw = std::cos(InCameraState.YawRadians);
    const float SinYaw = std::sin(InCameraState.YawRadians);
    return DirectX::XMVector3Normalize(DirectX::XMVectorSet(CosPitch * CosYaw, CosPitch * SinYaw, SinPitch, 0.0f));
}

DirectX::XMVECTOR GetCameraUp(const Graphics::FCameraState& InCameraState)
{
    const DirectX::XMVECTOR Forward = GetCameraForward(InCameraState);
    const DirectX::XMVECTOR WorldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const DirectX::XMVECTOR Right =
        NormalizeSafe(DirectX::XMVector3Cross(WorldUp, Forward), DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    return NormalizeSafe(DirectX::XMVector3Cross(Forward, Right), WorldUp);
}

ImU32 GetAxisColor(UEditorLayer::EGizmoAxis Axis, bool bHighlighted)
{
    switch (Axis)
    {
    case UEditorLayer::EGizmoAxis::X:
        return bHighlighted ? IM_COL32(255, 220, 96, 255) : IM_COL32(255, 80, 80, 255);
    case UEditorLayer::EGizmoAxis::Y:
        return bHighlighted ? IM_COL32(255, 220, 96, 255) : IM_COL32(96, 220, 96, 255);
    case UEditorLayer::EGizmoAxis::Z:
        return bHighlighted ? IM_COL32(255, 220, 96, 255) : IM_COL32(96, 160, 255, 255);
    default:
        return IM_COL32(220, 220, 220, 255);
    }
}

float DistancePointToSegmentSquared(const ImVec2& Point, const ImVec2& SegmentStart, const ImVec2& SegmentEnd)
{
    const float Dx = SegmentEnd.x - SegmentStart.x;
    const float Dy = SegmentEnd.y - SegmentStart.y;
    const float LengthSq = Dx * Dx + Dy * Dy;
    if (LengthSq <= 1.0e-6f)
    {
        const float DeltaX = Point.x - SegmentStart.x;
        const float DeltaY = Point.y - SegmentStart.y;
        return DeltaX * DeltaX + DeltaY * DeltaY;
    }

    const float T =
        (std::clamp)(((Point.x - SegmentStart.x) * Dx + (Point.y - SegmentStart.y) * Dy) / LengthSq, 0.0f, 1.0f);
    const float ClosestX = SegmentStart.x + Dx * T;
    const float ClosestY = SegmentStart.y + Dy * T;
    const float DeltaX = Point.x - ClosestX;
    const float DeltaY = Point.y - ClosestY;
    return DeltaX * DeltaX + DeltaY * DeltaY;
}

float DistancePointToPolylineSquared(const ImVec2& Point, const ImVector<ImVec2>& Polyline)
{
    float BestDistance = FLT_MAX;
    for (int Index = 0; Index + 1 < Polyline.Size; ++Index)
    {
        BestDistance =
            (std::min)(BestDistance, DistancePointToSegmentSquared(Point, Polyline[Index], Polyline[Index + 1]));
    }
    return BestDistance;
}

bool IntersectRayWithPlane(const Math::FRay& InRay, const DirectX::XMVECTOR& InPlanePoint,
                           const DirectX::XMVECTOR& InPlaneNormal, DirectX::XMVECTOR& OutIntersection)
{
    const DirectX::XMVECTOR RayOrigin = DirectX::XMLoadFloat3(&InRay.Origin);
    const DirectX::XMVECTOR RayDirection = DirectX::XMLoadFloat3(&InRay.Direction);
    const float Denominator = DirectX::XMVectorGetX(DirectX::XMVector3Dot(InPlaneNormal, RayDirection));
    if (std::abs(Denominator) <= 1.0e-6f)
    {
        return false;
    }

    const DirectX::XMVECTOR PlaneOffset = DirectX::XMVectorSubtract(InPlanePoint, RayOrigin);
    const float T = DirectX::XMVectorGetX(DirectX::XMVector3Dot(PlaneOffset, InPlaneNormal)) / Denominator;
    if (T < 0.0f)
    {
        return false;
    }

    OutIntersection = DirectX::XMVectorAdd(RayOrigin, DirectX::XMVectorScale(RayDirection, T));
    return true;
}

// [최적화] InViewProjection 을 호출자(DrawGizmoOverlay / HitTestGizmo)에서
// 딱 한 번 계산한 뒤 전달받아 사용한다.
// 이전 버전은 WorldToScreen 내부에서 VP 행렬을 매 점(65점 × 3축)마다
// 재계산했기 때문에 프레임당 최대 390회의 불필요한 행렬 빌드가 발생했다.
void BuildRotationCircle(const DirectX::XMVECTOR& InOrigin, const DirectX::XMVECTOR& InAxis, float InRadius,
                         const Graphics::FCameraState& InCameraState, const DirectX::XMMATRIX& InViewProjection,
                         const FEditorViewportRect& InViewportRect, ImVector<ImVec2>& OutPoints)
{
    OutPoints.clear();

    DirectX::XMVECTOR BasisU = DirectX::XMVector3Cross(InAxis, DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(BasisU)) <= 1.0e-6f)
    {
        BasisU = DirectX::XMVector3Cross(InAxis, GetCameraForward(InCameraState));
    }
    BasisU = NormalizeSafe(BasisU, DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    const DirectX::XMVECTOR BasisV =
        NormalizeSafe(DirectX::XMVector3Cross(InAxis, BasisU), DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

    for (int SegmentIndex = 0; SegmentIndex <= 64; ++SegmentIndex)
    {
        const float Angle = DirectX::XM_2PI * static_cast<float>(SegmentIndex) / 64.0f;
        const DirectX::XMVECTOR CircleOffset = DirectX::XMVectorAdd(DirectX::XMVectorScale(BasisU, std::cos(Angle)),
                                                                    DirectX::XMVectorScale(BasisV, std::sin(Angle)));
        const DirectX::XMVECTOR WorldPoint =
            DirectX::XMVectorAdd(InOrigin, DirectX::XMVectorScale(CircleOffset, InRadius));
        DirectX::XMFLOAT2 ScreenPoint = {};
        if (WorldToScreen(WorldPoint, InViewProjection, InViewportRect, ScreenPoint))
        {
            OutPoints.push_back(ImVec2(ScreenPoint.x, ScreenPoint.y));
        }
    }
}
} // namespace

UEditorLayer::UEditorLayer()
{
    BuildPanelRegistry();
}

UEditorLayer::~UEditorLayer()
{
    Cleanup();
}

bool UEditorLayer::Initialize(const FEditorModuleDependencies& InDependencies)
{
    Dependencies = InDependencies;

#ifdef WITH_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // 🏎️ Verstappen Engine Custom Style (Red Bull Racing Theme)
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.11f, 0.96f); // Deep Navy
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.05f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f); // Victory Red

    colors[ImGuiCol_Header] = ImVec4(0.86f, 0.08f, 0.24f, 0.45f); // Red accents
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.86f, 0.08f, 0.24f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.15f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.86f, 0.08f, 0.24f, 0.85f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.84f, 0.00f, 1.00f); // Racing Yellow on click

    colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.84f, 0.00f, 1.00f); // Yellow point
    colors[ImGuiCol_SliderGrab] = ImVec4(0.86f, 0.08f, 0.24f, 0.80f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.84f, 0.00f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);

    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.86f, 0.08f, 0.24f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.86f, 0.08f, 0.24f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.86f, 0.08f, 0.24f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.86f, 0.08f, 0.24f, 0.95f);

    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.PopupRounding = 2.0f;

    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.IniFilename = "imgui.ini";
    if (!ImGui_ImplWin32_Init(Dependencies.WindowHandle))
        return false;
    if (!ImGui_ImplDX11_Init(Dependencies.Device, Dependencies.DeviceContext))
        return false;
    bIsInitialized = true;
#endif

    if (Dependencies.Renderer)
    {
        SharedState.DebugRenderSettings = Dependencies.Renderer->GetDebugRenderSettings();
    }

    const std::wstring DefaultPath = L"Data/Scenes/Default.scene";
    wcscpy_s(SharedState.SceneFileSystemState.SaveFilePath.data(), SharedState.SceneFileSystemState.SaveFilePath.size(),
             DefaultPath.c_str());
    wcscpy_s(SharedState.SceneFileSystemState.LoadFilePath.data(), SharedState.SceneFileSystemState.LoadFilePath.size(),
             DefaultPath.c_str());

    FEditorContext Context = BuildContext();
    for (IEditorPanel* Panel : Panels)
    {
        if (Panel && !Panel->Initialize(Context))
        {
            return false;
        }
    }

    return true;
}

void UEditorLayer::Update(float DeltaTime)
{
    RefreshFrameSnapshot();
    FEditorContext Context = BuildContext();
    for (IEditorPanel* Panel : Panels)
    {
        if (Panel)
        {
            Panel->Update(Context, DeltaTime);
        }
    }
}

void UEditorLayer::BeginFrame()
{
#ifdef WITH_IMGUI
    if (!bIsInitialized)
    {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void UEditorLayer::DrawPanels()
{
#ifdef WITH_IMGUI
    if (!bIsInitialized)
    {
        return;
    }

    FEditorContext Context = BuildContext();
    DrawDockSpaceHost();
    DrawViewportWindow(Context);
    ControlPanel.Draw(Context);
    SceneFileSystemPanel.Draw(Context);
    SceneManagerPanel.Draw(Context);
    PropertyWindowPanel.Draw(Context);
    ConsolePanel.Draw(Context);
    PerformanceOverlay.Draw(Context);
#endif
}

void UEditorLayer::EndFrame()
{
#ifdef WITH_IMGUI
    if (!bIsInitialized)
    {
        return;
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
}

void UEditorLayer::Cleanup()
{
#ifdef WITH_IMGUI
    if (bIsInitialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        bIsInitialized = false;
    }
#endif
}

void UEditorLayer::SetFramePerformanceMetrics(const Core::FFramePerformanceMetrics& InMetrics)
{
    FrameData.PerformanceMetrics = InMetrics;
}

bool UEditorLayer::HandleWindowMessage(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
#ifdef WITH_IMGUI
    if (bIsInitialized)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam);
    }
#endif
    return false;
}

bool UEditorLayer::WantsKeyboardCapture() const
{
#ifdef WITH_IMGUI
    if (bIsInitialized && ImGui::GetCurrentContext())
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }
#endif
    return false;
}

bool UEditorLayer::IsPointInsideViewport(int X, int Y) const
{
    const FEditorViewportRect& Rect = SharedState.ViewportState.Rect;
    return Rect.IsValid() && X >= Rect.X && Y >= Rect.Y && X < Rect.X + Rect.Width && Y < Rect.Y + Rect.Height;
}

bool UEditorLayer::IsViewportFocused() const
{
    return SharedState.ViewportState.bFocused;
}

bool UEditorLayer::HandleLeftMouseDown(int X, int Y)
{
    if (!IsPointInsideViewport(X, Y))
    {
        return false;
    }

    FEditorContext Context = BuildContext();
    const EGizmoAxis Axis = HitTestGizmo(Context, X, Y);
    if (Axis == EGizmoAxis::None)
    {
        return false;
    }

    return BeginGizmoDrag(Context, Axis, X, Y);
}

void UEditorLayer::HandleLeftMouseUp(int X, int Y)
{
    GizmoState.MouseX = X;
    GizmoState.MouseY = Y;
    EndGizmoDrag();
}

void UEditorLayer::HandleMouseMove(int X, int Y)
{
    GizmoState.MouseX = X;
    GizmoState.MouseY = Y;
    FEditorContext Context = BuildContext();
    if (GizmoState.bIsDragging)
    {
        UpdateGizmoDrag(Context, X, Y);
    }
    else
    {
        GizmoState.HoveredAxis = HitTestGizmo(Context, X, Y);
    }
}

void UEditorLayer::CycleGizmoMode()
{
    switch (SharedState.ViewportState.GizmoMode)
    {
    case EGizmoMode::Translate:
        SharedState.ViewportState.GizmoMode = EGizmoMode::Rotate;
        break;
    case EGizmoMode::Rotate:
        SharedState.ViewportState.GizmoMode = EGizmoMode::Scale;
        break;
    case EGizmoMode::Scale:
    default:
        SharedState.ViewportState.GizmoMode = EGizmoMode::Translate;
        break;
    }
}

int UEditorLayer::GetViewportX() const
{
    return SharedState.ViewportState.Rect.X;
}

int UEditorLayer::GetViewportY() const
{
    return SharedState.ViewportState.Rect.Y;
}

int UEditorLayer::GetViewportWidth() const
{
    return SharedState.ViewportState.Rect.Width;
}

int UEditorLayer::GetViewportHeight() const
{
    return SharedState.ViewportState.Rect.Height;
}

FEditorContext UEditorLayer::BuildContext()
{
    FEditorContext Context;
    Context.Dependencies = &Dependencies;
    Context.SpawnSettings = &SharedState.SpawnSettings;
    Context.SceneFileSystemState = &SharedState.SceneFileSystemState;
    Context.DebugRenderSettings = &SharedState.DebugRenderSettings;
    Context.ConsoleState = &SharedState.ConsoleState;
    Context.ViewportState = &SharedState.ViewportState;
    Context.FrameData = &FrameData;
    Context.CameraState = CameraState;
    return Context;
}

void UEditorLayer::BuildPanelRegistry()
{
    Panels[static_cast<size_t>(EEditorPanelType::Overlay)] = &PerformanceOverlay;
    Panels[static_cast<size_t>(EEditorPanelType::ControlPanel)] = &ControlPanel;
    Panels[static_cast<size_t>(EEditorPanelType::SceneFileSystem)] = &SceneFileSystemPanel;
    Panels[static_cast<size_t>(EEditorPanelType::SceneManager)] = &SceneManagerPanel;
    Panels[static_cast<size_t>(EEditorPanelType::PropertyWindow)] = &PropertyWindowPanel;
    Panels[static_cast<size_t>(EEditorPanelType::Console)] = &ConsolePanel;
}

void UEditorLayer::RefreshFrameSnapshot()
{
    if (!Dependencies.SceneManager)
    {
        return;
    }

    const Scene::FSceneStatistics Statistics = Dependencies.SceneManager->GetSceneStatistics();
    const Scene::FSceneSelectionData& SelectionData = Dependencies.SceneManager->GetSelectionData();
    FrameData.TotalObjectCount = Statistics.TotalObjectCount;
    FrameData.VisibleObjectCount = Statistics.VisibleObjectCount;
    FrameData.bHasSelection = SelectionData.bHasSelection;
    FrameData.SelectedObjectCount = SelectionData.SelectionCount;
    FrameData.SelectedObjectIndex = SelectionData.ObjectIndex;
    FrameData.SelectedMeshID = SelectionData.MeshID;
    FrameData.SelectedMaterialID = SelectionData.MaterialID;
}

void UEditorLayer::DrawDockSpaceHost()
{
#ifdef WITH_IMGUI
    ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    const ImGuiID DockSpaceId = ImGui::DockSpaceOverViewport(0, MainViewport, ImGuiDockNodeFlags_PassthruCentralNode);
    if (!bDockLayoutInitialized)
    {
        BuildDefaultDockLayout(DockSpaceId);
        bDockLayoutInitialized = true;
    }
#endif
}

void UEditorLayer::BuildDefaultDockLayout(unsigned int DockSpaceId)
{
#ifdef WITH_IMGUI
    ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockSpaceId);
    ImGui::DockBuilderAddNode(DockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockSpaceId, MainViewport->WorkSize);

    ImGuiID BottomNode = 0;
    ImGuiID MainNode = DockSpaceId;
    ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Down, 0.24f, &BottomNode, &MainNode);

    ImGuiID LeftNode = 0;
    ImGuiID CenterNode = 0;
    ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Left, 0.18f, &LeftNode, &CenterNode);

    ImGuiID RightNode = 0;
    ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Right, 0.24f, &RightNode, &CenterNode);

    ImGuiID RightTopNode = 0;
    ImGuiID RightBottomNode = 0;
    ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Up, 0.32f, &RightTopNode, &RightBottomNode);

    ImGuiID RightMiddleNode = 0;
    ImGuiID RightPropertiesNode = 0;
    ImGui::DockBuilderSplitNode(RightBottomNode, ImGuiDir_Down, 0.38f, &RightPropertiesNode, &RightMiddleNode);

    ImGui::DockBuilderDockWindow("Viewport", CenterNode);
    ImGui::DockBuilderDockWindow("Control Panel", LeftNode);
    ImGui::DockBuilderDockWindow("Scene Files", RightTopNode);
    ImGui::DockBuilderDockWindow("Scene Outliner", RightMiddleNode);
    ImGui::DockBuilderDockWindow("Properties", RightPropertiesNode);
    ImGui::DockBuilderDockWindow("Console", BottomNode);
    ImGui::DockBuilderFinish(DockSpaceId);
#endif
}

void UEditorLayer::DrawViewportWindow(const FEditorContext& Context)
{
#ifdef WITH_IMGUI
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags ViewportFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavFocus;

    if (ImGui::Begin("Viewport", nullptr, ViewportFlags))
    {
        DrawViewportToolbar(Context);
        const ImVec2 AvailableSize = ImGui::GetContentRegionAvail();
        const ImVec2 ImageMin = ImGui::GetCursorScreenPos();
        if (Context.Dependencies && Context.Dependencies->Renderer &&
            Context.Dependencies->Renderer->GetSceneViewportSRV())
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(Context.Dependencies->Renderer->GetSceneViewportSRV()),
                         AvailableSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        }
        else
        {
            ImGui::Dummy(AvailableSize);
        }

        ImGui::SetCursorScreenPos(ImageMin);
        ImGui::InvisibleButton("##ViewportSurface", AvailableSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        const ImVec2 ItemMin = ImageMin;
        const ImVec2 ItemMax = ImVec2(ImageMin.x + AvailableSize.x, ImageMin.y + AvailableSize.y);
        ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        SharedState.ViewportState.Rect.X = static_cast<int>(ItemMin.x - MainViewport->Pos.x);
        SharedState.ViewportState.Rect.Y = static_cast<int>(ItemMin.y - MainViewport->Pos.y);
        SharedState.ViewportState.Rect.Width = static_cast<int>(ItemMax.x - ItemMin.x);
        SharedState.ViewportState.Rect.Height = static_cast<int>(ItemMax.y - ItemMin.y);
        SharedState.ViewportState.bHovered = ImGui::IsItemHovered();
        SharedState.ViewportState.bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawViewportStatsOverlay(Context);
        DrawGizmoOverlay(Context);
    }
    else
    {
        SharedState.ViewportState.Rect = {};
        SharedState.ViewportState.bHovered = false;
        SharedState.ViewportState.bFocused = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
#endif
}

void UEditorLayer::DrawViewportStatsOverlay(const FEditorContext& Context)
{
#ifdef WITH_IMGUI
    if (!Context.FrameData || !Context.ViewportState || !Context.ViewportState->Rect.IsValid())
    {
        return;
    }

    const Core::FFramePerformanceMetrics& Metrics = Context.FrameData->PerformanceMetrics;
    const double LastPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.LastPickingCycles);
    const double TotalPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.TotalPickingCycles);
    const float DisplayFPS = Metrics.AverageFPS > 0.0f ? Metrics.AverageFPS : Metrics.FramesPerSecond;
    const float FrameMS = DisplayFPS > 0.0f ? (1000.0f / DisplayFPS) : 0.0f;
    const FEditorViewportRect& Rect = Context.ViewportState->Rect;

    char FpsBuffer[96] = {};
    char PickingBuffer[160] = {};
    std::snprintf(FpsBuffer, sizeof(FpsBuffer), "FPS : %.0f (%.0f ms)", DisplayFPS, FrameMS);
    std::snprintf(PickingBuffer, sizeof(PickingBuffer),
                  "Picking Time %.4f ms : Num Attempts %llu : Accumulated Time %.4fms", LastPickingMS,
                  static_cast<unsigned long long>(Metrics.TotalPickCount), TotalPickingMS);

    if (Context.ViewportState->bShowFPS)
    {
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImVec2 Origin(static_cast<float>(Rect.X) + 12.0f, static_cast<float>(Rect.Y) + 10.0f);
        const ImU32 ShadowColor = IM_COL32(0, 0, 0, 220);
        const ImU32 TextColor = IM_COL32(24, 255, 24, 255);

        DrawList->PushClipRect(
            ImVec2(static_cast<float>(Rect.X), static_cast<float>(Rect.Y)),
            ImVec2(static_cast<float>(Rect.X + Rect.Width), static_cast<float>(Rect.Y + Rect.Height)), true);
        DrawList->AddText(ImVec2(Origin.x + 2.0f, Origin.y + 2.0f), ShadowColor, FpsBuffer);
        DrawList->AddText(Origin, TextColor, FpsBuffer);
        DrawList->AddText(ImVec2(Origin.x + 2.0f, Origin.y + 36.0f), ShadowColor, PickingBuffer);
        DrawList->AddText(ImVec2(Origin.x, Origin.y + 34.0f), TextColor, PickingBuffer);
        DrawList->PopClipRect();
    }
#endif
}

void UEditorLayer::DrawViewportToolbar(const FEditorContext& Context)
{
#ifdef WITH_IMGUI
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    if (ImGui::BeginChild("##ViewportToolbar", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() + 10.0f),
                          ImGuiChildFlags_Borders))
    {
        ImGui::TextUnformatted("Perspective");
        if (Context.ViewportState)
        {
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            const bool bTranslate = Context.ViewportState->GizmoMode == EGizmoMode::Translate;
            const bool bRotate = Context.ViewportState->GizmoMode == EGizmoMode::Rotate;
            const bool bScale = Context.ViewportState->GizmoMode == EGizmoMode::Scale;

            if (ImGui::Selectable("Translate", bTranslate, 0, ImVec2(72.0f, 0.0f)))
            {
                Context.ViewportState->GizmoMode = EGizmoMode::Translate;
            }
            ImGui::SameLine();
            if (ImGui::Selectable("Rotate", bRotate, 0, ImVec2(72.0f, 0.0f)))
            {
                Context.ViewportState->GizmoMode = EGizmoMode::Rotate;
            }
            ImGui::SameLine();
            if (ImGui::Selectable("Scale", bScale, 0, ImVec2(72.0f, 0.0f)))
            {
                Context.ViewportState->GizmoMode = EGizmoMode::Scale;
            }
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            const bool bWorld = Context.ViewportState->GizmoSpace == EGizmoCoordinateSpace::World;
            if (ImGui::Selectable(bWorld ? "World" : "Local", true, 0, ImVec2(72.0f, 0.0f)))
            {
                Context.ViewportState->GizmoSpace =
                    bWorld ? EGizmoCoordinateSpace::Local : EGizmoCoordinateSpace::World;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
#endif
}

void UEditorLayer::DrawGizmoOverlay(const FEditorContext& Context)
{
    if (!Context.Dependencies || !Context.Dependencies->Renderer || !Context.Dependencies->SceneManager ||
        !Context.FrameData || !Context.FrameData->bHasSelection)
    {
        return;
    }

    const FEditorViewportRect& Rect = SharedState.ViewportState.Rect;
    if (!Rect.IsValid())
    {
        return;
    }

    DirectX::XMFLOAT3 Translation = {};
    DirectX::XMFLOAT3 Scale = {};
    DirectX::XMVECTOR RotationQuaternion = {};
    if (!GetObjectTransform(*Context.Dependencies->SceneManager, Context.FrameData->SelectedObjectIndex, Translation,
                            RotationQuaternion, Scale))
    {
        return;
    }

    const Graphics::FCameraState& CameraState = Context.Dependencies->Renderer->GetCameraState();
    const DirectX::XMVECTOR Origin = DirectX::XMVectorSet(Translation.x, Translation.y, Translation.z, 1.0f);

    // [최적화] VP 행렬을 여기서 딱 한 번 계산한다.
    // 이전에는 WorldToScreen / BuildRotationCircle 내부에서 호출마다 재계산했기 때문에
    // Rotate 모드 기준 (1 + 65×3) = 196회의 중복 VP 빌드가 발생했다.
    const DirectX::XMMATRIX VP =
        BuildCameraViewMatrix(CameraState) * BuildCameraProjectionMatrix(CameraState, Rect.Width, Rect.Height);

    DirectX::XMFLOAT2 OriginScreen = {};
    if (!WorldToScreen(Origin, VP, Rect, OriginScreen))
    {
        return;
    }

    const float UnitsPerPixel = ComputeWorldUnitsPerPixel(Origin, CameraState, Rect.Height);
    const float AxisLengthWorld = UnitsPerPixel * GIZMO_AXIS_LENGTH_PIXELS;
    const float RotationRadiusWorld = UnitsPerPixel * GIZMO_ROTATE_RADIUS_PIXELS;

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(ImVec2(static_cast<float>(Rect.X), static_cast<float>(Rect.Y)),
                           ImVec2(static_cast<float>(Rect.X + Rect.Width), static_cast<float>(Rect.Y + Rect.Height)),
                           true);

    for (EGizmoAxis Axis : {EGizmoAxis::X, EGizmoAxis::Y, EGizmoAxis::Z})
    {
        DirectX::XMVECTOR AxisDirection = GetWorldAxis(Axis);
        const bool bIsScaleMode = (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Scale);
        const bool bIsLocalSpace =
            (Context.ViewportState && Context.ViewportState->GizmoSpace == EGizmoCoordinateSpace::Local);

        if (bIsScaleMode || bIsLocalSpace)
        {
            AxisDirection = DirectX::XMVector3Rotate(AxisDirection, RotationQuaternion);
        }

        const bool bHighlighted = (GizmoState.ActiveAxis == Axis) || (GizmoState.HoveredAxis == Axis);
        const ImU32 Color = GetAxisColor(Axis, bHighlighted);

        if (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Rotate)
        {
            ImVector<ImVec2> CirclePoints;
            BuildRotationCircle(Origin, AxisDirection, RotationRadiusWorld, CameraState, VP, Rect, CirclePoints);
            if (CirclePoints.Size >= 2)
            {
                DrawList->AddPolyline(CirclePoints.Data, CirclePoints.Size, Color, 0, 2.0f);
            }
        }
        else
        {
            const DirectX::XMVECTOR AxisEnd =
                DirectX::XMVectorAdd(Origin, DirectX::XMVectorScale(AxisDirection, AxisLengthWorld));
            DirectX::XMFLOAT2 AxisEndScreen = {};
            if (!WorldToScreen(AxisEnd, VP, Rect, AxisEndScreen))
            {
                continue;
            }

            DrawList->AddLine(ImVec2(OriginScreen.x, OriginScreen.y), ImVec2(AxisEndScreen.x, AxisEndScreen.y), Color,
                              3.0f);
            if (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Scale)
            {
                DrawList->AddRectFilled(ImVec2(AxisEndScreen.x - 5.0f, AxisEndScreen.y - 5.0f),
                                        ImVec2(AxisEndScreen.x + 5.0f, AxisEndScreen.y + 5.0f), Color, 2.0f);
            }
            else
            {
                DrawList->AddCircleFilled(ImVec2(AxisEndScreen.x, AxisEndScreen.y), 4.0f, Color);
            }
        }
    }

    DrawList->AddCircleFilled(ImVec2(OriginScreen.x, OriginScreen.y), 4.0f, IM_COL32(235, 235, 235, 255));
    DrawList->PopClipRect();
}

UEditorLayer::EGizmoAxis UEditorLayer::HitTestGizmo(const FEditorContext& Context, int MouseX, int MouseY) const
{
    if (!Context.Dependencies || !Context.Dependencies->Renderer || !Context.Dependencies->SceneManager ||
        !Context.FrameData || !Context.FrameData->bHasSelection)
    {
        return EGizmoAxis::None;
    }
    if (!IsPointInsideViewport(MouseX, MouseY))
    {
        return EGizmoAxis::None;
    }

    DirectX::XMFLOAT3 Translation = {};
    DirectX::XMFLOAT3 Scale = {};
    DirectX::XMVECTOR RotationQuaternion = {};
    if (!GetObjectTransform(*Context.Dependencies->SceneManager, Context.FrameData->SelectedObjectIndex, Translation,
                            RotationQuaternion, Scale))
    {
        return EGizmoAxis::None;
    }

    const FEditorViewportRect& Rect = SharedState.ViewportState.Rect;
    const Graphics::FCameraState& CameraState = Context.Dependencies->Renderer->GetCameraState();
    const DirectX::XMVECTOR Origin = DirectX::XMVectorSet(Translation.x, Translation.y, Translation.z, 1.0f);

    // [최적화] VP 행렬을 여기서 딱 한 번 계산한다.
    // 이전에는 WorldToScreen / BuildRotationCircle 내부에서 호출마다 재계산했기 때문에
    // Rotate 모드 기준 (1 + 65×3) = 196회의 중복 VP 빌드가 발생했다.
    const DirectX::XMMATRIX VP =
        BuildCameraViewMatrix(CameraState) * BuildCameraProjectionMatrix(CameraState, Rect.Width, Rect.Height);

    DirectX::XMFLOAT2 OriginScreen = {};
    if (!WorldToScreen(Origin, VP, Rect, OriginScreen))
    {
        return EGizmoAxis::None;
    }

    const float UnitsPerPixel = ComputeWorldUnitsPerPixel(Origin, CameraState, Rect.Height);
    const float AxisLengthWorld = UnitsPerPixel * GIZMO_AXIS_LENGTH_PIXELS;
    const float RotationRadiusWorld = UnitsPerPixel * GIZMO_ROTATE_RADIUS_PIXELS;
    const ImVec2 MousePoint(static_cast<float>(MouseX), static_cast<float>(MouseY));

    EGizmoAxis BestAxis = EGizmoAxis::None;
    float BestDistanceSq = GIZMO_PICK_DISTANCE_PIXELS * GIZMO_PICK_DISTANCE_PIXELS;
    for (EGizmoAxis Axis : {EGizmoAxis::X, EGizmoAxis::Y, EGizmoAxis::Z})
    {
        DirectX::XMVECTOR AxisDirection = GetWorldAxis(Axis);
        const bool bIsScaleMode = (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Scale);
        const bool bIsLocalSpace =
            (Context.ViewportState && Context.ViewportState->GizmoSpace == EGizmoCoordinateSpace::Local);

        if (bIsScaleMode || bIsLocalSpace)
        {
            AxisDirection = DirectX::XMVector3Rotate(AxisDirection, RotationQuaternion);
        }

        float DistanceSq = FLT_MAX;
        if (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Rotate)
        {
            ImVector<ImVec2> CirclePoints;
            BuildRotationCircle(Origin, AxisDirection, RotationRadiusWorld, CameraState, VP, Rect, CirclePoints);
            DistanceSq = DistancePointToPolylineSquared(MousePoint, CirclePoints);
        }
        else
        {
            const DirectX::XMVECTOR AxisEnd =
                DirectX::XMVectorAdd(Origin, DirectX::XMVectorScale(AxisDirection, AxisLengthWorld));
            DirectX::XMFLOAT2 AxisEndScreen = {};
            if (!WorldToScreen(AxisEnd, VP, Rect, AxisEndScreen))
            {
                continue;
            }

            DistanceSq = DistancePointToSegmentSquared(MousePoint, ImVec2(OriginScreen.x, OriginScreen.y),
                                                       ImVec2(AxisEndScreen.x, AxisEndScreen.y));
        }

        if (DistanceSq < BestDistanceSq)
        {
            BestDistanceSq = DistanceSq;
            BestAxis = Axis;
        }
    }

    return BestAxis;
}

bool UEditorLayer::BeginGizmoDrag(const FEditorContext& Context, EGizmoAxis Axis, int MouseX, int MouseY)
{
    if (Axis == EGizmoAxis::None || !Context.Dependencies || !Context.Dependencies->Renderer ||
        !Context.Dependencies->SceneManager || !Context.FrameData)
    {
        return false;
    }

    DirectX::XMFLOAT3 Translation = {};
    DirectX::XMFLOAT3 Scale = {};
    DirectX::XMVECTOR RotationQuaternion = {};
    if (!GetObjectTransform(*Context.Dependencies->SceneManager, Context.FrameData->SelectedObjectIndex, Translation,
                            RotationQuaternion, Scale))
    {
        return false;
    }

    const Graphics::FCameraState& CameraState = Context.Dependencies->Renderer->GetCameraState();
    const FEditorViewportRect& Rect = SharedState.ViewportState.Rect;
    const Math::FRay Ray = ScreenToRay(CameraState, MouseX - Rect.X, MouseY - Rect.Y, Rect.Width, Rect.Height);
    const DirectX::XMVECTOR Origin = DirectX::XMVectorSet(Translation.x, Translation.y, Translation.z, 1.0f);

    DirectX::XMVECTOR AxisDirection = GetWorldAxis(Axis);
    const bool bIsScaleMode = (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Scale);
    const bool bIsLocalSpace =
        (Context.ViewportState && Context.ViewportState->GizmoSpace == EGizmoCoordinateSpace::Local);

    if (bIsScaleMode || bIsLocalSpace)
    {
        AxisDirection = DirectX::XMVector3Rotate(AxisDirection, RotationQuaternion);
    }
    AxisDirection = NormalizeSafe(AxisDirection, GetWorldAxis(Axis));

    DirectX::XMVECTOR PlaneNormal = AxisDirection;
    if (Context.ViewportState && Context.ViewportState->GizmoMode != EGizmoMode::Rotate)
    {
        DirectX::XMVECTOR PlaneTangent = DirectX::XMVector3Cross(GetCameraForward(CameraState), AxisDirection);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(PlaneTangent)) <= 1.0e-6f)
        {
            PlaneTangent = DirectX::XMVector3Cross(GetCameraUp(CameraState), AxisDirection);
        }
        PlaneNormal = NormalizeSafe(DirectX::XMVector3Cross(AxisDirection, PlaneTangent),
                                    DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
    }

    DirectX::XMVECTOR Intersection = {};
    if (!IntersectRayWithPlane(Ray, Origin, PlaneNormal, Intersection))
    {
        return false;
    }

    GizmoState.HoveredAxis = Axis;
    GizmoState.ActiveAxis = Axis;
    GizmoState.bIsDragging = true;
    GizmoState.DragStartTranslation = Translation;
    DirectX::XMStoreFloat4(&GizmoState.DragStartRotationQuaternion, RotationQuaternion);
    GizmoState.DragStartRotationDegrees = QuaternionToEulerDegrees(RotationQuaternion);
    GizmoState.DragStartScale = Scale;
    GizmoState.DragPlaneNormal = StoreFloat3(PlaneNormal);
    GizmoState.DragStartIntersection = StoreFloat3(Intersection);
    GizmoState.DragStartAxisDirection = StoreFloat3(AxisDirection);
    const DirectX::XMVECTOR InitialOffset = DirectX::XMVectorSubtract(Intersection, Origin);
    GizmoState.DragStartAxisDistance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(InitialOffset, AxisDirection));

    return true;
}

void UEditorLayer::UpdateGizmoDrag(const FEditorContext& Context, int MouseX, int MouseY)
{
    if (!GizmoState.bIsDragging || GizmoState.ActiveAxis == EGizmoAxis::None || !Context.Dependencies ||
        !Context.Dependencies->Renderer || !Context.Dependencies->SceneManager || !Context.FrameData)
    {
        return;
    }

    const Graphics::FCameraState& CameraState = Context.Dependencies->Renderer->GetCameraState();
    const FEditorViewportRect& Rect = SharedState.ViewportState.Rect;
    const Math::FRay Ray = ScreenToRay(CameraState, MouseX - Rect.X, MouseY - Rect.Y, Rect.Width, Rect.Height);
    const DirectX::XMVECTOR StartTranslation = LoadFloat3(GizmoState.DragStartTranslation);
    const DirectX::XMVECTOR PlaneNormal = LoadFloat3(GizmoState.DragPlaneNormal);
    const DirectX::XMVECTOR AxisDirection =
        NormalizeSafe(LoadFloat3(GizmoState.DragStartAxisDirection), GetWorldAxis(GizmoState.ActiveAxis));

    DirectX::XMVECTOR Intersection = {};
    if (!IntersectRayWithPlane(Ray, StartTranslation, PlaneNormal, Intersection))
    {
        return;
    }

    DirectX::XMFLOAT3 NewTranslation = GizmoState.DragStartTranslation;
    DirectX::XMFLOAT3 NewScale = GizmoState.DragStartScale;
    DirectX::XMVECTOR NewRotationQuaternion = DirectX::XMLoadFloat4(&GizmoState.DragStartRotationQuaternion);

    if (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Rotate)
    {
        // [Fix] 로컬 좌표계 조작 시, 오브젝트가 회전함에 따라 회전축도 실시간으로 추적해야 함.
        // 하지만 현재 프레임의 Delta 각도를 구하기 위해선 드래그 시작 시점의 평면 투영을 유지하면서도
        // 축의 방향성만은 최신 상태를 유지하거나, 일관된 투영 공간을 사용해야 함.

        // 더 안정적인 방식: 드래그 시작 시점의 축(AxisDirection)을 고수하되,
        // 평면 위에서의 각도 변화량을 더 정밀하게 계산.
        const DirectX::XMVECTOR StartOffset =
            DirectX::XMVectorSubtract(LoadFloat3(GizmoState.DragStartIntersection), StartTranslation);
        const DirectX::XMVECTOR CurrentOffset = DirectX::XMVectorSubtract(Intersection, StartTranslation);

        // 원점 근처에서 각도가 튀는 것을 방지하기 위해 최소 길이 체크
        const float StartLenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(StartOffset));
        const float CurrentLenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(CurrentOffset));

        if (StartLenSq > 1.0e-6f && CurrentLenSq > 1.0e-6f)
        {
            const DirectX::XMVECTOR StartVector = DirectX::XMVector3Normalize(StartOffset);
            const DirectX::XMVECTOR CurrentVector = DirectX::XMVector3Normalize(CurrentOffset);

            const DirectX::XMVECTOR CrossVector = DirectX::XMVector3Cross(StartVector, CurrentVector);
            const float DotProduct =
                std::clamp(DirectX::XMVectorGetX(DirectX::XMVector3Dot(StartVector, CurrentVector)), -1.0f, 1.0f);

            // 축 방향에 따른 부호 결정
            float Angle = std::acos(DotProduct);
            const float Sign = DirectX::XMVectorGetX(DirectX::XMVector3Dot(CrossVector, AxisDirection));
            if (Sign < 0.0f)
                Angle = -Angle;

            const DirectX::XMVECTOR StartRotation = DirectX::XMLoadFloat4(&GizmoState.DragStartRotationQuaternion);

            // [핵심] DeltaRotation은 '드래그 시작 시점의 로컬 축' 또는 '월드 축'을 기준으로 생성됨.
            // 이미 StartRotation에 반영되어 있으므로 AxisDirection(드래그 시작 시점의 결정된 축)을 그대로 사용하는 것이
            // 수학적으로 옳음. 다만 누적 오차를 줄이기 위해 매번 정규화 수행.
            const DirectX::XMVECTOR DeltaRotation = DirectX::XMQuaternionRotationAxis(AxisDirection, Angle);
            NewRotationQuaternion =
                DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(StartRotation, DeltaRotation));
        }
    }
    else
    {
        const DirectX::XMVECTOR TranslationOffset = DirectX::XMVectorSubtract(Intersection, StartTranslation);
        const float AxisDistance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(TranslationOffset, AxisDirection));
        const float DeltaDistance = AxisDistance - GizmoState.DragStartAxisDistance;

        if (Context.ViewportState && Context.ViewportState->GizmoMode == EGizmoMode::Scale)
        {
            const float UnitsPerPixel = ComputeWorldUnitsPerPixel(StartTranslation, CameraState, Rect.Height);
            const float DeltaScale = DeltaDistance / ((std::max)(UnitsPerPixel * 60.0f, 1.0e-3f));
            switch (GizmoState.ActiveAxis)
            {
            case EGizmoAxis::X:
                NewScale.x = (std::max)(GIZMO_MIN_SCALE, GizmoState.DragStartScale.x + DeltaScale);
                break;
            case EGizmoAxis::Y:
                NewScale.y = (std::max)(GIZMO_MIN_SCALE, GizmoState.DragStartScale.y + DeltaScale);
                break;
            case EGizmoAxis::Z:
                NewScale.z = (std::max)(GIZMO_MIN_SCALE, GizmoState.DragStartScale.z + DeltaScale);
                break;
            default:
                break;
            }
        }
        else
        {
            NewTranslation = StoreFloat3(
                DirectX::XMVectorAdd(StartTranslation, DirectX::XMVectorScale(AxisDirection, DeltaDistance)));
        }
    }

    // [최적화] 드래그 중에는 Grid / BVH 전체 재구축을 매 프레임 실행하지 않는다.
    // 재구축은 EndGizmoDrag 에서 드래그 종료 시점에 한 번만 수행한다.
    ApplyObjectTransform(*Context.Dependencies->SceneManager, *Context.Dependencies->Renderer,
                         Context.FrameData->SelectedObjectIndex, NewTranslation, NewRotationQuaternion, NewScale, true);
}

void UEditorLayer::EndGizmoDrag()
{
    // 드래그 중 defer 했던 Grid / BVH 재구축을 여기서 한 번만 수행한다.
    if (GizmoState.bIsDragging && Dependencies.SceneManager)
    {
        Dependencies.SceneManager->RebuildCentersAndRadii();
        if (Scene::UUniformGrid* Grid = Dependencies.SceneManager->GetGrid())
        {
            Grid->BuildGrid();
        }
        Dependencies.SceneManager->BuildSceneBVH();
    }

    GizmoState.HoveredAxis = EGizmoAxis::None;
    GizmoState.ActiveAxis = EGizmoAxis::None;
    GizmoState.bIsDragging = false;
}
} // namespace UI
