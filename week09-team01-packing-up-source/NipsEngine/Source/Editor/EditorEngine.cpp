#include "Editor/EditorEngine.h"

#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Serialization/SceneSaveManager.h"
//#include "Game/GameTypes.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/GameItemDataLoader.h"
#include "Game/Systems/ItemSystem.h"
#include "Game/Settings/GameSettings.h"
#include "Game/UI/GameUISystem.h"
#include "Engine/Slate/SlateApplication.h"
#include "Engine/Input/InputRouter.h"
#include "Viewport/ViewportRect.h"
#include "Component/GizmoComponent.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/PrimitiveActors.h"
#include "Editor/EditorRenderPipeline.h"
#include "Audio/AudioSystem.h"
#include "Core/Paths.h"
#include "Core/Logging/Stats.h"
#include "Slate/SSplitterV.h"
#include "Slate/SSplitterH.h"
#include "Settings/EditorSettings.h"
#include "Settings/EngineSettings.h"
#include <algorithm>

DEFINE_CLASS(UEditorEngine, UEngine)
REGISTER_FACTORY(UEditorEngine)

namespace
{
	void InitializePlayGameData()
	{
		//Game::RegisterGameTypes();

		FItemSystem& Items = FItemSystem::Get();
		Items.ClearItemData();
		FCleaningToolSystem::Get().ClearToolData();
		FGameItemDataLoader::LoadFromFile("Asset/Data/Items.json", Items);
		GGameContext::Get().Reset();
		Items.ResetRuntimeState();
	}

	int32 FindComponentIndex(AActor* Actor, UActorComponent* Component)
	{
		if (Actor == nullptr || Component == nullptr)
		{
			return -1;
		}

		const TArray<UActorComponent*>& Components = Actor->GetComponents();
		for (int32 Index = 0; Index < static_cast<int32>(Components.size()); ++Index)
		{
			if (Components[Index] == Component)
			{
				return Index;
			}
		}

		return -1;
	}

	int32 FindNonSceneComponentTypeOccurrence(AActor* Actor, UActorComponent* Component)
	{
		if (Actor == nullptr || Component == nullptr || Component->IsA<USceneComponent>())
		{
			return -1;
		}

		const auto* ComponentType = Component->GetTypeInfo();
		int32 Occurrence = 0;
		for (UActorComponent* Candidate : Actor->GetComponents())
		{
			if (Candidate == nullptr || Candidate->IsA<USceneComponent>() || Candidate->IsEditorOnly())
			{
				continue;
			}

			if (Candidate->GetTypeInfo() != ComponentType)
			{
				continue;
			}

			if (Candidate == Component)
			{
				return Occurrence;
			}

			++Occurrence;
		}

		return -1;
	}

	UActorComponent* ResolveNonSceneComponentByTypeOccurrence(AActor* Actor, UActorComponent* SourceComponent, int32 Occurrence)
	{
		if (Actor == nullptr || SourceComponent == nullptr || Occurrence < 0)
		{
			return nullptr;
		}

		const auto* SourceType = SourceComponent->GetTypeInfo();
		int32 CurrentOccurrence = 0;
		for (UActorComponent* Candidate : Actor->GetComponents())
		{
			if (Candidate == nullptr || Candidate->IsA<USceneComponent>() || Candidate->IsEditorOnly())
			{
				continue;
			}

			if (Candidate->GetTypeInfo() != SourceType)
			{
				continue;
			}

			if (CurrentOccurrence == Occurrence)
			{
				return Candidate;
			}

			++CurrentOccurrence;
		}

		return nullptr;
	}

	bool BuildSceneComponentPathRecursive(USceneComponent* Current, USceneComponent* Target, TArray<int32>& OutPath)
	{
		if (Current == nullptr || Target == nullptr)
		{
			return false;
		}

		if (Current == Target)
		{
			return true;
		}

		const TArray<USceneComponent*>& Children = Current->GetChildren();
		int32 RuntimeChildIndex = 0;
		for (int32 Index = 0; Index < static_cast<int32>(Children.size()); ++Index)
		{
			USceneComponent* Child = Children[Index];
			if (Child == nullptr || Child->IsEditorOnly())
			{
				continue;
			}

			OutPath.push_back(RuntimeChildIndex);
			if (BuildSceneComponentPathRecursive(Child, Target, OutPath))
			{
				return true;
			}
			OutPath.pop_back();
			++RuntimeChildIndex;
		}

		return false;
	}

