#include <Core/App.h>
#include <Core/PlatformTime.h>
#include <Graphics/Renderer.h>

#include <Scene/SceneTypes.h>
#include <Math/Frustum.h>
#include <Scene/AssetLoader.h>
#include <Scene/SceneData.h>
#include <Scene/SceneManager.h>
#include <Math/MathTypes.h>
#include <Scene/SceneSerializer.h>
#include <UI/EditorLayer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <windowsx.h>

namespace Core
{
    FFramePerformanceMetrics GPerformanceMetrics;
}

namespace
{
    constexpr float MAX_PITCH_RADIANS = 1.55334306f;

    DirectX::XMVECTOR BuildCameraForward(const Graphics::FCameraState& InCameraState)
    {
        const float CosPitch = std::cos(InCameraState.PitchRadians);
        const float SinPitch = std::sin(InCameraState.PitchRadians);
        const float CosYaw = std::cos(InCameraState.YawRadians);
        const float SinYaw = std::sin(InCameraState.YawRadians);

        return DirectX::XMVector3Normalize(DirectX::XMVectorSet(
            CosPitch * CosYaw,
            CosPitch * SinYaw,
            SinPitch,
            0.0f));
    }

    Math::FMatrix BuildCameraViewProjection(const Graphics::FCameraState& InCameraState, int InViewportWidth, int InViewportHeight)
    {
        const float AspectRatio = (InViewportHeight == 0) ? 1.0f : static_cast<float>(InViewportWidth) / static_cast<float>(InViewportHeight);
        const DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&InCameraState.Position);
        const DirectX::XMVECTOR Forward = BuildCameraForward(InCameraState);
        const DirectX::XMVECTOR WorldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        const DirectX::XMVECTOR CameraTarget = DirectX::XMVectorAdd(CameraPosition, Forward);

