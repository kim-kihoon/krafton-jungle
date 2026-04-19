#include "EditorEngine.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "Actor/Actor.h"
#include "Actor/CameraActor.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/ConsoleVariableManager.h"
#include "Core/Engine.h"
#include "Input/InputManager.h"
#include "Debug/EngineLog.h"
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Scene/Level.h"
#include "Viewport/Viewport.h"
#include "Viewport/EditorViewportClient.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Material.h"
#include "Viewport/PreviewViewportClient.h"
#include "World/World.h"
#include "Slate/EditorViewportOverlay.h"
#include "Pawn/EditorCameraPawn.h"

namespace
{
	constexpr const char* PreviewLevelContextName = "PreviewLevel";

	const TArray<FWorldContext*>& GetEmptyPreviewWorldContexts()
	{
		static TArray<FWorldContext*> EmptyPreviewWorldContexts;
		return EmptyPreviewWorldContexts;
	}

	void InitializeDefaultPreviewLevel(FEditorEngine* Engine)
	{
		if (Engine == nullptr)
		{
			return;
		}

		FWorldContext* PreviewContext = Engine->CreatePreviewWorldContext(PreviewLevelContextName, 1280, 720);
		if (PreviewContext == nullptr || PreviewContext->World == nullptr)
		{
			return;
		}

		UWorld* PreviewWorld = PreviewContext->World;
		if (UCameraComponent* PreviewCamera = PreviewWorld->GetActiveCameraComponent())
		{
			PreviewCamera->GetCamera()->SetPosition({ -8.0f, -8.0f, 6.0f });
			PreviewCamera->GetCamera()->SetRotation(45.0f, -20.0f);
			PreviewCamera->SetFov(50.0f);
		}
	}
}

FEditorEngine::~FEditorEngine() = default;

