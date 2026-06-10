#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Serialization/Archive.h"


void APawn::BeginPlay()
{
	// InputComponent는 다른 컴포넌트의 BeginPlay보다 먼저 준비한다.
	// LuaBlueprintComponent의 Event InputAction/InputAxis 자동 바인딩은 자기 BeginPlay에서
	// Pawn::GetInputComponent()를 사용하므로, Super::BeginPlay() 전에 생성/Setup되어 있어야 한다.
	if (!InputComponent)
	{
		InputComponent = GetComponentByClass<UInputComponent>();
		if (!InputComponent)
		{
			InputComponent = AddComponent<UInputComponent>();
		}
	}
	if (InputComponent)
	{
		InputComponent->ClearBindings();
	}
	SetupInputComponent();

	Super::BeginPlay();
}

void APawn::ProcessPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	if (InputComponent)
	{
		InputComponent->ProcessInput(Snapshot, DeltaTime);
	}
}

void APawn::PossessedBy(APlayerController* PC)
{
	Controller = PC;
	ControlRotation = bOverrideInitialControlRotation ? FRotator(InitialControlRotation) : GetActorRotation();

	// 자기 첫 카메라 컴포넌트를 ActiveCamera로 — PIE 시작 시 시점이 Pawn 기준이 되도록.
	// 카메라 컴포넌트가 없으면 no-op (CameraManager의 기존 흐름이 다른 카메라를 선택).
	// E.2/2: PC->GetPlayerCameraManager() 경로 사용.
	if (UCameraComponent* MyCamera = GetComponentByClass<UCameraComponent>())
	{
		if (PC)
		{
			if (APlayerCameraManager* Mgr = PC->GetPlayerCameraManager())
			{
				Mgr->SetActiveCamera(MyCamera);
				Mgr->Possess(MyCamera);
			}
		}
	}
}

void APawn::UnPossessed()
{
	Controller = nullptr;

}

// 직렬화 오버라이드 제거: bAutoPossessPlayer / bUseControllerRotation* 4개는 모두
// UPROPERTY(Save) 라 AActor 가 상속시킨 반사 직렬화(ShouldReflectProperties==true)로 자동 처리된다.
// (기존 수동 Ar<< 는 반사와 중복이었음.)

void APawn::ApplyControllerRotationToRoot()
{
	if (!bUseControllerRotationPitch && !bUseControllerRotationYaw && !bUseControllerRotationRoll) return;

	USceneComponent* Root = GetRootComponent();
	if (!Root) return;

	FRotator R = Root->GetRelativeRotation();
	if (bUseControllerRotationYaw)   R.Yaw   = ControlRotation.Yaw;
	if (bUseControllerRotationPitch) R.Pitch = ControlRotation.Pitch;
	if (bUseControllerRotationRoll)  R.Roll  = ControlRotation.Roll;
	Root->SetRelativeRotation(R);
}