        const DirectX::XMMATRIX View = DirectX::XMMatrixLookAtLH(CameraPosition, CameraTarget, WorldUp);
        const DirectX::XMMATRIX Projection = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(InCameraState.FOVDegrees),
            AspectRatio,
            InCameraState.NearClip,
            InCameraState.FarClip);
        return View * Projection;
    }

    Math::FMatrix BuildCameraViewMatrix(const Graphics::FCameraState& InCameraState)
    {
        const DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&InCameraState.Position);
        const DirectX::XMVECTOR Forward = BuildCameraForward(InCameraState);
        const DirectX::XMVECTOR WorldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        const DirectX::XMVECTOR CameraTarget = DirectX::XMVectorAdd(CameraPosition, Forward);
        return DirectX::XMMatrixLookAtLH(CameraPosition, CameraTarget, WorldUp);
    }

    Math::FMatrix BuildCameraProjectionMatrix(const Graphics::FCameraState& InCameraState, int InViewportWidth, int InViewportHeight)
    {
        const float AspectRatio = (InViewportHeight == 0) ? 1.0f : static_cast<float>(InViewportWidth) / static_cast<float>(InViewportHeight);
        return DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(InCameraState.FOVDegrees),
            AspectRatio,
            InCameraState.NearClip,
            InCameraState.FarClip);
    }

    bool ComputeSceneBounds(const Scene::USceneManager& InSceneManager, Math::FBox& OutBounds)
    {
        const Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        const uint32_t Count = InSceneManager.GetObjectCount();
        if (!SceneData || Count == 0) return false;

        float MinX = (std::numeric_limits<float>::max)();
        float MinY = (std::numeric_limits<float>::max)();
        float MinZ = (std::numeric_limits<float>::max)();
        float MaxX = std::numeric_limits<float>::lowest();
        float MaxY = std::numeric_limits<float>::lowest();
        float MaxZ = std::numeric_limits<float>::lowest();

        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            MinX = (std::min)(MinX, SceneData->MinX[Index]);
            MinY = (std::min)(MinY, SceneData->MinY[Index]);
            MinZ = (std::min)(MinZ, SceneData->MinZ[Index]);
            MaxX = (std::max)(MaxX, SceneData->MaxX[Index]);
            MaxY = (std::max)(MaxY, SceneData->MaxY[Index]);
            MaxZ = (std::max)(MaxZ, SceneData->MaxZ[Index]);
        }

        OutBounds.Min = { MinX, MinY, MinZ };
        OutBounds.Max = { MaxX, MaxY, MaxZ };
        return true;
    }

    void FitCameraToScene(const Scene::USceneManager& InSceneManager, Graphics::FCameraState& InOutCameraState, int InViewportWidth, int InViewportHeight)
    {
        Math::FBox SceneBounds = {};
        if (!ComputeSceneBounds(InSceneManager, SceneBounds)) return;

        const DirectX::XMFLOAT3 Center = {
            (SceneBounds.Min.x + SceneBounds.Max.x) * 0.5f,
            (SceneBounds.Min.y + SceneBounds.Max.y) * 0.5f,
            (SceneBounds.Min.z + SceneBounds.Max.z) * 0.5f
        };

        const DirectX::XMFLOAT3 Extents = {
            (SceneBounds.Max.x - SceneBounds.Min.x) * 0.5f,
            (SceneBounds.Max.y - SceneBounds.Min.y) * 0.5f,
            (SceneBounds.Max.z - SceneBounds.Min.z) * 0.5f
        };

        const float BoundingRadius = std::sqrt(
            (Extents.x * Extents.x) +
            (Extents.y * Extents.y) +
            (Extents.z * Extents.z));

        const float AspectRatio = (InViewportHeight == 0) ? 1.0f : static_cast<float>(InViewportWidth) / static_cast<float>(InViewportHeight);
        const float VerticalHalfFov = DirectX::XMConvertToRadians(InOutCameraState.FOVDegrees) * 0.5f;
        const float HorizontalHalfFov = std::atan(std::tan(VerticalHalfFov) * AspectRatio);
        const float LimitingHalfFov = (std::min)(VerticalHalfFov, HorizontalHalfFov);
        const float CameraDistance = (BoundingRadius / std::sin(LimitingHalfFov)) + 12.0f;

        const DirectX::XMVECTOR Forward = DirectX::XMVector3Normalize(DirectX::XMVectorSet(1.0f, 1.0f, -0.5f, 0.0f));
        const DirectX::XMVECTOR CenterVector = DirectX::XMLoadFloat3(&Center);
        const DirectX::XMVECTOR CameraPosition = DirectX::XMVectorSubtract(CenterVector, DirectX::XMVectorScale(Forward, CameraDistance));

        DirectX::XMStoreFloat3(&InOutCameraState.Position, CameraPosition);
        InOutCameraState.YawRadians = std::atan2(DirectX::XMVectorGetY(Forward), DirectX::XMVectorGetX(Forward));
        InOutCameraState.PitchRadians = std::atan2(
            DirectX::XMVectorGetZ(Forward),
            std::sqrt(
                (DirectX::XMVectorGetX(Forward) * DirectX::XMVectorGetX(Forward)) +
                (DirectX::XMVectorGetY(Forward) * DirectX::XMVectorGetY(Forward))));
        InOutCameraState.FarClip = (std::max)(InOutCameraState.FarClip, CameraDistance + BoundingRadius + 16.0f);
        InOutCameraState.NearClip = 0.1f;
    }

    Math::FRay CalculatePickingRay(const Graphics::FCameraState& Camera, int MouseX, int MouseY, int ScreenWidth, int ScreenHeight)
    {
        float ptX = (2.0f * static_cast<float>(MouseX)) / static_cast<float>(ScreenWidth) - 1.0f;
        float ptY = 1.0f - (2.0f * static_cast<float>(MouseY)) / static_cast<float>(ScreenHeight);

        Math::FMatrix view = BuildCameraViewMatrix(Camera);
        Math::FMatrix proj = BuildCameraProjectionMatrix(Camera, ScreenWidth, ScreenHeight);
        DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, view * proj);

        DirectX::XMVECTOR nearPtVec = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ptX, ptY, 0.0f, 1.0f), invViewProj);
        DirectX::XMVECTOR farPtVec = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ptX, ptY, 1.0f, 1.0f), invViewProj);
        DirectX::XMVECTOR dirVec = DirectX::XMVectorSubtract(farPtVec, nearPtVec);

        DirectX::XMFLOAT3 origin, direction;
        DirectX::XMStoreFloat3(&origin, nearPtVec);
        DirectX::XMStoreFloat3(&direction, dirVec);

        return Math::FRay(origin, direction);
    }
}