	bool TryBuildSceneComponentPath(AActor* Actor, UActorComponent* Component, TArray<int32>& OutPath)
	{
		OutPath.clear();

		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (Actor == nullptr || SceneComponent == nullptr || Actor->GetRootComponent() == nullptr)
		{
			return false;
		}

		return BuildSceneComponentPathRecursive(Actor->GetRootComponent(), SceneComponent, OutPath);
	}

	EGameUIState GameUIStateFromBootMode(const FString& BootMode)
	{
		if (BootMode == "StartMenu")
		{
			return EGameUIState::StartMenu;
		}
		if (BootMode == "InGame")
		{
			return EGameUIState::InGame;
		}
		if (BootMode == "Ending")
		{
			return EGameUIState::Ending;
		}
		return EGameUIState::None;
	}

	USceneComponent* ResolveSceneComponentPath(AActor* Actor, const TArray<int32>& Path)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		USceneComponent* Current = Actor->GetRootComponent();
		for (int32 ChildIndex : Path)
		{
			if (Current == nullptr)
			{
				return nullptr;
			}

			const TArray<USceneComponent*>& Children = Current->GetChildren();
			USceneComponent* MatchedChild = nullptr;
			int32 RuntimeChildIndex = 0;
			for (USceneComponent* Child : Children)
			{
				if (Child == nullptr || Child->IsEditorOnly())
				{
					continue;
				}

				if (RuntimeChildIndex == ChildIndex)
				{
					MatchedChild = Child;
					break;
				}

				++RuntimeChildIndex;
			}

			if (MatchedChild == nullptr)
			{
				return nullptr;
			}

			Current = MatchedChild;
		}

		return Current;
	}

	UActorComponent* FindMatchingComponent(
		AActor* SourceActor,
		AActor* TargetActor,
		UActorComponent* SourceComponent,
		int32 FallbackIndex,
		int32 NonSceneTypeOccurrence,
		bool bHasScenePath,
		const TArray<int32>& ScenePath)
	{
		if (SourceActor == nullptr || TargetActor == nullptr || SourceComponent == nullptr)
		{
			return nullptr;
		}

		if (bHasScenePath)
		{
			if (USceneComponent* SceneComponent = ResolveSceneComponentPath(TargetActor, ScenePath))
			{
				if (SceneComponent->GetTypeInfo() == SourceComponent->GetTypeInfo())
				{
					return SceneComponent;
				}
			}
		}

		if (!SourceComponent->IsA<USceneComponent>())
		{
			if (UActorComponent* OccurrenceMatchedComponent =
				ResolveNonSceneComponentByTypeOccurrence(TargetActor, SourceComponent, NonSceneTypeOccurrence))
			{
				return OccurrenceMatchedComponent;
			}
		}

		const FName SourceName = SourceComponent->GetFName();
		const auto* SourceType = SourceComponent->GetTypeInfo();
		const TArray<UActorComponent*>& TargetComponents = TargetActor->GetComponents();

		for (UActorComponent* TargetComponent : TargetComponents)
		{
			if (TargetComponent != nullptr &&
				TargetComponent->GetFName() == SourceName &&
				TargetComponent->GetTypeInfo() == SourceType)
			{
				return TargetComponent;
			}
		}

		if (FallbackIndex >= 0 && FallbackIndex < static_cast<int32>(TargetComponents.size()))
		{
			UActorComponent* FallbackComponent = TargetComponents[FallbackIndex];
			if (FallbackComponent != nullptr && FallbackComponent->GetTypeInfo() == SourceType)
			{
				return FallbackComponent;
			}
		}

		return nullptr;
	}

	void SyncEngineSettings()
	{
		const FEditorSettings& EditorSettings = FEditorSettings::Get();
		FEngineSettings& EngineSettings = FEngineSettings::Get();

		EngineSettings.bEnableStaticMeshLOD = EditorSettings.ShowFlags.bEnableLOD;
		EngineSettings.SpatialBatchRefitMinDirtyCount = EditorSettings.SpatialBatchRefitMinDirtyCount;
		EngineSettings.SpatialBatchRefitDirtyPercentThreshold = EditorSettings.SpatialBatchRefitDirtyPercentThreshold;
		EngineSettings.SpatialRotationStructuralChangeThreshold = EditorSettings.SpatialRotationStructuralChangeThreshold;
		EngineSettings.SpatialRotationDirtyCountThreshold = EditorSettings.SpatialRotationDirtyCountThreshold;
		EngineSettings.SpatialRotationDirtyPercentThreshold = EditorSettings.SpatialRotationDirtyPercentThreshold;
	}
}

