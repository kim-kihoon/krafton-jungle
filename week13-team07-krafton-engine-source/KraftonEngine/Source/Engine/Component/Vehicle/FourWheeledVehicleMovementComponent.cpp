#include "FourWheeledVehicleMovementComponent.h"

#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Camera/CameraComponent.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Input/InputSystem.h"
#include "Runtime/Engine.h"
#include "Runtime/GameEngine.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "Render/Scene/FScene.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>

namespace
{
	float ApproachSteer(float Current, float Target, float MaxDelta)
	{
		if (Current < Target)
		{
			return std::min(Current + MaxDelta, Target);
		}
		return std::max(Current - MaxDelta, Target);
	}
}

UFourWheeledVehicleMovementComponent::UFourWheeledVehicleMovementComponent()
{
	PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
	PrimaryComponentTick.SetEndTickGroup(TG_PostUpdateWork);
}

void UFourWheeledVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterVehicle();
	bPendingCameraActivation = bActivateFollowCamera;
}

void UFourWheeledVehicleMovementComponent::EndPlay()
{
	UnregisterVehicle();
	Super::EndPlay();
}

void UFourWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEnableVehicleSimulation)
	{
		return;
	}

	if (!ChassisComponent)
	{
		RegisterVehicle();
	}

	if (bPendingCameraActivation)
	{
		TryActivateFollowCamera();
	}

	UpdateInputsAndVehicle(DeltaTime);
}

void UFourWheeledVehicleMovementComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(ChassisComponent);
}

void UFourWheeledVehicleMovementComponent::RegisterVehicle()
{
	if (!bEnableVehicleSimulation)
	{
		return;
	}

	ChassisComponent = ResolveChassisComponent();
	UWorld* World = GetWorld();
	if (!ChassisComponent || !World || !World->GetPhysicsScene())
	{
		return;
	}

	World->GetPhysicsScene()->RegisterVehicle(this, BuildRuntimeParams());
}

void UFourWheeledVehicleMovementComponent::UnregisterVehicle()
{
	UWorld* World = GetWorldEvenIfPendingKill();
	if (World && World->GetPhysicsScene())
	{
		World->GetPhysicsScene()->UnregisterVehicle(this);
	}
	ChassisComponent = nullptr;
	VehicleState = FFourWheeledVehicleRuntimeState();
}

void UFourWheeledVehicleMovementComponent::UpdateInputsAndVehicle(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->GetPhysicsScene() || !ChassisComponent)
	{
		return;
	}

	const InputSystem& Input = InputSystem::Get();
	const float RawSteerInput = (Input.GetKey('D') ? 1.0f : 0.0f) + (Input.GetKey('A') ? -1.0f : 0.0f);
	const float MaxSteerDelta = SteerInterpSpeed * DeltaTime;
	SmoothedSteerInput = ApproachSteer(SmoothedSteerInput, RawSteerInput, MaxSteerDelta);

	const FFourWheeledVehicleRuntimeParams Params = BuildRuntimeParams();
	World->GetPhysicsScene()->UpdateVehicle(this, Params);
	World->GetPhysicsScene()->GetVehicleState(this, VehicleState);
}

FFourWheeledVehicleRuntimeParams UFourWheeledVehicleMovementComponent::BuildRuntimeParams() const
{
	FFourWheeledVehicleRuntimeParams Params;
	const InputSystem& Input = InputSystem::Get();
	Params.ChassisComponent = ChassisComponent.Get();
	Params.ThrottleInput = Input.GetKey('W') ? 1.0f : 0.0f;
	Params.BrakeInput = Input.GetKey('S') ? 1.0f : 0.0f;
	Params.SteerInput = SmoothedSteerInput;
	Params.bUseManualGears = bUseManualGears;
	Params.bGearShiftUpPressed = Input.GetKeyDown(VK_RBUTTON);
	Params.bGearShiftDownPressed = Input.GetKeyDown(VK_LBUTTON);
	Params.bGearNeutralPressed = Input.GetKeyDown('N');
	Params.WheelRadius = WheelRadius;
	Params.WheelWidth = WheelWidth;
	Params.ChassisMass = ChassisMass;
	Params.MaxSteerAngleDeg = MaxSteerAngleDeg;
	Params.EnginePeakTorque = EnginePeakTorque;
	Params.EngineMaxRPM = EngineMaxRPM;
	Params.BrakeTorque = BrakeTorque;
	Params.bEnableDownforce = bEnableDownforce;
	Params.DownforceCoeff = DownforceCoeff;
	Params.MaxDownforceMultiplier = MaxDownforceMultiplier;
	Params.bUseRearWheelDrive = bUseRearWheelDrive;
	Params.TireFrictionMultiplier = TireFrictionMultiplier;
	Params.TireLatGripScale = TireLatGripScale;
	Params.CenterOfMassOffset = CenterOfMassOffset;
	Params.WheelCenterOffsets[0] = WheelOffsetLF;
	Params.WheelCenterOffsets[1] = WheelOffsetRF;
	Params.WheelCenterOffsets[2] = WheelOffsetLR;
	Params.WheelCenterOffsets[3] = WheelOffsetRR;
	return Params;
}