UApp::UApp()
    : WindowHandle(nullptr)
    , InstanceHandle(nullptr)
    , ScreenWidth(0)
    , ScreenHeight(0)
{
    Renderer = std::make_unique<Graphics::URenderer>();
    SceneManager = std::make_unique<Scene::USceneManager>();
    EditorLayer = std::make_unique<UI::UEditorLayer>();
}

UApp::~UApp()
{
    if (EditorLayer) EditorLayer->Cleanup();
    if (bIsCOMInitialized)
    {
        ::CoUninitialize();
    }
}

bool UApp::Initialize(HINSTANCE InHInstance, int InCmdShow)
{
    InstanceHandle = InHInstance;

    const HRESULT COMResult = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(COMResult) || COMResult == S_FALSE)
    {
        bIsCOMInitialized = true;
    }
    else if (COMResult != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    WNDCLASSEXW WindowClass = { sizeof(WNDCLASSEXW), CS_CLASSDC, UApp::WindowProc, 0L, 0L, InstanceHandle, nullptr, nullptr, nullptr, nullptr, L"VerstappenEngineClass", nullptr };
    ::RegisterClassExW(&WindowClass);

    // 1920x1080 표준 창 모드 설정
    const DWORD WindowStyle = WS_OVERLAPPEDWINDOW;
    ScreenWidth = 1920;
    ScreenHeight = 1080;
    RECT WindowRect = { 0, 0, ScreenWidth, ScreenHeight };
    ::AdjustWindowRect(&WindowRect, WindowStyle, FALSE);

    const int ActualWidth = WindowRect.right - WindowRect.left;
    const int ActualHeight = WindowRect.bottom - WindowRect.top;

    WindowHandle = ::CreateWindowW(WindowClass.lpszClassName, AppName.c_str(), WindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, ActualWidth, ActualHeight, nullptr, nullptr, InstanceHandle, this);

    if (!WindowHandle) return false;

    ::SetWindowLongPtrW(WindowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ::ShowWindow(WindowHandle, InCmdShow);
    ::UpdateWindow(WindowHandle);

    if (!Renderer->Initialize(WindowHandle, ScreenWidth, ScreenHeight)) return false;

    SceneManager->Initialize();
    SceneManager->SetBoundsLookup(
        [&](uint32_t MeshID, Math::FBox& OutAABB, DirectX::XMFLOAT3& OutCenter, float& OutRadius)
        {
            const Graphics::URenderer::FMeshResource* MeshRes = Renderer->GetMeshResource(MeshID);
            if (MeshRes)
            {
                OutAABB = MeshRes->LocalAABB;
                OutCenter = MeshRes->LocalCenter;
                OutRadius = MeshRes->LocalRadius;
            }
        });
    SceneManager->SetMeshLoader(
        [&](const std::wstring& Path)
        {
            return Renderer->GetOrLoadMesh(Path);
        });

    if (!Scene::FAssetLoader::LoadDefaultScene(*SceneManager, &CameraState) &&
        !Scene::FSceneSerializer::LoadWorldMatrices(*SceneManager))
    {
        Scene::FSceneGridSpawnRequest GridReq;
        GridReq.Width = 50;
        GridReq.Height = 50;
        GridReq.Depth = 20;
        GridReq.Spacing = 1.0f;
        GridReq.MeshID = 0;
        GridReq.MaterialID = 0;

        // 실제 메쉬 데이터로부터 바운딩 정보 가져오기
        if (const auto* MeshRes = Renderer->GetMeshResource(GridReq.MeshID))
        {
            GridReq.LocalAABB = MeshRes->LocalAABB;
            GridReq.LocalRadius = MeshRes->LocalRadius;
        }

        SceneManager->SpawnStaticMeshGrid(GridReq);
    }
    SceneManager->RebuildCentersAndRadii();
    Core::GPerformanceMetrics.CurrentStructure = SceneManager->DetermineOptimalStructure();
    Core::GPerformanceMetrics.CurrentStructure = Core::ESpatialStructure::UniformGrid;

    // FitCameraToScene(*SceneManager, CameraState, ScreenWidth, ScreenHeight);
    Renderer->SetCameraState(CameraState);

    UI::FEditorModuleDependencies EditorDeps;
    EditorDeps.WindowHandle = WindowHandle;
    EditorDeps.Renderer = Renderer.get();
    EditorDeps.CameraState = &CameraState;
    EditorDeps.SceneManager = SceneManager.get();
    EditorDeps.Device = Renderer->GetDevice();
    EditorDeps.DeviceContext = Renderer->GetContext();

    if (!EditorLayer->Initialize(EditorDeps)) return false;

    return true;
}

int UApp::Run()
{
    MSG Message = {};
    auto LastTime = std::chrono::high_resolution_clock::now();

    while (Message.message != WM_QUIT)
    {
        if (::PeekMessageW(&Message, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&Message);
            ::DispatchMessageW(&Message);
            continue;
        }

        const auto CurrentTime = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<float> FrameDelta = CurrentTime - LastTime;
        DeltaTime = FrameDelta.count();
        LastTime = CurrentTime;

        Update(DeltaTime);
        Render(DeltaTime);
    }

    return static_cast<int>(Message.wParam);
}

void UApp::Update(float InDeltaTime)
{
    UpdateCamera(InDeltaTime);

    UniformCullingAndRenderCollect();

    UpdateFramePerformanceMetrics(InDeltaTime);

    Picking();
    SceneManager->Update(InDeltaTime);

    EditorLayer->SetCameraState(&CameraState);
    EditorLayer->Update(InDeltaTime);
}

void UApp::UniformCullingAndRenderCollect()
{
    if (SceneManager)
    {
        const int ActiveViewportWidth = (EditorLayer && EditorLayer->GetViewportWidth() > 0) ? EditorLayer->GetViewportWidth() : ScreenWidth;
        const int ActiveViewportHeight = (EditorLayer && EditorLayer->GetViewportHeight() > 0) ? EditorLayer->GetViewportHeight() : ScreenHeight;
        Math::FMatrix View = BuildCameraViewMatrix(CameraState);
        Math::FMatrix Proj = BuildCameraProjectionMatrix(CameraState, ActiveViewportWidth, ActiveViewportHeight);

        Math::FFrustum CameraFrustum;
        CameraFrustum.Update(View, Proj);

        /*Scene::FSceneDataSOA* SceneData = SceneManager->GetSceneData();
        SceneData->ResetRenderQueue();
        SceneData->IsVisible.fill(false);

        if (Core::GPerformanceMetrics.CurrentStructure == Core::ESpatialStructure::UniformGrid)
        {
            SceneManager->GetGrid()->QueryFrustum(
                CameraFrustum,
                SceneData->RenderQueue.data(),
                SceneData->RenderCount,
                Scene::FSceneDataSOA::MAX_OBJECTS
            );
        }
        else
        {
            SceneManager->GetSceneBVH()->QueryFrustum(
                CameraFrustum,
                SceneData->RenderQueue.data(),
                SceneData->RenderCount,
                Scene::FSceneDataSOA::MAX_OBJECTS
            );
        }

        for (uint32_t QueueIndex = 0; QueueIndex < SceneData->RenderCount; ++QueueIndex)
        {
            SceneData->IsVisible[SceneData->RenderQueue[QueueIndex]] = true;
        }*/
        Scene::FLODSelectionContext LODContext = {};
        LODContext.CameraPosition = CameraState.Position;
        LODContext.ViewportHeight = static_cast<float>((std::max)(ActiveViewportHeight, 1));
        LODContext.TanHalfFovY = std::tan(DirectX::XMConvertToRadians(CameraState.FOVDegrees) * 0.5f);

        // 최적화된 컬링 및 LOD 빌드 함수 호출
        SceneManager->GetGrid()->CullingAndBuildRenderQueue(CameraFrustum, LODContext);
    }
}

bool UApp::CheckHit(const DirectX::XMFLOAT3& cameraPosition, const Math::FRay& pickRay, uint32_t& OutHitIndex)
{
    float HitDistance = 1000.0f;
    bool bAnyHit = false;

    // [최적화] SceneData 포인터를 람다 정의 시점에 한 번만 가져온다.
    // 이전에는 narrow phase 호출마다 GetSceneData()를 반복 호출했다.
    const Scene::FSceneDataSOA* SceneData = SceneManager->GetSceneData();

    auto PreciseTriangleTest = [&](uint32_t ObjIndex, float& OutHitDistance) -> bool
        {
            uint32_t MeshID = SceneData->MeshIDs[ObjIndex];

            const auto* MeshRes = Renderer->GetMeshResource(MeshID);
            if (!MeshRes || MeshRes->SourceIndices.empty()) return false;

            const auto& PMat = SceneData->WorldMatrices[ObjIndex];
            DirectX::XMMATRIX WorldMat = DirectX::XMMatrixSet(DirectX::XMVectorGetX(PMat.Row0), DirectX::XMVectorGetX(PMat.Row1),
                                     DirectX::XMVectorGetX(PMat.Row2), 0.0f, DirectX::XMVectorGetY(PMat.Row0),
                                     DirectX::XMVectorGetY(PMat.Row1), DirectX::XMVectorGetY(PMat.Row2), 0.0f,
                                     DirectX::XMVectorGetZ(PMat.Row0), DirectX::XMVectorGetZ(PMat.Row1),
                                     DirectX::XMVectorGetZ(PMat.Row2), 0.0f, DirectX::XMVectorGetW(PMat.Row0),
                                     DirectX::XMVectorGetW(PMat.Row1), DirectX::XMVectorGetW(PMat.Row2), 1.0f);

            DirectX::XMVECTOR Det;
            DirectX::XMMATRIX InvWorld = DirectX::XMMatrixInverse(&Det, WorldMat);

            DirectX::XMVECTOR LocalOrigin = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&pickRay.Origin), InvWorld);
            DirectX::XMVECTOR LocalDir = DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&pickRay.Direction), InvWorld);
            LocalDir = DirectX::XMVector3Normalize(LocalDir);

            DirectX::XMFLOAT3 RayOrigin, RayDir;
            DirectX::XMStoreFloat3(&RayOrigin, LocalOrigin);
            DirectX::XMStoreFloat3(&RayDir, LocalDir);
            Math::FRay LocalRay(RayOrigin, RayDir);

            bool bHit = false;
            float ClosestWorldT = OutHitDistance;

            float LocalT = 0.0f;
            if (MeshRes->Raycast(LocalRay, LocalT))
            {
                DirectX::XMVECTOR LocalIntersection = DirectX::XMVectorAdd(LocalOrigin, DirectX::XMVectorScale(LocalDir, LocalT));
                DirectX::XMVECTOR WorldIntersection = DirectX::XMVector3TransformCoord(LocalIntersection, WorldMat);

                DirectX::XMVECTOR DistVec = DirectX::XMVectorSubtract(WorldIntersection, DirectX::XMLoadFloat3(&pickRay.Origin));
                float WorldT = DirectX::XMVectorGetX(DirectX::XMVector3Length(DistVec));

                if (WorldT < ClosestWorldT)
                {
                    ClosestWorldT = WorldT;
                    bHit = true;
                }
            }

            if (bHit) OutHitDistance = ClosestWorldT;
            return bHit;
        };

    uint32_t StaticHitIndex = 0;

    float StaticHitDistance = HitDistance;
    bool bStaticHit = false;

    if (Core::GPerformanceMetrics.CurrentStructure == Core::ESpatialStructure::UniformGrid)
    {
        bStaticHit =
            SceneManager->GetGrid()->Raycast(pickRay, 1000.0f, StaticHitIndex, StaticHitDistance, PreciseTriangleTest);
    }
    else
    {
        bStaticHit = SceneManager->GetSceneBVH()->Raycast(pickRay, 1000.0f, StaticHitIndex, StaticHitDistance,
                                                          PreciseTriangleTest);
    }
    if (bStaticHit && StaticHitDistance < HitDistance)
    {
        HitDistance = StaticHitDistance;
        OutHitIndex = StaticHitIndex;
        bAnyHit = true;
    }

    return bAnyHit;
}