//  Init
void UEditorEngine::Init(FWindowsWindow* InWindow)
{
	UEngine::Init(InWindow);
	FEditorSettings::Get().LoadFromFile(FEditorSettings::GetDefaultSettingsPath());
	SyncEngineSettings();

	MainPanel.Create(Window, Renderer, this);
	if (WorldList.empty())
	{
		CreateWorldContext(EWorldType::Editor, FName("Default"));
	}
	SetActiveWorld(WorldList[0].ContextHandle);
	ApplySpatialIndexMaintenanceSettings();

	// Selection & Gizmo
	SelectionManager.Init();
	ViewportLayout.Init(InWindow, GetWorld(), &SelectionManager, this);
	GetFocusedWorld()->SetActiveCamera(GetCamera());

	// Slate 초기화 및 Viewport Layout 추가
	FSlateApplication::Get().Initialize();
	ViewportLayout.BuildViewportLayout(static_cast<int32>(Window->GetWidth()), static_cast<int32>(Window->GetHeight()));

	GameUISystem::Get().Init(
		InWindow->GetHWND(),
		Renderer.GetFD3DDevice().GetDevice(),
		Renderer.GetFD3DDevice().GetDeviceContext()
	);

	// Editor용 렌더 파이프라인 세팅
	SetRenderPipeline(std::make_unique<FEditorRenderPipeline>(this, Renderer));
}

void UEditorEngine::Shutdown()
{
	// 스플리터 비율을 Settings 에 기록 후 저장
	if (SSplitterV* SV = ViewportLayout.GetRootSplitterV())
		FEditorSettings::Get().SplitterVRatio = SV->GetSplitRatio();
	if (SSplitterH* SH = ViewportLayout.GetTopSplitterH())
		FEditorSettings::Get().SplitterHRatio = SH->GetSplitRatio();

	FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());

	CloseScene();
	SelectionManager.Shutdown();
	MainPanel.Release();
	GameUISystem::Get().Shutdown();
	
	// CloseScene 이후에 ViewportLayout을 내리면 Client 포인터 단절로 인한 역참조를 피할 수 있습니다.
	ViewportLayout.Shutdown();           // 위젯 트리 해제 (소유권: UEditorEngine)
	FSlateApplication::Get().Shutdown(); // RootWindow 해제

	// 엔진 공통 해제 (Renderer, D3D 등)
	UEngine::Shutdown();
}

void UEditorEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);
	ViewportLayout.OnWindowResized(Width, Height);
}


void UEditorEngine::Tick(float DeltaTime)
{
	FInputRouter::TickInputSystem();
	UpdateInputWorldType();
	ViewportLayout.Tick(DeltaTime);
	MainPanel.Update();
	WorldTick(DeltaTime);
	ViewportLayout.LateTick(DeltaTime);
	Render(DeltaTime);
}

void UEditorEngine::WorldTick(float DeltaTime)
{
	// 포커스된 뷰포트의 카메라를 해당 월드의 ActiveCamera로 설정
	const int32 FocusedIdx = ViewportLayout.GetLastFocusedViewportIndex();
	FEditorViewportClient* FocusedClient = ViewportLayout.GetViewportClient(FocusedIdx);
	if (UWorld* FocusedWorld = FocusedClient->GetFocusedWorld())
	{
		if (FocusedWorld->GetWorldType() == EWorldType::Editor)
		{
			if (FViewportCamera* Cam = FocusedClient->GetCamera())
			{
				FocusedWorld->SetActiveCamera(Cam);
			}
		}
	}

	// WorldList의 모든 월드에 대해 Tick()을 넣어줍니다. UWorld::Tick에서 EWorldType에 따라 TickEditor / TickGame이 분기됩니다.
	for (FWorldContext& Ctx : WorldList)
	{
		if (!Ctx.World || Ctx.bPaused)
			continue;
		Ctx.World->Tick(DeltaTime);
	}

	FAudioSystem::Get().Tick(DeltaTime);
}

