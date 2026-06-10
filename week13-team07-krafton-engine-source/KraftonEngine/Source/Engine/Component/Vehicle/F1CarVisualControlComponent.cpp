#include "F1CarVisualControlComponent.h"

#include "Component/Vehicle/FourWheeledVehicleMovementComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Object/Reflection/ObjectFactory.h"

#include <algorithm>
#include <cmath>

void UF1CarVisualControlComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeVisuals();
}

void UF1CarVisualControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEnableRuntimeControls)
	{
		return;
	}

	if (!IsInitialized())
	{
		InitializeVisuals();
	}

	UpdateInputAndMotion(DeltaTime);
	ApplyVisualRotations();
}

void UF1CarVisualControlComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(VehicleRoot);
	Collector.AddReferencedObject(SteeringWheelPivot);
	Collector.AddReferencedObject(SteeringWheelMesh);
	for (FWheelVisual& Wheel : Wheels)
	{
		Collector.AddReferencedObject(Wheel.SteerPivot);
		Collector.AddReferencedObject(Wheel.SpinPivot);
		Collector.AddReferencedObject(Wheel.WheelMesh);
		Collector.AddReferencedObject(Wheel.CaliperMesh);
	}
}

void UF1CarVisualControlComponent::InitializeVisuals()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	VehicleRoot = Owner->GetRootComponent();
	if (!VehicleRoot)
	{
		return;
	}

	SteeringWheelMesh = FindMeshByPathToken("mcl39-steering-wheel");
	SetupSteeringWheel(SteeringWheelMesh);

	Wheels.clear();
	SetupWheel(FindMeshByPathToken("mcl39-wheelspin-lf"), FindMeshByPathToken("mcl39-caliper-lf"), true, 1.0f);
	SetupWheel(FindMeshByPathToken("mcl39-wheelspin-rf"), FindMeshByPathToken("mcl39-caliper-rf"), true, 1.0f);
	SetupWheel(FindMeshByPathToken("mcl39-wheelspin-lr"), nullptr, false, 1.0f);
	SetupWheel(FindMeshByPathToken("mcl39-wheelspin-rr"), nullptr, false, 1.0f);
}

void UF1CarVisualControlComponent::SetupSteeringWheel(UStaticMeshComponent* Mesh)
{
	AActor* Owner = GetOwner();
	if (!Owner || !VehicleRoot || !Mesh || Mesh->GetParent() == SteeringWheelPivot)
	{
		return;
	}

	const FVector MeshLocation = Mesh->GetRelativeLocation();
	const FVector PivotLocation = EstimateMeshPivotLocation(Mesh);
	SteeringWheelPivot = CreatePivot(Owner, VehicleRoot, PivotLocation);
	if (!SteeringWheelPivot)
	{
		return;
	}

	Mesh->AttachToComponent(SteeringWheelPivot);
	Mesh->SetRelativeLocation(MeshLocation - PivotLocation);
	Mesh->SetRelativeRotation(FRotator::ZeroRotator);
}

