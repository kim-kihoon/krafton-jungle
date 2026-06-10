#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Physics/IPhysicsScene.h"

#include "Source/Engine/Component/Vehicle/FourWheeledVehicleMovementComponent.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

UCLASS()
class UFourWheeledVehicleMovementComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UFourWheeledVehicleMovementComponent();

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	const FFourWheeledVehicleRuntimeState& GetVehicleState() const { return VehicleState; }
	bool ShouldShowDrivingHud() const { return bShowDrivingHud; }
	void AppendDrivingHud(class FScene& Scene) const;
	static void AppendDrivingHudForWorld(class UWorld& World);

private:
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Enable Vehicle Simulation")
	bool bEnableVehicleSimulation = true;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Chassis Mesh Token")
	FString ChassisMeshToken = "mcl39-chassis";
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Auto Find Chassis")
	bool bAutoFindChassis = true;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel Radius", Min=0.01f, Max=10.0f, Speed=0.01f)
	float WheelRadius = 0.36f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel Width", Min=0.01f, Max=10.0f, Speed=0.01f)
	float WheelWidth = 0.30f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Chassis Mass", Min=1.0f, Max=10000.0f, Speed=1.0f)
	float ChassisMass = 800.0f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Max Steer Angle", Min=0.0f, Max=89.0f, Speed=0.1f)
	float MaxSteerAngleDeg = 28.0f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Steer Interp Speed", Min=0.0f, Max=100.0f, Speed=0.1f)
	float SteerInterpSpeed = 8.0f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Manual Gears")
	bool bUseManualGears = true;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Activate Follow Camera On Begin Play")
	bool bActivateFollowCamera = true;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Show Driving HUD")
	bool bShowDrivingHud = true;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Engine Peak Torque", Min=0.0f, Max=100000.0f, Speed=10.0f)
	float EnginePeakTorque = 750.0f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Engine Max RPM", Min=1000.0f, Max=25000.0f, Speed=100.0f)
	float EngineMaxRPM = 15000.0f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Brake Torque", Min=0.0f, Max=100000.0f, Speed=10.0f)
	float BrakeTorque = 1500.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Aero", DisplayName="Enable Simple Downforce")
	bool bEnableDownforce = true;
	UPROPERTY(Edit, Save, Category="Vehicle|Aero", DisplayName="Downforce Coeff (N per (m/s)^2)", Min=0.0f, Max=50.0f, Speed=0.1f)
	float DownforceCoeff = 5.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Aero", DisplayName="Max Downforce Multiplier (x weight)", Min=0.0f, Max=10.0f, Speed=0.1f)
	float MaxDownforceMultiplier = 3.5f;
	UPROPERTY(Edit, Save, Category="Vehicle|Tires", DisplayName="Rear Wheel Drive")
	bool bUseRearWheelDrive = true;
	UPROPERTY(Edit, Save, Category="Vehicle|Tires", DisplayName="Tire Friction Multiplier", Min=0.5f, Max=3.0f, Speed=0.05f)
	float TireFrictionMultiplier = 1.85f;
	UPROPERTY(Edit, Save, Category="Vehicle|Tires", DisplayName="Lateral Grip Scale", Min=1.0f, Max=6.0f, Speed=0.1f)
	float TireLatGripScale = 2.8f;
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Center Of Mass Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector CenterOfMassOffset = FVector(0.0f, 0.0f, -0.35f);
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel LF Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WheelOffsetLF = FVector(-0.80f, -1.45f, -0.35f);
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel RF Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WheelOffsetRF = FVector(0.80f, -1.45f, -0.35f);
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel LR Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WheelOffsetLR = FVector(-0.80f, 1.45f, -0.35f);
	UPROPERTY(Edit, Save, Category="Vehicle", DisplayName="Wheel RR Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.01f)
	FVector WheelOffsetRR = FVector(0.80f, 1.45f, -0.35f);

	TObjectPtr<UPrimitiveComponent> ChassisComponent = nullptr;
	FFourWheeledVehicleRuntimeState VehicleState;
	float SmoothedSteerInput = 0.0f;
	bool bPendingCameraActivation = false;

	void RegisterVehicle();
	void UnregisterVehicle();
	void UpdateInputsAndVehicle(float DeltaTime);
	FFourWheeledVehicleRuntimeParams BuildRuntimeParams() const;
	UPrimitiveComponent* ResolveChassisComponent() const;
	UStaticMeshComponent* FindStaticMeshByPathToken(const FString& Token) const;
	void TryActivateFollowCamera();
};