void UApp::Picking()
{
    if (bPendingPick && SceneManager)
    {
        if (!EditorLayer || !EditorLayer->IsPointInsideViewport(PickPosition.x, PickPosition.y))
        {
            bPendingPick = false;
            return;
        }

        if (Core::GPerformanceMetrics.CurrentStructure == Core::ESpatialStructure::UniformGrid)
        {
            Core::GPerformanceMetrics.GridCellTestCount = 0;
            Core::GPerformanceMetrics.GridObjectAABBTestCount = 0;
        }
        else
        {
            Core::GPerformanceMetrics.BVHNodeTestCount = 0;
            Core::GPerformanceMetrics.ObjectAABBTestCount = 0;
        }

        // 1) 마우스 화면 좌표 획득
        const int ViewportWidth = (EditorLayer->GetViewportWidth() > 0) ? EditorLayer->GetViewportWidth() : ScreenWidth;
        const int ViewportHeight = (EditorLayer->GetViewportHeight() > 0) ? EditorLayer->GetViewportHeight() : ScreenHeight;
        const int ViewportOriginX = EditorLayer->GetViewportX();
        const int ViewportOriginY = EditorLayer->GetViewportY();
        const int LocalPickX = PickPosition.x - ViewportOriginX;
        const int LocalPickY = PickPosition.y - ViewportOriginY;

        // 2) 화면 좌표 -> 월드 좌표로의 픽 레이(Pick Ray) 계산
        Math::FRay pickRay = CalculatePickingRay(CameraState, LocalPickX, LocalPickY, ViewportWidth, ViewportHeight);

        // 3) 전체 Picking 횟수 누적
        ++Core::GPerformanceMetrics.TotalPickCount;

        // 4) 모든 오브젝트(프리미티브)에 대해 충돌 판정
        uint32_t HitIndex = 0;

        // 5) 퍼포먼스 측정 — 순수 raycast 시간만 측정한다.
        //    SelectObject / ClearSelection 은 picking 로직이 아니므로 범위 밖으로 이동.
        const uint64_t PickStart = Core::FPlatformTime::Cycles64();
        bool isHit = CheckHit(CameraState.Position, pickRay, HitIndex);
        uint64_t LastPickTime = Core::FPlatformTime::Cycles64() - PickStart;

        // 필요 시 'isHit' 결과를 활용해 추가 로직 처리
        const bool bAdditiveSelect = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        if (isHit)
        {
            if (bAdditiveSelect)
            {
                SceneManager->ToggleObjectSelection(HitIndex);
            }
            else
            {
                SceneManager->SelectObject(HitIndex);
            }
        }
        else if (!bAdditiveSelect)
        {
            SceneManager->ClearSelection();
        }

        // 6) 퍼포먼스 기록
        Core::GPerformanceMetrics.LastPickingCycles = LastPickTime;
        Core::GPerformanceMetrics.TotalPickingCycles += LastPickTime;

        if (Core::GPerformanceMetrics.CurrentStructure == Core::ESpatialStructure::UniformGrid)
        {
            Core::GPerformanceMetrics.GridLastPickingCycles = LastPickTime;
            Core::GPerformanceMetrics.GridTotalPickingCycles += LastPickTime;
            Core::GPerformanceMetrics.GridTotalPickCount++;
        }
        else
        {
            Core::GPerformanceMetrics.BVHLastPickingCycles = LastPickTime;
            Core::GPerformanceMetrics.BVHTotalPickingCycles += LastPickTime;
            Core::GPerformanceMetrics.BVHTotalPickCount++;
        }

        bPendingPick = false;
    }
}