void FEditorEngine::StartPIE()
{
	if (IsPlayingInEditor() || !EditorWorldContext || !EditorWorldContext->World)
	{
		return;
	}

	UE_LOG("[PIE] Play In Editor Started.");

	// 1. 현재 에디터 카메라 정보 먼저 저장 (동기화에 사용)
	FVector EditorCamPos = FVector::ZeroVector;
	float EditorCamYaw = 0.0f;
	float EditorCamPitch = 0.0f;
	if (UCameraComponent* EditorCam = EditorWorldContext->World->GetActiveCameraComponent())
	{
		EditorCamPos = EditorCam->GetCamera()->GetPosition();
		EditorCamYaw = EditorCam->GetCamera()->GetYaw();
		EditorCamPitch = EditorCam->GetCamera()->GetPitch();
	}

	// 2. 에디터 카메라 상태 백업 및 초기 위치 동기화 (점프 방지)
	EditorCameraStatesBackup.clear();
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		EditorCameraStatesBackup[Entry.Id] = Entry.LocalState;
		
		// [수정]: PIE 시작 즉시 모든 퍼스펙티브 뷰포트 위치를 현재 에디터 카메라 위치로 맞춤
		if (Entry.LocalState.ProjectionType == EViewportType::Perspective)
		{
			Entry.LocalState.Position = EditorCamPos;
			Entry.LocalState.Rotation.Yaw = EditorCamYaw;
			Entry.LocalState.Rotation.Pitch = EditorCamPitch;
		}
	}

	// 3. 에디터 폰 좌표 즉시 동기화
	if (AEditorCameraPawn* EditorPawn = CameraSubsystem.GetEditorCameraPawn())
	{
		EditorPawn->SetActorLocation(EditorCamPos);
		if (UCameraComponent* EditorCamComp = EditorPawn->GetCameraComponent())
		{
			EditorCamComp->GetCamera()->SetRotation(EditorCamYaw, EditorCamPitch);
		}
	}

	SetSelectedActor(nullptr);

	FDuplicateionContext Context;
	UWorld* EditorWorld = EditorWorldContext->World;
	UWorld* PIEWorld = static_cast<UWorld*>(EditorWorld->Duplicate(Context, nullptr));
	for (auto& [OldObj, NewObj] : Context.DuplicatedObjects)
	{
		NewObj->FixupReferences(Context);
	}

	float AspectRatio = MainWindow ? (static_cast<float>(MainWindow->GetWidth()) / static_cast<float>(MainWindow->GetHeight())) : 1.0f;
	PIEWorldContext = CreateWorldContext("PIELevel", EWorldType::PIE, AspectRatio, false);
	PIEWorldContext->World = PIEWorld;
	PIEWorld->SetWorldType(EWorldType::PIE);
	
	ActiveEditorWorldContext = PIEWorldContext;
	LockedPIEViewportId = GetPIEViewportId();

	// 4. 카메라 설정 우선순위 (사용자 배치 카메라 우선 탐색)
	bool bPossessedCameraActor = false;
	ACameraActor* BestCamera = nullptr;

	// PIEWorld의 모든 액터를 순회하며 CameraActor를 찾음 (스트리밍 레벨 포함)
	TArray<AActor*> AllPIEActors = PIEWorld->GetAllActors();
	for (AActor* Actor : AllPIEActors)
	{
		if (Actor && Actor->IsA(ACameraActor::StaticClass()))
		{
			ACameraActor* Candidate = static_cast<ACameraActor*>(Actor);
			// "DefaultCameraActor"가 아닌 사용자가 배치한 카메라를 우선순위로 둠
			if (Candidate->GetName().find("Default") == std::string::npos)
			{
				BestCamera = Candidate;
				break; 
			}
			if (!BestCamera) BestCamera = Candidate;
		}
	}

	if (BestCamera)
	{
		if (UCameraComponent* CameraComponent = BestCamera->GetCameraComponent())
		{
			// 액터의 현재 트랜스폼을 카메라 컴포넌트에 반영
			CameraComponent->ApplyComponentTransformToCamera();
			
			// 현재 PIE 뷰포트 종횡비 적용
			if (CameraComponent->GetCamera())
			{
				CameraComponent->GetCamera()->SetAspectRatio(AspectRatio);
			}

			PIEWorld->SetActiveCameraComponent(CameraComponent);
			
			// [추가]: Possess한 카메라의 좌표를 에디터 뷰포트 상태에도 즉시 동기화 (Eject 시 튐 방지)
			FCamera* BestCam = CameraComponent->GetCamera();
			if (BestCam)
			{
				for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
				{
					if (Entry.LocalState.ProjectionType == EViewportType::Perspective)
					{
						Entry.LocalState.Position = BestCam->GetPosition();
						Entry.LocalState.Rotation.Yaw = BestCam->GetYaw();
						Entry.LocalState.Rotation.Pitch = BestCam->GetPitch();
					}
				}
			}

			UE_LOG("[PIE] Camera possession: %s", BestCamera->GetName().c_str());
			bPossessedCameraActor = true;
		}
	}

	if (!bPossessedCameraActor)
	{
		if (UCameraComponent* PIECam = PIEWorld->GetActiveCameraComponent())
		{
			PIECam->GetCamera()->SetPosition(EditorCamPos);
			PIECam->GetCamera()->SetRotation(EditorCamYaw, EditorCamPitch);
		}
	}

	PIEWorld->BeginPlay();
	SyncViewportClient();

	// 시작 즉시 PIE 뷰포트 마우스 캡처 및 중앙 고정
	if (FInputManager* Input = GetInputManager())
	{
		Input->SetMouseCapture(true);
		if (MainWindow)
		{
			const FViewportId PIEId = GetPIEViewportId();
			const FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(PIEId);
			if (Entry && Entry->Viewport)
			{
				const FRect& Rect = Entry->Viewport->GetRect();
				POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
				::ClientToScreen(MainWindow->GetHwnd(), &Center);
				::SetCursorPos(Center.x, Center.y);
			}
		}
	}
}