void UEditorEngine::RenderUI(float DeltaTime)
{
	MainPanel.Render(DeltaTime);
}

void UEditorEngine::StartPlaySession()
{
	const EViewportPlayState CurrentState = GetEditorState();

	if (CurrentState == EViewportPlayState::Paused)
	{
		ResumePlaySession();
		return;
	}
	if (CurrentState == EViewportPlayState::Playing) return;

	FAudioSystem::Get().Init();
	FAudioSystem::Get().StopAll();

	// 포커스된 뷰포트 클라이언트를 찾고 카메라 상태를 저장한 뒤, 실행 상태를 변경합니다.
	const int32 FocusedIdx = ViewportLayout.GetLastFocusedViewportIndex();
	FEditorViewportClient* FocusedClient = ViewportLayout.GetViewportClient(FocusedIdx);
	UWorld* FocusedWorld = GetFocusedWorld();

	if (!FocusedWorld) return;

	const TArray<AActor*> PreviousSelectedActors = SelectionManager.GetSelectedActors();
	AActor* PreviousPrimaryActor = SelectionManager.GetPrimarySelection();
	UActorComponent* PreviousSelectedComponent = MainPanel.GetPropertyWidget().GetSelectedDetailComponent();
	const bool bPreviousActorSelected = MainPanel.GetPropertyWidget().IsActorSelected();
	const int32 PreviousSelectedComponentIndex = FindComponentIndex(PreviousPrimaryActor, PreviousSelectedComponent);
	const int32 PreviousNonSceneTypeOccurrence = FindNonSceneComponentTypeOccurrence(PreviousPrimaryActor, PreviousSelectedComponent);
	TArray<int32> PreviousSelectedComponentScenePath;
	const bool bPreviousHasScenePath = TryBuildSceneComponentPath(
		PreviousPrimaryActor,
		PreviousSelectedComponent,
		PreviousSelectedComponentScenePath);

	FocusedClient->SaveCameraSnapshot();

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->SetPlayState(EViewportPlayState::Playing);
		}
	}

	// PIE 월드 복제하고 세팅한 뒤, RegisterWorld() 헬퍼를 사용해 월드를 WorldList에 등록합니다.
	UWorld* PIEWorld = Cast<UWorld>(FocusedWorld->Duplicate());
	PIEWorld->SetWorldType(EWorldType::PIE);
	FName PIEHandle(("PIE_" + std::to_string(FocusedIdx)).c_str());
	std::string PIEName = "PIE_World_" + std::to_string(FocusedIdx);
	
	RegisterWorld(PIEWorld, EWorldType::PIE, PIEHandle, PIEName);
	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		ViewportPIEHandles[i] = PIEHandle;
	}

	// 월드를 전환한 뒤 모든 뷰포트에 같은 PIE World를 연결하고, PIE World를 실행합니다.
	SetActiveWorld(PIEHandle);
	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->StartPIE(PIEWorld);
			ViewportClient->SetEndPIECallback([this]() { StopPlaySession(); });
		}
	}

	const FString CurrentSceneName = MainPanel.GetSceneWidget().GetCurrentSceneName();
	const FString CurrentScenePath = FPaths::ToString(FPaths::Combine(
		FSceneSaveManager::GetSceneDirectory(),
		FPaths::ToWide(CurrentSceneName + ".Scene")));

	InitializePlayGameData();
	GameUISystem::Get().ResetGameData();
	GameUISystem::Get().SetState(GameUIStateFromBootMode(FSceneSaveManager::GetGameUIBootMode(CurrentScenePath)));
	GameUISystem::Get().SetExitPlayCallback([this]() { StopPlaySession(); });
	GameUISystem::Get().SetStartGameCallback([this]() { StartMainGamePIE(); });
	FInputRouter::SetCursorVisibility(GameUISystem::Get().WantsMouseCursor());
	FInputRouter::ResetMouseDelta(2);

	const TArray<AActor*> EditorActors = FocusedWorld->GetActors();
	const TArray<AActor*> PIEActors = PIEWorld->GetActors();
	TArray<AActor*> MappedSelectedActors;
	MappedSelectedActors.reserve(PreviousSelectedActors.size());
	AActor* MappedPrimaryActor = nullptr;

	for (AActor* PreviousActor : PreviousSelectedActors)
	{
		AActor* MappedActor = nullptr;
		for (int32 Index = 0; Index < static_cast<int32>(EditorActors.size()); ++Index)
		{
			if (EditorActors[Index] == PreviousActor && Index < static_cast<int32>(PIEActors.size()))
			{
				MappedActor = PIEActors[Index];
				break;
			}
		}

		if (MappedActor != nullptr)
		{
			MappedSelectedActors.push_back(MappedActor);
			if (PreviousActor == PreviousPrimaryActor)
			{
				MappedPrimaryActor = MappedActor;
			}
		}
	}

	if (MappedPrimaryActor == nullptr && !MappedSelectedActors.empty())
	{
		MappedPrimaryActor = MappedSelectedActors.front();
	}

	SelectionManager.ClearSelection();
	if (MappedPrimaryActor != nullptr)
	{
		SelectionManager.AddSelect(MappedPrimaryActor);
	}
	for (AActor* MappedActor : MappedSelectedActors)
	{
		if (MappedActor != MappedPrimaryActor)
		{
			SelectionManager.AddSelect(MappedActor);
		}
	}

	UActorComponent* MappedSelectedComponent = nullptr;
	if (MappedPrimaryActor != nullptr && PreviousSelectedComponent != nullptr)
	{
		MappedSelectedComponent = FindMatchingComponent(
			PreviousPrimaryActor,
			MappedPrimaryActor,
			PreviousSelectedComponent,
			PreviousSelectedComponentIndex,
			PreviousNonSceneTypeOccurrence,
			bPreviousHasScenePath,
			PreviousSelectedComponentScenePath);
	}

	MainPanel.GetPropertyWidget().RestoreSelection(MappedPrimaryActor, MappedSelectedComponent, bPreviousActorSelected);

	UCameraComponent* PawnCamera = nullptr;
	if (APawnActor* Pawn = PIEWorld->FindPawn())
	{
		for (UActorComponent* Component : Pawn->GetComponents())
		{
			if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(Component))
			{
				PawnCamera = CameraComponent;
				break;
			}
		}
	}

	if (PawnCamera)
	{
		PIEWorld->SetActiveCameraComponent(PawnCamera);
	}
	else
	{
		PIEWorld->SetActiveCamera(FocusedClient->GetCamera());
	}
	PIEWorld->BeginPlay();
}