void UApp::Render(float DeltaTime)
{
    Renderer->UpdatePerformanceMetrics(Core::GPerformanceMetrics); // 추가: 성능 데이터 전달
    Renderer->SetCameraState(CameraState);
    Renderer->SetSceneViewportRect(0, 0, ScreenWidth, ScreenHeight);
    if (EditorLayer->GetViewportWidth() > 0 && EditorLayer->GetViewportHeight() > 0)
    {
        Renderer->SetSceneViewportRect(
            EditorLayer->GetViewportX(),
            EditorLayer->GetViewportY(),
            EditorLayer->GetViewportWidth(),
            EditorLayer->GetViewportHeight());
    }
    Renderer->BeginFrame();
    Renderer->RenderScene(*SceneManager, DeltaTime);
    Renderer->BindMainRenderTarget();
    EditorLayer->SetFramePerformanceMetrics(Core::GPerformanceMetrics);
    EditorLayer->BeginFrame();
    EditorLayer->DrawPanels();
    EditorLayer->EndFrame();
    Renderer->EndFrame();
}

void UApp::UpdateCamera(float InDeltaTime)
{
    if (EditorLayer && EditorLayer->WantsKeyboardCapture() && !bIsRightMouseLooking)
    {
        return;
    }

    const DirectX::XMVECTOR Forward = BuildCameraForward(CameraState);
    const DirectX::XMVECTOR WorldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const DirectX::XMVECTOR Right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(WorldUp, Forward));
    DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&CameraState.Position);

    const float MoveDistance = CameraState.MoveSpeed * InDeltaTime;

    if (bIsRightMouseLooking)
    {
        if ((::GetAsyncKeyState('W') & 0x8000) != 0)
            Position = DirectX::XMVectorAdd(Position, DirectX::XMVectorScale(Forward, MoveDistance));
        if ((::GetAsyncKeyState('S') & 0x8000) != 0)
            Position = DirectX::XMVectorSubtract(Position, DirectX::XMVectorScale(Forward, MoveDistance));
        if ((::GetAsyncKeyState('A') & 0x8000) != 0)
            Position = DirectX::XMVectorSubtract(Position, DirectX::XMVectorScale(Right, MoveDistance));
        if ((::GetAsyncKeyState('D') & 0x8000) != 0)
            Position = DirectX::XMVectorAdd(Position, DirectX::XMVectorScale(Right, MoveDistance));
    }

    if (PendingWheelDelta != 0.0f)
    {
        Position =
            DirectX::XMVectorAdd(Position, DirectX::XMVectorScale(Forward, PendingWheelDelta * CameraState.WheelSpeed));
        PendingWheelDelta = 0.0f;
    }

    DirectX::XMStoreFloat3(&CameraState.Position, Position);
}