void FEditorEngine::EndPIE()
{
	if (!IsPlayingInEditor())
	{
		return;
	}

	bRequestEndPIE = false; // 예약 플래그 초기화
	UE_LOG("[PIE] Play In Editor Stopped.");

	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
	if (PIEWorld)
	{
		PIEWorld->SetPaused(false);
	}

	// 1. 입력 및 상태 복구 시작
	if (FInputManager* Input = GetInputManager())
	{
		Input->SetMouseCapture(false);
	}

	// 2. 활성 컨텍스트를 에디터로 먼저 돌려놓음
	ActiveEditorWorldContext = EditorWorldContext;

	// 3. 뷰포트 클라이언트를 에디터용(4분할)으로 즉시 복구 (이 시점에 Detach/Attach 발생)
	// PIEWorldContext가 아직 살아있으므로 안전하게 전환 가능
	SyncViewportClient();
	SyncFocusedViewportLocalState();

	// 4. 이제 더 이상 사용되지 않는 PIE 자원들을 정리
	if (PIEWorldContext)
	{
		DestroyWorldContext(PIEWorldContext);
		PIEWorldContext = nullptr;
	}

	SetSelectedActor(nullptr);
	
	// PIE 전용 클라이언트 객체 완전 제거
	PIEViewportClient.reset();
	LockedPIEViewportId = INVALID_VIEWPORT_ID;

	// 5. 카메라 상태 복원
	for (auto& Pair : EditorCameraStatesBackup)
	{
		if (FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(Pair.first))
		{
			Entry->LocalState = Pair.second;
		}
	}
	EditorCameraStatesBackup.clear();
}

void FEditorEngine::Shutdown()
{
	FEngineLog::Get().SetCallback({});
	EditorUI.SaveEditorSettings();

	if (IViewportClient* ActiveClient = GetViewportClient())
	{
		if (ActiveClient == PreviewViewportClient.get() || ActiveClient == PIEViewportClient.get())
		{
			SetViewportClient(nullptr);
		}
	}

	PIEViewportClient.reset();
	PreviewViewportClient.reset();
	CameraSubsystem.Shutdown();
	SelectionSubsystem.Shutdown();
	ReleaseEditorWorlds();

	FEngine::Shutdown();
}

void FEditorEngine::SetSelectedActor(AActor* InActor)
{
	SelectionSubsystem.SetSelectedActor(InActor);
}

AActor* FEditorEngine::GetSelectedActor() const
{
	return SelectionSubsystem.GetSelectedActor();
}