void UEditorEngine::StartMainGamePIE()
{
	if (GetEditorState() != EViewportPlayState::Playing)
	{
		return;
	}

	const int32 FocusedIdx = ViewportLayout.GetLastFocusedViewportIndex();
	auto PIEHandleIt = ViewportPIEHandles.find(FocusedIdx);
	if (PIEHandleIt == ViewportPIEHandles.end() && !ViewportPIEHandles.empty())
	{
		PIEHandleIt = ViewportPIEHandles.begin();
	}

	const FName PIEHandle = PIEHandleIt != ViewportPIEHandles.end()
		? PIEHandleIt->second
		: FName(("PIE_" + std::to_string(FocusedIdx)).c_str());
	const std::string PIEName = "PIE_World_" + std::to_string(FocusedIdx);

	const FString ScenePath = FPaths::ToString(FPaths::Combine(FPaths::SceneDir(), GameSettings::MainSceneName));
	FWorldContext MainSceneContext;
	FSceneSaveManager::Load(ScenePath, MainSceneContext, nullptr);
	UWorld* MainSceneWorld = MainSceneContext.World;
	if (MainSceneWorld == nullptr)
	{
		return;
	}

	MainSceneWorld->SetWorldType(EWorldType::PIE);
	ApplySpatialIndexMaintenanceSettings(MainSceneWorld);

	if (GetWorldContextFromHandle(PIEHandle) != nullptr)
	{
		UnregisterWorld(PIEHandle);
	}

	RegisterWorld(MainSceneWorld, EWorldType::PIE, PIEHandle, PIEName);
	SetActiveWorld(PIEHandle);
	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		ViewportPIEHandles[i] = PIEHandle;
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->SetPlayState(EViewportPlayState::Playing);
			ViewportClient->StartPIE(MainSceneWorld);
			ViewportClient->SetEndPIECallback([this]() { StopPlaySession(); });
		}
	}

	InitializePlayGameData();
	GameUISystem::Get().ResetGameData();
	GameUISystem::Get().SetState(EGameUIState::InGame);
	GameUISystem::Get().SetExitPlayCallback([this]() { StopPlaySession(); });
	GameUISystem::Get().SetStartGameCallback([this]() { StartMainGamePIE(); });

	FInputRouter::SetCursorVisibility(false);
	FInputRouter::ResetMouseDelta(2);
	MainSceneWorld->BeginPlay();
}

