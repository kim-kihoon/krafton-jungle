#include "Game/Viewport/GameViewportClient.h"

#include "Component/CameraComponent.h"
#include "Engine/GameFramework/World.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/PrimitiveActors.h"
#include "Game/UI/GameUISystem.h"
#include "Math/Utils.h"

#include <windows.h>

namespace
{
	APawnActor* EnsurePlayerPawn(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}

		if (APawnActor* ExistingPawn = World->FindPawn())
		{
			ExistingPawn->EnsureDefaultComponents();
			return ExistingPawn;
		}

		APawnActor* Pawn = World->SpawnActor<APawnActor>();
		Pawn->InitDefaultComponents();
		if (APlayerStartActor* PlayerStart = World->FindPlayerStart())
		{
			Pawn->SetActorLocation(PlayerStart->GetActorLocation());
			Pawn->SetActorRotation(PlayerStart->GetActorRotation());
		}
		return Pawn;
	}

	UCameraComponent* FindPawnCamera(APawnActor* Pawn)
	{
		if (Pawn == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Pawn->GetComponents())
		{
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
			{
				return Camera;
			}
		}

		return nullptr;
	}
}

FGameViewportClient::~FGameViewportClient()
{
	FInputRouter::LockMouse(false);
	FInputRouter::SetCursorVisibility(true);
}

// 디버그용 Free Camera와 PlayerController를 초기화합니다.
void FGameViewportClient::Initialize(FWindowsWindow* InWindow)
{
	FViewportClient::Initialize(InWindow);
	FreeCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	FreeCamera.SetLocation(FVector(-5.0f, -5.0f, 3.0f));
	FreeCamera.SetLookAt(FVector::ZeroVector);
	PlayerController.SetFreeCamera(&FreeCamera);
	PlayerController.SetWorld(World);
	PlayerController.SetToggleInputCaptureCallback([this]() { ToggleInteractionMode(); });
	PlayerController.SetTogglePauseCallback(&GameUISystem::TogglePauseMenuIfInGame);
	InputRouter.SetGamePlayerController(&PlayerController);
	InputRouter.SetUIInputHandler(&GameUISystem::Get());
	InputRouter.SetViewportDim(0.0f, 0.0f, WindowWidth, WindowHeight);
}

// 현재 활성화된 카메라에 맞춰 뷰포트 크기를 적용합니다.
void FGameViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);
	FreeCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	if (ActiveCamera)
	{
		ActiveCamera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
	InputRouter.SetViewportDim(0.0f, 0.0f, WindowWidth, WindowHeight);
}

void FGameViewportClient::Tick(float DeltaTime)
{
	const bool bUIWantsMouse = GameUISystem::Get().WantsMouseCursor();

	FInputRouteContext RouteContext;
	RouteContext.Window = Window;
	RouteContext.ViewportRect = FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	RouteContext.bHovered = true;
	RouteContext.bControlLocked = bUIWantsMouse;
	RouteContext.bInputActive = bInputActive && !bUIWantsMouse;
	RouteContext.bHasActiveCamera = ActiveCamera != nullptr;
	RouteContext.bUseCustomCursor = GameUISystem::Get().WantsCustomCursor();
	InputRouter.Tick(DeltaTime, RouteContext);
}

void FGameViewportClient::LateTick(float DeltaTime)
{
	PlayerController.LateTick(DeltaTime);
}

// 카메라 활성화 여부에 따라 적절한 카메라를 선택하여 렌더러에 넘겨줄 FSceneView 구조체의 내용을 채웁니다.
void FGameViewportClient::BuildSceneView(FSceneView& OutView) const
{
	PlayerController.BuildSceneView(OutView, FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight)), EViewMode::Lit);
}

void FGameViewportClient::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	PlayerController.SetWorld(InWorld);
	if (World)
	{
		APawnActor* Pawn = EnsurePlayerPawn(World);
		PlayerController.SetPlayer(Pawn);
		SetCamera(FindPawnCamera(Pawn));

		if (APlayerStartActor* PlayerStart = World->FindPlayerStart())
		{
			FreeCamera.SetProjectionType(EViewportProjectionType::Perspective);
			FreeCamera.ClearCustomLookDir();
			FreeCamera.SetLocation(PlayerStart->GetActorLocation());
			FreeCamera.SetRotation(FRotator::MakeFromEuler(PlayerStart->GetActorRotation()));
			GetPlayerController().SetFreeCamera(&FreeCamera);
		}

		if (ActiveCamera == nullptr)
		{
			World->SetActiveCamera(&FreeCamera);
		}
	}
}

void FGameViewportClient::SetCamera(UCameraComponent* InCamera)
{
	ActiveCamera = InCamera;
	PlayerController.SetCamera(InCamera);
	if (World)
	{
		World->SetActiveCameraComponent(InCamera);
	}
	if (ActiveCamera)
	{
		ActiveCamera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
}

// 마우스 커서를 표시하고, 입력을 받을지 여부를 토글합니다.
void FGameViewportClient::ToggleInteractionMode()
{
	bInputActive = !bInputActive;
}