void FEditorEngine::ActivateEditorLevel()
{
	ActiveEditorWorldContext = (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext : nullptr;
}

bool FEditorEngine::ActivatePreviewLevel(const FString& ContextName)
{
	FWorldContext* PreviewContext = FindPreviewWorld(ContextName);
	if (PreviewContext == nullptr)
	{
		return false;
	}
	ActiveEditorWorldContext = PreviewContext;
	return true;
}

ULevel* FEditorEngine::GetEditorLevel() const
{
	return (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext->World->GetLevel() : nullptr;
}

ULevel* FEditorEngine::GetPreviewLevel(const FString& ContextName) const
{
	const FWorldContext* Context = FindPreviewWorld(ContextName);
	return (Context && Context->World) ? Context->World->GetLevel() : nullptr;
}

UWorld* FEditorEngine::GetEditorWorld() const
{
	return EditorWorldContext ? EditorWorldContext->World : nullptr;
}

const TArray<FWorldContext*>& FEditorEngine::GetPreviewWorldContexts() const
{
	return PreviewWorldContexts.empty() ? GetEmptyPreviewWorldContexts() : PreviewWorldContexts;
}

FWorldContext* FEditorEngine::CreatePreviewWorldContext(const FString& ContextName, int32 Width, int32 Height)
{
	if (ContextName.empty())
	{
		return nullptr;
	}
	if (FWorldContext* ExistingContext = FindPreviewWorld(ContextName))
	{
		return ExistingContext;
	}
	const float AspectRatio = (Height > 0) ? (static_cast<float>(Width) / static_cast<float>(Height)) : 1.0f;
	FWorldContext* PreviewContext = CreateWorldContext(ContextName, EWorldType::Preview, AspectRatio, false);
	if (!PreviewContext)
	{
		return nullptr;
	}
	PreviewWorldContexts.push_back(PreviewContext);
	return PreviewContext;
}

ULevel* FEditorEngine::GetLevel() const
{
	return GetActiveLevel();
}

ULevel* FEditorEngine::GetActiveLevel() const
{
	UWorld* ActiveWorld = GetActiveWorld();
	return ActiveWorld ? ActiveWorld->GetLevel() : nullptr;
}

UWorld* FEditorEngine::GetActiveWorld() const
{
	return ActiveEditorWorldContext ? ActiveEditorWorldContext->World : FEngine::GetActiveWorld();
}

FViewportId FEditorEngine::GetPIEViewportId() const
{
	if (!IsPlayingInEditor())
	{
		return INVALID_VIEWPORT_ID;
	}

	if (LockedPIEViewportId != INVALID_VIEWPORT_ID)
	{
		return LockedPIEViewportId;
	}

	FSlateApplication* Slate = GetSlateApplication();
	FViewportId FocusedId = Slate ? Slate->GetFocusedViewportId() : INVALID_VIEWPORT_ID;

	// 1순위: 현재 포커스된 창이 Perspective라면 그걸로 당첨!
	const FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
	if (FocusedEntry && FocusedEntry->bActive && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
	{
		return FocusedId;
	}

	// 2순위: 활성 상태인 Perspective 창이 있다면 그걸 사용
	for (const FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		if (Entry.bActive && Entry.LocalState.ProjectionType == EViewportType::Perspective)
		{
			return Entry.Id;
		}
	}

	// 3순위: 무조건 0순위 Perspective 창을 강제 할당!
	const FViewportEntry* PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
	if (PerspEntry)
	{
		return PerspEntry->Id;
	}

	return INVALID_VIEWPORT_ID;
}

const FWorldContext* FEditorEngine::GetActiveWorldContext() const
{
	return ActiveEditorWorldContext ? ActiveEditorWorldContext : FEngine::GetActiveWorldContext();
}

void FEditorEngine::HandleResize(int32 Width, int32 Height)
{
	FEngine::HandleResize(Width, Height);
	if (Width == 0 || Height == 0)
	{
		return;
	}
	UpdateEditorWorldAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
}

void FEditorEngine::PreInitialize()
{
	ImGui_ImplWin32_EnableDpiAwareness();
	FEngineLog::Get().SetCallback([this](const char* Msg)
	{
		EditorUI.GetConsole().AddLog("%s", Msg);
	});
}

void FEditorEngine::BindHost(FWindowsWindow* InMainWindow)
{
	MainWindow = InMainWindow;
	EditorUI.SetupWindow(InMainWindow);
}

bool FEditorEngine::InitializeWorlds(int32 Width, int32 Height)
{
	return InitEditorWorlds(Width, Height);
}

bool FEditorEngine::InitializeMode()
{
	if (!InitEditorPreview())
	{
		return false;
	}
	InitEditorConsole();
	if (!InitEditorCamera())
	{
		return false;
	}
	InitEditorViewportRouting();
	return true;
}

void FEditorEngine::FinalizeInitialize()
{
	UE_LOG("EditorEngine initialized");
	const int32 W = MainWindow ? MainWindow->GetWidth() : 800;
	const int32 H = MainWindow ? MainWindow->GetHeight() : 600;
	TArray<FViewport>& Viewports = ViewportRegistry.GetViewports();
	FViewport* VPs[MAX_VIEWPORTS] = { &Viewports[0], &Viewports[1], &Viewports[2], &Viewports[3] };
	SlateApplication = std::make_unique<FSlateApplication>();
	SlateApplication->Initialize(FRect(0, 0, W, H), VPs, MAX_VIEWPORTS);
	EditorUI.OnSlateReady();
	CreateInitUI();
	FObjManager::PreloadAllModelFiles(FPaths::FromPath(FPaths::MeshDir()).c_str());
}

void FEditorEngine::PrepareFrame(float DeltaTime)
{
	if (bRequestEndPIE)
	{
		EndPIE();
	}

	static bool bLastCaptured = false;

	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		const bool bIsPaused = PIEWorld && PIEWorld->IsPaused();

		if (Input && Input->IsKeyDown(VK_ESCAPE))
		{
			RequestEndPIE();
			return;
		}

		// F8: Possess/Eject 토글
		static bool bF8WasDown = false;
		bool bF8IsDown = (::GetAsyncKeyState(VK_F8) & 0x8000) != 0;

		if (bF8IsDown && !bF8WasDown)
		{
			if (Input)
			{
				bool bNewCapture = !Input->IsMouseCaptured();
				Input->SetMouseCapture(bNewCapture);

				if (bNewCapture)
				{
					bPIEJustCaptured = true; 
					if (MainWindow)
					{
						const FViewportId PIEId = GetPIEViewportId();
						const FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(PIEId);
						if (Entry && Entry->Viewport)
						{
							const FRect& Rect = Entry->Viewport->GetRect();
							POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
							::ClientToScreen(MainWindow->GetHwnd(), &Center);
							::SetCursorPos(Center.x, Center.y);
						}
					}
				}
				UE_LOG("[PIE] %s", Input->IsMouseCaptured() ? "Possessed (FPS Mode)" : "Ejected (Editor Mode)");
			}
		}
		bF8WasDown = bF8IsDown;

		// [캡처 해제 시 점프 방지]: 마우스 캡처가 풀리는 순간, 현재 PIE 카메라 좌표를 에디터 카메라에 강제 동기화
		if (Input && bLastCaptured && !Input->IsMouseCaptured() && PIEWorld)
		{
			if (UCameraComponent* PIECam = PIEWorld->GetActiveCameraComponent())
			{
				FCamera* Cam = PIECam->GetCamera();
				for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
				{
					if (Entry.LocalState.ProjectionType == EViewportType::Perspective)
					{
						Entry.LocalState.Position = Cam->GetPosition();
						Entry.LocalState.Rotation.Yaw = Cam->GetYaw();
						Entry.LocalState.Rotation.Pitch = Cam->GetPitch();
					}
				}
			}
		}
		bLastCaptured = Input ? Input->IsMouseCaptured() : false;
	}

	// [Possessed 상태 시점 보존]: 캡처 중에는 PIE 카메라 정보를 에디터 상태로 계속 밀어넣음
	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		UCameraComponent* PIECamComp = PIEWorld ? PIEWorld->GetActiveCameraComponent() : nullptr;
		const bool bIsPaused = PIEWorld && PIEWorld->IsPaused();

		if (Input && Input->IsMouseCaptured() && PIECamComp && PIECamComp->GetCamera())
		{
			FCamera* Cam = PIECamComp->GetCamera();

			for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
			{
				if (Entry.LocalState.ProjectionType == EViewportType::Perspective)
				{
					Entry.LocalState.Position = Cam->GetPosition();
					Entry.LocalState.Rotation.Yaw = Cam->GetYaw();
					Entry.LocalState.Rotation.Pitch = Cam->GetPitch();
					Entry.LocalState.FovY = Cam->GetFOV();
				}
			}

			if (FEditorViewportController* Controller = CameraSubsystem.GetViewportController())
			{
				if (FViewportLocalState* ActiveState = Controller->GetActiveLocalState())
				{
					ActiveState->Position = Cam->GetPosition();
					ActiveState->Rotation.Yaw = Cam->GetYaw();
					ActiveState->Rotation.Pitch = Cam->GetPitch();
					ActiveState->FovY = Cam->GetFOV();
				}
			}

			if (AEditorCameraPawn* EditorPawn = CameraSubsystem.GetEditorCameraPawn())
			{
				EditorPawn->SetActorLocation(Cam->GetPosition());
				if (UCameraComponent* EditorCamComp = EditorPawn->GetCameraComponent())
				{
					EditorCamComp->GetCamera()->SetRotation(Cam->GetYaw(), Cam->GetPitch());
					EditorCamComp->SetFov(Cam->GetFOV());
				}
			}
		}
	}

	SyncViewportClient();
	SyncFocusedViewportLocalState();
	CameraSubsystem.PrepareFrame(GetActiveWorld(), GetLevel(), DeltaTime);

	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		UCameraComponent* PIECamComp = PIEWorld ? PIEWorld->GetActiveCameraComponent() : nullptr;
		const bool bIsPaused = PIEWorld && PIEWorld->IsPaused();

		if (Input && PIECamComp && PIECamComp->GetCamera())
		{
			// [Pause 대응]: 일시정지 중에는 모든 조작 차단
			if (!bIsPaused)
			{
				if (!Input->IsMouseCaptured())
				{
					// [Ejected 상태]: 오직 PIE 뷰포트의 좌표만 PIE 카메라에 투영
					const FViewportId PIEId = GetPIEViewportId();
					FViewportEntry* PIEEntry = ViewportRegistry.FindEntryByViewportID(PIEId);

					if (PIEEntry)
					{
						FCamera* Cam = PIECamComp->GetCamera();
						Cam->SetPosition(PIEEntry->LocalState.Position);
						Cam->SetRotation(PIEEntry->LocalState.Rotation.Yaw, PIEEntry->LocalState.Rotation.Pitch);
						PIECamComp->SetFov(PIEEntry->LocalState.FovY);
					}
				}
				else
				{
					// 마우스가 캡처된 상태일 때만 PIE 카메라 조작
					if (bPIEJustCaptured)
					{
						bPIEJustCaptured = false;
					}
					else
					{
						TickPIECamera(DeltaTime);
					}
				}
			}
		}
	}
}