void UEditorEngine::PausePlaySession()
{
	if (GetEditorState() != EViewportPlayState::Playing)
		return;

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->SetPlayState(EViewportPlayState::Paused);
		}
	}

	// PIE 컨텍스트를 일시정지 상태로 표시해 WorldTick에서 제외합니다.
	auto HandleIt = ViewportPIEHandles.find(ViewportLayout.GetLastFocusedViewportIndex());
	if (HandleIt == ViewportPIEHandles.end() && !ViewportPIEHandles.empty())
	{
		HandleIt = ViewportPIEHandles.begin();
	}
	if (HandleIt != ViewportPIEHandles.end())
	{
		if (FWorldContext* Ctx = GetWorldContextFromHandle(HandleIt->second))
		{
			Ctx->bPaused = true;
		}
	}
}

void UEditorEngine::ResumePlaySession()
{
	auto ResumeIt = ViewportPIEHandles.find(ViewportLayout.GetLastFocusedViewportIndex());
	if (ResumeIt == ViewportPIEHandles.end() && !ViewportPIEHandles.empty())
	{
		ResumeIt = ViewportPIEHandles.begin();
	}

	if (ResumeIt != ViewportPIEHandles.end())
	{
		if (FWorldContext* Ctx = GetWorldContextFromHandle(ResumeIt->second))
		{
			Ctx->bPaused = false;
		}
	}

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->SetPlayState(EViewportPlayState::Playing);
		}
	}
}