void UApp::UpdateFramePerformanceMetrics(float InDeltaTime)
{
    Core::GPerformanceMetrics.DeltaTimeSeconds = InDeltaTime;
    
    const float RawFPS = (InDeltaTime > 0.0f) ? (1.0f / InDeltaTime) : 0.0f;
    
    // 지수 이동 평균(EMA) 필터 적용 (알파 = 0.05)
    // 숫자가 너무 빠르게 변하지 않도록 부드럽게 보정합니다.
    if (Core::GPerformanceMetrics.FramesPerSecond < 1.0f)
    {
        Core::GPerformanceMetrics.FramesPerSecond = RawFPS;
    }
    else
    {
        constexpr float Alpha = 0.05f;
        Core::GPerformanceMetrics.FramesPerSecond = (RawFPS * Alpha) + (Core::GPerformanceMetrics.FramesPerSecond * (1.0f - Alpha));
    }

    ++Core::GPerformanceMetrics.FrameIndex;

    static float FPSAccumulator = 0.0f;
    static uint64_t FPSFrameCount = 0;

    FPSAccumulator += InDeltaTime;
    ++FPSFrameCount;

    // 0.5초마다 평균 FPS 업데이트 (HUD 표시용)
    if (FPSAccumulator >= 0.5f)
    {
        Core::GPerformanceMetrics.AverageFPS = static_cast<float>(FPSFrameCount) / FPSAccumulator;
        FPSAccumulator = 0.0f;
        FPSFrameCount = 0;
    }

    if (!WindowHandle || !SceneManager)
    {
        return;
    }

    const uint32_t TotalObjects = SceneManager->GetObjectCount();
    const uint32_t VisibleObjects = SceneManager->GetVisibleObjectCount();

    // 렌더링 단계별 가공 데이터 업데이트 (HUD 연동)
    Core::GPerformanceMetrics.DrawCount = VisibleObjects;
    Core::GPerformanceMetrics.TotalObjectsCount = TotalObjects;
    Core::GPerformanceMetrics.PrevVisible = VisibleObjects;
    Core::GPerformanceMetrics.PrevInvisible = TotalObjects - VisibleObjects;
}