void FEditorEngine::TickWorlds(float DeltaTime)
{
	if (UWorld* ActiveWorld = GetActiveWorld())
	{
		ActiveWorld->Tick(DeltaTime);
	}
}

std::unique_ptr<IViewportClient> FEditorEngine::CreateViewportClient()
{
	auto Client = std::make_unique<FEditorViewportClient>(*this, EditorUI, ViewportRegistry, MainWindow);
	EditorViewportClientRaw = Client.get();
	return Client;
}

void FEditorEngine::RenderFrame()
{
	FRenderer* Renderer = GetRenderer();
	if (!Renderer || Renderer->IsOccluded()) return;

	Renderer->BeginFrame();

	// [안정성 확보]: 프레임 시작 시점의 클라이언트를 캡처
	IViewportClient* CurrentClient = GetViewportClient();

	// 1. PIE 모드일 때: 항상 EditorViewportClient(4분할)가 주도적으로 렌더링을 수행
	if (IsPlayingInEditor() && EditorViewportClientRaw)
	{
		EditorViewportClientRaw->Render(this, Renderer);

		// Possessed(FPS) 모드인 경우, EditorViewportClient::Render 내의 RenderAll이
		// 이미 게임 화면(1개) + 에디터 화면(3개)을 모두 그렸으므로 추가 렌더링 없이 종료
		if (CurrentClient == PIEViewportClient.get())
		{
			Renderer->EndFrame();
			return;
		}
	}

	// 2. 에디터 모드 또는 프리뷰 모드일 때: 현재 활성 클라이언트를 통해 렌더링
	if (CurrentClient)
	{
		// PIE 모드에서 이미 EditorViewportClientRaw를 통해 4분할 렌더링을 마쳤다면 중복 호출 방지
		if (!IsPlayingInEditor() || CurrentClient != EditorViewportClientRaw)
		{
			CurrentClient->Render(this, Renderer);
		}
	}

	Renderer->EndFrame();
}