void UEditorEngine::StopPlaySession()
{
	if (GetEditorState() == EViewportPlayState::Editing)
		return;

	const int32 FocusedIdx = ViewportLayout.GetLastFocusedViewportIndex();
	FEditorViewportClient* FocusedClient = ViewportLayout.GetViewportClient(FocusedIdx);

	UWorld* PIEWorld = nullptr;
	FName PIEHandle = FName::None;
	TArray<AActor*> PIESelectedActors;
	AActor* PIEPrimaryActor = SelectionManager.GetPrimarySelection();
	UActorComponent* PIESelectedComponent = MainPanel.GetPropertyWidget().GetSelectedDetailComponent();
	const bool bPIEActorSelected = MainPanel.GetPropertyWidget().IsActorSelected();
	int32 PIESelectedComponentIndex = FindComponentIndex(PIEPrimaryActor, PIESelectedComponent);
	const int32 PIENonSceneTypeOccurrence = FindNonSceneComponentTypeOccurrence(PIEPrimaryActor, PIESelectedComponent);
	TArray<int32> PIESelectedComponentScenePath;
	const bool bPIEHasScenePath = TryBuildSceneComponentPath(
		PIEPrimaryActor,
		PIESelectedComponent,
		PIESelectedComponentScenePath);

	// 기존 PIE 월드를 해제합니다. 모든 뷰포트가 같은 PIE 핸들을 공유합니다.
	auto HandleIt = ViewportPIEHandles.find(FocusedIdx);
	if (HandleIt == ViewportPIEHandles.end() && !ViewportPIEHandles.empty())
	{
		HandleIt = ViewportPIEHandles.begin();
	}
	if (HandleIt != ViewportPIEHandles.end())
	{
		PIEHandle = HandleIt->second;
		if (FWorldContext* PIECtx = GetWorldContextFromHandle(PIEHandle))
		{
			PIEWorld = PIECtx->World;
			PIESelectedActors = SelectionManager.GetSelectedActors();
		}
	}

	// 원본 에디터 월드를 검색합니다.
	FName EditorHandle = GetEditorWorldHandle();
	UWorld* EditorWorld = nullptr;
	
	if (EditorHandle != FName::None)
	{
		SetActiveWorld(EditorHandle);
		if (FWorldContext* Ctx = GetWorldContextFromHandle(EditorHandle))
		{
			EditorWorld = Ctx->World;
		}
	}

	TArray<AActor*> RestoredSelectedActors;
	AActor* RestoredPrimaryActor = nullptr;
	UActorComponent* RestoredSelectedComponent = nullptr;
	if (PIEWorld != nullptr && EditorWorld != nullptr)
	{
		const TArray<AActor*> PIEActors = PIEWorld->GetActors();
		const TArray<AActor*> EditorActors = EditorWorld->GetActors();
		RestoredSelectedActors.reserve(PIESelectedActors.size());

		for (AActor* SelectedActor : PIESelectedActors)
		{
			AActor* MappedActor = nullptr;
			for (int32 Index = 0; Index < static_cast<int32>(PIEActors.size()); ++Index)
			{
				if (PIEActors[Index] == SelectedActor && Index < static_cast<int32>(EditorActors.size()))
				{
					MappedActor = EditorActors[Index];
					break;
				}
			}

			if (MappedActor != nullptr)
			{
				RestoredSelectedActors.push_back(MappedActor);
				if (SelectedActor == PIEPrimaryActor)
				{
					RestoredPrimaryActor = MappedActor;
				}
			}
		}

		if (RestoredPrimaryActor == nullptr && !RestoredSelectedActors.empty())
		{
			RestoredPrimaryActor = RestoredSelectedActors.front();
		}

		if (RestoredPrimaryActor != nullptr && PIESelectedComponent != nullptr)
		{
			RestoredSelectedComponent = FindMatchingComponent(
				PIEPrimaryActor,
				RestoredPrimaryActor,
				PIESelectedComponent,
				PIESelectedComponentIndex,
				PIENonSceneTypeOccurrence,
				bPIEHasScenePath,
				PIESelectedComponentScenePath);
		}
	}

	if (PIEHandle != FName::None)
	{
		UnregisterWorld(PIEHandle);
	}

	ViewportPIEHandles.clear();

	// 원본 에디터 월드로 모든 뷰포트 및 상태를 복구합니다.
	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		if (FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i))
		{
			ViewportClient->EndPIE(EditorWorld);
			ViewportClient->SetPlayState(EViewportPlayState::Editing);
		}
	}
	FocusedClient->RestoreCameraSnapshot();

	// PIE 종료 시 게임 UI 상태 초기화 (Ending 화면 등이 에디터에 남지 않도록)
	GameUISystem::Get().SetState(EGameUIState::None);
	GameUISystem::Get().SetExitPlayCallback(nullptr);
	GameUISystem::Get().SetStartGameCallback(nullptr);

	if (ViewportPIEHandles.empty())
	{
		FInputRouter::SetCursorVisibility(true);
	}

	SelectionManager.ClearSelection();
	if (RestoredPrimaryActor != nullptr)
	{
		SelectionManager.AddSelect(RestoredPrimaryActor);
	}
	for (AActor* RestoredActor : RestoredSelectedActors)
	{
		if (RestoredActor != RestoredPrimaryActor)
		{
			SelectionManager.AddSelect(RestoredActor);
		}
	}

	if (RestoredSelectedActors.empty())
	{
		MainPanel.ResetWidgetSelections();
	}
	else
	{
		MainPanel.GetPropertyWidget().RestoreSelection(RestoredPrimaryActor, RestoredSelectedComponent, bPIEActorSelected);
	}
}

void UEditorEngine::ResetViewport()
{
	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i);
		if (!ViewportClient)
		{
			continue;
		}
		ViewportClient->CreateCamera();
		ViewportClient->SetWorld(GetWorld());
		ViewportClient->ApplyCameraMode();
	}

	// 디폴트로 0번 뷰포트의 카메라를 월드 활성 카메라로 재등록
	if (UWorld* ActiveWorld = GetWorld())
	{
		ActiveWorld->SetActiveCamera(ViewportLayout.GetIndexedViewportClientCamera(0));
	}
}