LRESULT CALLBACK UApp::WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    UApp* AppInstance = reinterpret_cast<UApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (AppInstance != nullptr && AppInstance->EditorLayer != nullptr)
    {
        if (AppInstance->EditorLayer->HandleWindowMessage(hWnd, Message, wParam, lParam)) return 1;
    }

    switch (Message)
    {
    case WM_SIZE:
        if (AppInstance != nullptr && wParam != SIZE_MINIMIZED)
        {
            AppInstance->ScreenWidth = LOWORD(lParam);
            AppInstance->ScreenHeight = HIWORD(lParam);
            if (AppInstance->Renderer)
            {
                AppInstance->Renderer->Resize(AppInstance->ScreenWidth, AppInstance->ScreenHeight);
            }
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (AppInstance != nullptr)
        {
            if (!AppInstance->EditorLayer || !AppInstance->EditorLayer->IsPointInsideViewport(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                return 0;
            }
            AppInstance->bIsRightMouseLooking = true;
            AppInstance->LastMousePosition.x = GET_X_LPARAM(lParam);
            AppInstance->LastMousePosition.y = GET_Y_LPARAM(lParam);
            ::SetCapture(hWnd);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (AppInstance != nullptr)
        {
            const int MouseX = GET_X_LPARAM(lParam);
            const int MouseY = GET_Y_LPARAM(lParam);
            if (!AppInstance->EditorLayer || !AppInstance->EditorLayer->IsPointInsideViewport(MouseX, MouseY))
            {
                return 0;
            }
            if (AppInstance->EditorLayer->HandleLeftMouseDown(MouseX, MouseY))
            {
                return 0;
            }
            AppInstance->PickStartCycles = Core::FPlatformTime::Cycles64();
            AppInstance->bPendingPick = true;
            AppInstance->PickPosition.x = MouseX;
            AppInstance->PickPosition.y = MouseY;
        }
        return 0;
    case WM_LBUTTONUP:
        if (AppInstance != nullptr && AppInstance->EditorLayer != nullptr)
        {
            AppInstance->EditorLayer->HandleLeftMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_RBUTTONUP:
        if (AppInstance != nullptr)
        {
            AppInstance->bIsRightMouseLooking = false;
            ::ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN:
        if (AppInstance != nullptr)
        {
            if (wParam == VK_SPACE)
            {
                if (AppInstance->EditorLayer &&
                    AppInstance->EditorLayer->IsViewportFocused() &&
                    !AppInstance->EditorLayer->WantsKeyboardCapture())
                {
                    AppInstance->EditorLayer->CycleGizmoMode();
                    return 0;
                }
            }
            else if (wParam == '1')
            {
                Core::GPerformanceMetrics.CurrentStructure = Core::ESpatialStructure::UniformGrid;
            }
            else if (wParam == '2')
            {
                Core::GPerformanceMetrics.CurrentStructure = Core::ESpatialStructure::SceneBVH;
            }
            else if (wParam == 'R')
            {
                Core::GPerformanceMetrics.LastPickingCycles = 0;
                Core::GPerformanceMetrics.TotalPickingCycles = 0;
                Core::GPerformanceMetrics.TotalPickCount = 0;

                Core::GPerformanceMetrics.GridLastPickingCycles = 0;
                Core::GPerformanceMetrics.GridTotalPickingCycles = 0;
                Core::GPerformanceMetrics.GridTotalPickCount = 0;

                Core::GPerformanceMetrics.BVHLastPickingCycles = 0;
                Core::GPerformanceMetrics.BVHTotalPickingCycles = 0;
                Core::GPerformanceMetrics.BVHTotalPickCount = 0;

                Core::GPerformanceMetrics.FrameIndex = 0;

                Core::GPerformanceMetrics.BVHNodeTestCount = 0;
                Core::GPerformanceMetrics.ObjectAABBTestCount = 0;

                Core::GPerformanceMetrics.GridCellTestCount = 0;
                Core::GPerformanceMetrics.GridObjectAABBTestCount = 0;
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (AppInstance != nullptr && AppInstance->EditorLayer != nullptr)
        {
            AppInstance->EditorLayer->HandleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        if (AppInstance != nullptr && AppInstance->bIsRightMouseLooking)
        {
            const POINT CurrentMousePosition = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int DeltaX = CurrentMousePosition.x - AppInstance->LastMousePosition.x;
            const int DeltaY = CurrentMousePosition.y - AppInstance->LastMousePosition.y;

            AppInstance->CameraState.YawRadians += static_cast<float>(DeltaX) * AppInstance->CameraState.LookSensitivity;
            AppInstance->CameraState.PitchRadians = (std::clamp)(
                AppInstance->CameraState.PitchRadians - static_cast<float>(DeltaY) * AppInstance->CameraState.LookSensitivity,
                -MAX_PITCH_RADIANS,
                MAX_PITCH_RADIANS);
            AppInstance->LastMousePosition = CurrentMousePosition;
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (AppInstance != nullptr)
        {
            POINT ClientPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ::ScreenToClient(hWnd, &ClientPoint);
            if (AppInstance->EditorLayer && AppInstance->EditorLayer->IsPointInsideViewport(ClientPoint.x, ClientPoint.y))
            {
                AppInstance->PendingWheelDelta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            }
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hWnd, Message, wParam, lParam);
}