void FEditorEngine::SyncPlatformState()
{
	SyncPlatformCursor();
}

FEditorViewportController* FEditorEngine::GetViewportController()
{
	return CameraSubsystem.GetViewportController();
}

void FEditorEngine::FlushDebugDrawForViewport(FRenderer* Renderer, const FShowFlags& ShowFlags, bool bClearAfterFlush)
{
	if (!Renderer)
	{
		return;
	}
	if (UWorld* ActiveWorld = GetActiveWorld())
	{
		GetDebugDrawManager().Flush(Renderer, ShowFlags, ActiveWorld, bClearAfterFlush);
	}
	else if (bClearAfterFlush)
	{
		GetDebugDrawManager().Clear();
	}
}

void FEditorEngine::ClearDebugDrawForFrame()
{
	GetDebugDrawManager().Clear();
}

void FEditorEngine::CreateInitUI()
{
	auto* RawEditorVP = static_cast<FEditorViewportClient*>(ViewportClient.get());
	std::unique_ptr<SEditorViewportOverlay> Overlay =
		std::make_unique<SEditorViewportOverlay>(this, &EditorUI, RawEditorVP);
	SWidget* RawOverlay = SlateApplication->CreateWidget(std::move(Overlay));
	SlateApplication->AddOverlayWidget(RawOverlay);
}