void UEditorEngine::CloseScene()
{
	SelectionManager.ClearSelection();

	for (FWorldContext& Ctx : WorldList)
	{
		Ctx.World->EndPlay(EEndPlayReason::Type::EndPlayInEditor);
		UObjectManager::Get().DestroyObject(Ctx.World);
	}
	WorldList.clear();
	ActiveWorldHandle = FName::None;

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i);
		if (!ViewportClient)
		{
			continue;
		}
		ViewportClient->DestroyCamera();
		ViewportClient->SetWorld(nullptr);
	}
}

void UEditorEngine::NewScene()
{
	ClearScene();
	FWorldContext& Ctx = CreateWorldContext(EWorldType::Editor, FName("NewScene"), "New Scene");
	SetActiveWorld(Ctx.ContextHandle);
	ApplySpatialIndexMaintenanceSettings(Ctx.World);

	ResetViewport();
}

void UEditorEngine::ApplySpatialIndexMaintenanceSettings(UWorld* TargetWorld)
{
	// Init 초반에는 ViewportLayout이 아직 연결되지 않았을 수 있으므로 FocusedWorld보다 ActiveWorld(GetWorld) 경로를 우선 사용한다.
	UWorld* World = (TargetWorld != nullptr) ? TargetWorld : GetWorld();
	if (World == nullptr)
	{
		World = GetFocusedWorld();
		if (World == nullptr)
		{
			return;
		}
	}

	SyncEngineSettings();
	FWorldSpatialIndex::FMaintenancePolicy& Policy = World->GetSpatialIndex().GetMaintenancePolicy();
	FEngineSettings::Get().ApplyToSpatialPolicy(Policy);
}

FViewportCamera* UEditorEngine::GetCamera()
{
	return ViewportLayout.GetIndexedViewportClientCamera(0);
}

const FViewportCamera* UEditorEngine::GetCamera() const
{
	return ViewportLayout.GetIndexedViewportClientCamera(0);
}

FEditorRenderPipeline* UEditorEngine::GetEditorRenderPipeline() const
{
	return static_cast<FEditorRenderPipeline*>(GetRenderPipeline());
}

void UEditorEngine::ClearScene()
{
	SelectionManager.ClearSelection();

	for (FWorldContext& Ctx : WorldList)
	{
		Ctx.World->EndPlay(EEndPlayReason::Type::LevelTransition);
		UObjectManager::Get().DestroyObject(Ctx.World);
	}

	WorldList.clear();
	ActiveWorldHandle = FName::None;

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		FEditorViewportClient* ViewportClient = ViewportLayout.GetViewportClient(i);
		if (!ViewportClient)
		{
			continue;
		}
		ViewportClient->DestroyCamera();
		ViewportClient->SetWorld(nullptr);
	}
}

// 이미 생성된 월드를 컨텍스트에 등록합니다.
FWorldContext& UEditorEngine::RegisterWorld(UWorld* InWorld, EWorldType Type, const FName& Handle, const std::string& Name)
{
	FWorldContext Context;
	Context.WorldType = Type;
	Context.World = InWorld;
	Context.ContextName = Name;
	Context.ContextHandle = Handle;
	
	WorldList.push_back(Context);
	return WorldList.back();
}

// 컨텍스트에서 월드를 찾아 파괴하고 리스트에서 제거합니다.
void UEditorEngine::UnregisterWorld(const FName& Handle)
{
	for (auto it = WorldList.begin(); it != WorldList.end(); ++it)
	{
		if (it->ContextHandle == Handle)
		{
			if (it->World)
			{
				it->World->EndPlay(EEndPlayReason::Type::EndPlayInEditor);
				UObjectManager::Get().DestroyObject(it->World);
			}
			WorldList.erase(it);
			return; // 찾아서 지웠으므로 즉시 종료
		}
	}
}

// Editor Context World 핸들을 찾아 반환합니다.
FName UEditorEngine::GetEditorWorldHandle() const
{
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.WorldType == EWorldType::Editor)
		{
			return Ctx.ContextHandle;
		}
	}
	return FName::None;
}