void UF1CarVisualControlComponent::SetupWheel(UStaticMeshComponent* WheelMesh, UStaticMeshComponent* CaliperMesh, bool bFront, float SpinSign)
{
	AActor* Owner = GetOwner();
	if (!Owner || !VehicleRoot || !WheelMesh)
	{
		return;
	}

	const FVector WheelLocation = WheelMesh->GetRelativeLocation();
	const FVector PivotLocation = EstimateMeshPivotLocation(WheelMesh);
	USceneComponent* SteerPivot = CreatePivot(Owner, VehicleRoot, PivotLocation);
	USceneComponent* SpinPivot = bFront ? CreatePivot(Owner, SteerPivot, FVector::ZeroVector) : CreatePivot(Owner, VehicleRoot, PivotLocation);
	if (!SteerPivot || !SpinPivot)
	{
		return;
	}

	if (!bFront)
	{
		SteerPivot = SpinPivot;
	}

	if (CaliperMesh && bFront)
	{
		const FVector CaliperLocation = CaliperMesh->GetRelativeLocation();
		CaliperMesh->AttachToComponent(SteerPivot);
		CaliperMesh->SetRelativeLocation(CaliperLocation - PivotLocation);
		CaliperMesh->SetRelativeRotation(FRotator::ZeroRotator);
	}

	const FVector ForwardAxis = NormalizeOrFallback(VehicleForwardAxis, FVector(0.0f, -1.0f, 0.0f));
	float CamberRollDeg = 0.0f;
	if (bFront && FrontWheelCamberDeg > 0.0f)
	{
		const bool bLeftSide = PathContains(WheelMesh, "-lf") || PathContains(WheelMesh, "-lr");
		CamberRollDeg = bLeftSide ? -FrontWheelCamberDeg : FrontWheelCamberDeg;
	}

	const FQuat CamberQuat = FQuat::FromAxisAngle(ForwardAxis, CamberRollDeg * FMath::DegToRad);
	WheelMesh->AttachToComponent(SpinPivot);
	WheelMesh->SetRelativeLocation(WheelLocation - PivotLocation);
	WheelMesh->SetRelativeRotation(FRotator::ZeroRotator);

	FWheelVisual Wheel;
	Wheel.SteerPivot = SteerPivot;
	Wheel.SpinPivot = SpinPivot;
	Wheel.WheelMesh = WheelMesh;
	Wheel.CaliperMesh = CaliperMesh;
	Wheel.SpinSign = SpinSign;
	Wheel.bFront = bFront;
	Wheel.SpinAxisLocal = CamberQuat.RotateVector(NormalizeOrFallback(WheelSpinAxis, FVector::XAxisVector));
	if (Wheel.SpinAxisLocal.IsNearlyZero())
	{
		Wheel.SpinAxisLocal = FVector::XAxisVector;
	}
	else
	{
		Wheel.SpinAxisLocal = Wheel.SpinAxisLocal.GetSafeNormal();
	}
	Wheels.push_back(Wheel);
}

void UF1CarVisualControlComponent::UpdateInputAndMotion(float DeltaTime)
{
	const InputSystem& Input = InputSystem::Get();

	float SteerInput = 0.0f;
	if (Input.GetKey('A'))
	{
		SteerInput -= 1.0f;
	}
	if (Input.GetKey('D'))
	{
		SteerInput += 1.0f;
	}

	float DriveInput = 0.0f;
	if (Input.GetKey('W'))
	{
		DriveInput += 1.0f;
	}
	if (Input.GetKey('S'))
	{
		DriveInput -= 1.0f;
	}

	const float TargetSteerDeg = SteerInput * MaxSteerAngleDeg;
	if (!bMoveVehicleRoot)
	{
		if (AActor* Owner = GetOwner())
		{
			if (const UFourWheeledVehicleMovementComponent* VehicleMovement =
				Owner->GetComponentByClass<UFourWheeledVehicleMovementComponent>())
			{
				const FFourWheeledVehicleRuntimeState& State = VehicleMovement->GetVehicleState();
				if (State.bValid)
				{
					CurrentSpeed = State.ForwardSpeed;
				}
			}
		}
	}

	const float MaxSteerDelta = SteerInterpSpeed * MaxSteerAngleDeg * DeltaTime;
	CurrentSteerDeg = Approach(CurrentSteerDeg, TargetSteerDeg, MaxSteerDelta);

	if (bMoveVehicleRoot)
	{
		if (DriveInput > 0.0f)
		{
			CurrentSpeed += Acceleration * DriveInput * DeltaTime;
		}
		else if (DriveInput < 0.0f)
		{
			CurrentSpeed += BrakeDeceleration * DriveInput * DeltaTime;
		}
		else
		{
			CurrentSpeed = Approach(CurrentSpeed, 0.0f, DragDeceleration * DeltaTime);
		}

		CurrentSpeed = FMath::Clamp(CurrentSpeed, -MaxSpeed * 0.35f, MaxSpeed);
	}

	if (bMoveVehicleRoot && VehicleRoot)
	{
		const float SpeedAlpha = MaxSpeed > 0.0f ? FMath::Clamp(CurrentSpeed / MaxSpeed, -1.0f, 1.0f) : 0.0f;
		const float YawDelta = CurrentSteerDeg * SpeedAlpha * DeltaTime * 0.8f;
		const FVector YawAxis = NormalizeOrFallback(VehicleYawAxis, FVector::ZAxisVector);
		const FVector ForwardAxis = NormalizeOrFallback(VehicleForwardAxis, FVector::ForwardVector);
		VehicleRoot->AddLocalRotation(FQuat::FromAxisAngle(YawAxis, YawDelta * FMath::DegToRad));
		VehicleRoot->MoveLocal(ForwardAxis * (CurrentSpeed * DeltaTime));
	}

	if (!bMoveVehicleRoot || CurrentSpeed != 0.0f)
	{
		const float Radius = std::max(WheelRadius, 0.01f);
		WheelSpinDeg += (CurrentSpeed * DeltaTime / Radius) * FMath::RadToDeg;
	}
}