bool FEditorEngine::InitEditorPreview()
{
	InitializeDefaultPreviewLevel(this);
	PreviewViewportClient = std::make_unique<FPreviewViewportClient>(EditorUI, PreviewLevelContextName);
	return PreviewViewportClient != nullptr;
}

void FEditorEngine::InitEditorConsole()
{
	FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
	CVM.GetAllNames([this](const FString& Name)
	{
		EditorUI.GetConsole().RegisterCommand(Name.c_str());
	});
	EditorUI.GetConsole().SetCommandHandler([](const char* CommandLine)
	{
		FString Result;
		if (FConsoleVariableManager::Get().Execute(CommandLine, Result))
		{
			FEngineLog::Get().Log("%s", Result.c_str());
		}
		else
		{
			FEngineLog::Get().Log("[error] Unknown command: '%s'", CommandLine);
		}
	});
}

bool FEditorEngine::InitEditorCamera()
{
	return CameraSubsystem.Initialize(GetActiveWorld(), GetInputManager(), GetEnhancedInputManager());
}

void FEditorEngine::InitEditorViewportRouting()
{
	SyncViewportClient();
	FViewportEntry* PerspEntry = nullptr;
	if (SlateApplication)
	{
		const FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
		if (FocusedId != INVALID_VIEWPORT_ID)
		{
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
			if (FocusedEntry && FocusedEntry->bActive &&
				FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
			{
				PerspEntry = FocusedEntry;
			}
		}
	}
	if (!PerspEntry)
	{
		PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
	}
	if (PerspEntry)
	{
		CameraSubsystem.GetViewportController()->SetActiveLocalState(&PerspEntry->LocalState);
	}
}

bool FEditorEngine::InitEditorWorlds(int32 Width, int32 Height)
{
	const float AspectRatio = (Height > 0) ? (static_cast<float>(Width) / static_cast<float>(Height)) : 1.0f;
	EditorWorldContext = CreateWorldContext("EditorLevel", EWorldType::Editor, AspectRatio, true);
	if (!EditorWorldContext)
	{
		return false;
	}
	EnsureDefaultCameraActor();
	ActivateEditorLevel();
	return true;
}

void FEditorEngine::EnsureDefaultCameraActor()
{
	ULevel* Level = GetEditorLevel();
	if (!Level)
	{
		return;
	}
	for (AActor* Actor : Level->GetActors())
	{
		if (Actor && Actor->IsA(ACameraActor::StaticClass()))
		{
			return;
		}
	}
	ACameraActor* DefaultCam = Level->SpawnActor<ACameraActor>("DefaultCameraActor");
	if (DefaultCam)
	{
		DefaultCam->SetActorLocation(FVector(-5.0f, 0.0f, 2.0f));
		UE_LOG("[Editor] Default CameraActor spawned.");
	}
}

void FEditorEngine::ReleaseEditorWorlds()
{
	ActiveEditorWorldContext = nullptr;
	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		DestroyWorldContext(PreviewContext);
	}
	PreviewWorldContexts.clear();
	DestroyWorldContext(EditorWorldContext);
	EditorWorldContext = nullptr;
}

FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName)
{
	for (FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}
	return nullptr;
}

const FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName) const
{
	for (const FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}
	return nullptr;
}

void FEditorEngine::UpdateEditorWorldAspectRatio(float AspectRatio)
{
	UpdateWorldAspectRatio(EditorWorldContext ? EditorWorldContext->World : nullptr, AspectRatio);
	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		UpdateWorldAspectRatio(PreviewContext ? PreviewContext->World : nullptr, AspectRatio);
	}
}