UPrimitiveComponent* UFourWheeledVehicleMovementComponent::ResolveChassisComponent() const
{
	if (!bAutoFindChassis)
	{
		return ChassisComponent.Get();
	}

	return FindStaticMeshByPathToken(ChassisMeshToken);
}

UStaticMeshComponent* UFourWheeledVehicleMovementComponent::FindStaticMeshByPathToken(const FString& Token) const
{
	AActor* Owner = GetOwner();
	if (!Owner || Token.empty())
	{
		return nullptr;
	}

	for (UActorComponent* Component : Owner->GetComponents())
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
		if (!MeshComponent)
		{
			continue;
		}

		if (MeshComponent->GetStaticMeshPath().find(Token) != FString::npos)
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

void UFourWheeledVehicleMovementComponent::TryActivateFollowCamera()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UCameraComponent* FollowCamera = Owner->GetComponentByClass<UCameraComponent>();
	if (!FollowCamera)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	APlayerCameraManager* CameraManager = PC->GetPlayerCameraManager();
	if (!CameraManager)
	{
		return;
	}

	CameraManager->RegisterCamera(FollowCamera);
	PC->SetViewTargetWithBlend(Owner, 0.0f);
	CameraManager->SetActiveCamera(FollowCamera);
	CameraManager->Possess(FollowCamera);

	if (GEngine)
	{
		if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			if (FViewport* Viewport = GameEngine->GetStandaloneViewport())
			{
				FollowCamera->OnResize(
					static_cast<int32>(Viewport->GetWidth()),
					static_cast<int32>(Viewport->GetHeight()));
			}
		}
	}

	bPendingCameraActivation = false;
}

void UFourWheeledVehicleMovementComponent::AppendDrivingHud(FScene& Scene) const
{
	if (!bShowDrivingHud)
	{
		return;
	}

	const float SpeedKmh = VehicleState.bValid ? FMath::Abs(VehicleState.ForwardSpeed) * 3.6f : 0.0f;
	const float EngineRPM = VehicleState.bValid ? VehicleState.EngineRPM : 0.0f;
	const float EngineRPMRatio = VehicleState.bValid ? VehicleState.EngineRPMRatio : 0.0f;
	const char* GearLabel = VehicleState.bValid ? VehicleState.GearDisplay.c_str() : "--";
	const bool bRedline = EngineRPMRatio >= 0.88f;

	char LineBuffer[96];
	const float LineStep = 22.0f;
	const float HudScale = 1.25f;
	const float HudX = -260.0f;
	float LineY = -LineStep * 2.0f;

	auto AddLine = [&](const char* Text)
	{
		Scene.AddOverlayText(Text, FVector2(HudX, LineY), HudScale, FScene::EOverlayTextAnchor::RightCenter);
		LineY += LineStep;
	};

	snprintf(LineBuffer, sizeof(LineBuffer), "Gear: %s", GearLabel);
	AddLine(LineBuffer);

	snprintf(LineBuffer, sizeof(LineBuffer), "Speed: %.0f km/h", SpeedKmh);
	AddLine(LineBuffer);

	snprintf(LineBuffer, sizeof(LineBuffer), bRedline ? "RPM: %.0f REDLINE" : "RPM: %.0f", EngineRPM);
	AddLine(LineBuffer);

	snprintf(LineBuffer, sizeof(LineBuffer), bRedline ? "Rev: %.0f%% !" : "Rev: %.0f%%", EngineRPMRatio * 100.0f);
	AddLine(LineBuffer);
}

void UFourWheeledVehicleMovementComponent::AppendDrivingHudForWorld(UWorld& World)
{
	for (AActor* Actor : World.GetActors())
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		const UFourWheeledVehicleMovementComponent* VehicleMovement =
			Actor->GetComponentByClass<UFourWheeledVehicleMovementComponent>();
		if (!VehicleMovement || !VehicleMovement->ShouldShowDrivingHud())
		{
			continue;
		}

		VehicleMovement->AppendDrivingHud(World.GetScene());
		break;
	}
}