void UF1CarVisualControlComponent::ApplyVisualRotations()
{
	if (SteeringWheelPivot)
	{
		const FVector Axis = NormalizeOrFallback(SteeringWheelAxis, FVector::XAxisVector);
		const FQuat Rotation = FQuat::FromAxisAngle(Axis, CurrentSteerDeg * SteeringWheelRatio * FMath::DegToRad);
		SteeringWheelPivot->SetRelativeRotation(Rotation);
	}

	const FVector SteerAxis = NormalizeOrFallback(FrontSteerAxis, FVector::ZAxisVector);
	for (FWheelVisual& Wheel : Wheels)
	{
		if (Wheel.SteerPivot && Wheel.bFront)
		{
			Wheel.SteerPivot->SetRelativeRotation(FQuat::FromAxisAngle(SteerAxis, CurrentSteerDeg * FMath::DegToRad));
		}

		if (Wheel.SpinPivot)
		{
			const FVector SpinAxis = Wheel.SpinAxisLocal.IsNearlyZero()
				? NormalizeOrFallback(WheelSpinAxis, FVector::XAxisVector)
				: Wheel.SpinAxisLocal;
			const FQuat SpinRotation = FQuat::FromAxisAngle(SpinAxis, WheelSpinDeg * Wheel.SpinSign * FMath::DegToRad);
			Wheel.SpinPivot->SetRelativeRotation(SpinRotation);
		}
	}
}

UStaticMeshComponent* UF1CarVisualControlComponent::FindMeshByPathToken(const char* Token) const
{
	AActor* Owner = GetOwner();
	if (!Owner || !Token)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Owner->GetComponents())
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
		if (PathContains(MeshComponent, Token))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

bool UF1CarVisualControlComponent::IsInitialized() const
{
	return VehicleRoot && SteeringWheelPivot && !Wheels.empty();
}

USceneComponent* UF1CarVisualControlComponent::CreatePivot(AActor* Owner, USceneComponent* Parent, const FVector& LocalLocation)
{
	if (!Owner || !Parent)
	{
		return nullptr;
	}

	USceneComponent* Pivot = Owner->AddComponent<USceneComponent>();
	Pivot->AttachToComponent(Parent);
	Pivot->SetRelativeLocation(LocalLocation);
	Pivot->SetRelativeRotation(FRotator::ZeroRotator);
	Pivot->SetRelativeScale(FVector::OneVector);
	Pivot->SetHiddenInComponentTree(true);
	return Pivot;
}

FVector UF1CarVisualControlComponent::EstimateMeshPivotLocation(const UStaticMeshComponent* Component)
{
	if (!Component)
	{
		return FVector::ZeroVector;
	}

	FVector Pivot = Component->GetRelativeLocation();
	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
	if (MeshAsset)
	{
		if (!MeshAsset->bBoundsValid)
		{
			MeshAsset->CacheBounds();
		}
		if (MeshAsset->bBoundsValid)
		{
			Pivot += MeshAsset->BoundsCenter;
		}
	}

	return Pivot;
}

bool UF1CarVisualControlComponent::PathContains(const UStaticMeshComponent* Component, const char* Token)
{
	if (!Component || !Token)
	{
		return false;
	}

	const FString& Path = Component->GetStaticMeshPath();
	return Path.find(Token) != FString::npos;
}

float UF1CarVisualControlComponent::Approach(float Current, float Target, float MaxDelta)
{
	if (Current < Target)
	{
		return std::min(Current + MaxDelta, Target);
	}
	return std::max(Current - MaxDelta, Target);
}

FVector UF1CarVisualControlComponent::NormalizeOrFallback(const FVector& Value, const FVector& Fallback)
{
	const FVector Normalized = Value.GetSafeNormal();
	return Normalized.IsNearlyZero() ? Fallback : Normalized;
}