void FEditorEngine::SyncFocusedViewportLocalState()
{
	if (!EditorViewportClientRaw || !SlateApplication)
	{
		return;
	}
	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		if (Input && Input->IsMouseCaptured())
		{
			CameraSubsystem.GetViewportController()->SetActiveLocalState(nullptr);
			return;
		}
	}
	FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
	FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
	FViewportLocalState* LocalState = (FocusedEntry && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
		? &FocusedEntry->LocalState
		: nullptr;
	CameraSubsystem.GetViewportController()->SetActiveLocalState(LocalState);
}

void FEditorEngine::SyncPlatformCursor()
{
	if (!SlateApplication || !SlateApplication->GetIsCoursorInArea())
	{
		return;
	}
	const EMouseCursor SlateCursor = SlateApplication->GetCurrentCursor();
	LPCWSTR WinCursorName = IDC_ARROW;
	switch (SlateCursor)
	{
	case EMouseCursor::Default: WinCursorName = IDC_ARROW; break;
	case EMouseCursor::ResizeLeftRight: WinCursorName = IDC_SIZEWE; break;
	case EMouseCursor::ResizeUpDown: WinCursorName = IDC_SIZENS; break;
	case EMouseCursor::Hand: WinCursorName = IDC_HAND; break;
	case EMouseCursor::None: WinCursorName = nullptr; break;
	}
	if (WinCursorName)
	{
		::SetCursor(::LoadCursor(NULL, WinCursorName));
	}
	else
	{
		::SetCursor(nullptr);
	}
}

void FEditorEngine::SyncViewportClient()
{
	if (!GetActiveWorldContext())
	{
		return;
	}
	IViewportClient* TargetViewportClient = ViewportClient.get();
	const FWorldContext* ActiveLevelContext = GetActiveWorldContext();
	if (ActiveLevelContext && ActiveLevelContext->WorldType == EWorldType::Preview && PreviewViewportClient)
	{
		TargetViewportClient = PreviewViewportClient.get();
	}
	else if (ActiveLevelContext && ActiveLevelContext->WorldType == EWorldType::PIE)
	{
		FInputManager* Input = GetInputManager();
		if (Input && !Input->IsMouseCaptured())
		{
			TargetViewportClient = EditorViewportClientRaw;
		}
		else
		{
			if (!PIEViewportClient)
			{
				PIEViewportClient = std::make_unique<FGameViewportClient>();
			}
			TargetViewportClient = PIEViewportClient.get();
		}
	}
	if (GetViewportClient() != TargetViewportClient)
	{
		SetViewportClient(TargetViewportClient);
	}
}

FViewport* FEditorEngine::FindViewport(FViewportId Id)
{
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		if (Entry.Id == Id && Entry.bActive)
		{
			return Entry.Viewport;
		}
	}
	return nullptr;
}

void FEditorEngine::TickPIECamera(float DeltaTime)
{
	FInputManager* Input = GetInputManager();
	if (!Input)
	{
		return;
	}
	
	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
	if (!PIEWorld)
	{
		return;
	}
	UCameraComponent* CamComp = PIEWorld->GetActiveCameraComponent();
	if (!CamComp)
	{
		return;
	}
	FCamera* Camera = CamComp->GetCamera();
	if (!Camera)
	{
		return;
	}

	// 마우스가 캡처된 상태라면 우클릭 없이도 회전 가능
	if (Input->IsMouseCaptured())
	{
		const float Sensitivity = Camera->GetMouseSensitivity();
		const float DX = Input->GetMouseDeltaX();
		const float DY = Input->GetMouseDeltaY();
		Camera->Rotate(DX * Sensitivity, -DY * Sensitivity);
	}

	if (Input->IsKeyDown('W'))
	{
		Camera->MoveForward(DeltaTime);
	}
	if (Input->IsKeyDown('S'))
	{
		Camera->MoveForward(-DeltaTime);
	}
	if (Input->IsKeyDown('A'))
	{
		Camera->MoveRight(-DeltaTime);
	}
	if (Input->IsKeyDown('D'))
	{
		Camera->MoveRight(DeltaTime);
	}
	if (Input->IsKeyDown('E'))
	{
		Camera->MoveUp(DeltaTime);
	}
	if (Input->IsKeyDown('Q'))
	{
		Camera->MoveUp(-DeltaTime);
	}
}
