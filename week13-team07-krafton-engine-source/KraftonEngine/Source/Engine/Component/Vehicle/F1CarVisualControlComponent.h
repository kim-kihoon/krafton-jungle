#pragma once

#include "Component/ActorComponent.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Vehicle/F1CarVisualControlComponent.generated.h"

class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class UF1CarVisualControlComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	struct FWheelVisual
	{
		TObjectPtr<USceneComponent> SteerPivot = nullptr;
		TObjectPtr<USceneComponent> SpinPivot = nullptr;
		TObjectPtr<UStaticMeshComponent> WheelMesh = nullptr;
		TObjectPtr<UStaticMeshComponent> CaliperMesh = nullptr;
		float SpinSign = 1.0f;
		bool bFront = false;
		FVector SpinAxisLocal = FVector(1.0f, 0.0f, 0.0f);
	};

	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Enable Runtime Controls")
	bool bEnableRuntimeControls = true;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Max Steer Angle", Min=0.0f, Max=89.0f, Speed=0.1f)
	float MaxSteerAngleDeg = 28.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Steer Interp Speed", Min=0.0f, Max=100.0f, Speed=0.1f)
	float SteerInterpSpeed = 8.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Steering Wheel Ratio", Min=0.0f, Max=100.0f, Speed=0.1f)
	float SteeringWheelRatio = 7.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Max Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float MaxSpeed = 35.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Acceleration", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float Acceleration = 18.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Brake Deceleration", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float BrakeDeceleration = 35.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Drag Deceleration", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float DragDeceleration = 8.0f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Wheel Radius", Min=0.01f, Max=10.0f, Speed=0.01f)
	float WheelRadius = 0.36f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Vehicle Forward Axis", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector VehicleForwardAxis = FVector(0.0f, -1.0f, 0.0f);
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Vehicle Yaw Axis", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector VehicleYawAxis = FVector(0.0f, 0.0f, 1.0f);
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Front Steer Axis", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector FrontSteerAxis = FVector(0.0f, 0.0f, 1.0f);
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Wheel Spin Axis", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector WheelSpinAxis = FVector(1.0f, 0.0f, 0.0f);
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Front Wheel Camber Deg", Min=0.0f, Max=15.0f, Speed=0.1f)
	float FrontWheelCamberDeg = 2.5f;
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Steering Wheel Axis", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector SteeringWheelAxis = FVector(0.0f, 0.0f, 1.0f);
	UPROPERTY(Edit, Save, Category="F1 Visual", DisplayName="Move Vehicle Root")
	bool bMoveVehicleRoot = true;

	TObjectPtr<USceneComponent> VehicleRoot = nullptr;
	TObjectPtr<USceneComponent> SteeringWheelPivot = nullptr;
	TObjectPtr<UStaticMeshComponent> SteeringWheelMesh = nullptr;
	TArray<FWheelVisual> Wheels;

	float CurrentSteerDeg = 0.0f;
	float CurrentSpeed = 0.0f;
	float WheelSpinDeg = 0.0f;

	void InitializeVisuals();
	void SetupSteeringWheel(UStaticMeshComponent* Mesh);
	void SetupWheel(UStaticMeshComponent* WheelMesh, UStaticMeshComponent* CaliperMesh, bool bFront, float SpinSign);
	void UpdateInputAndMotion(float DeltaTime);
	void ApplyVisualRotations();

	UStaticMeshComponent* FindMeshByPathToken(const char* Token) const;
	bool IsInitialized() const;
	static USceneComponent* CreatePivot(AActor* Owner, USceneComponent* Parent, const FVector& LocalLocation);
	static FVector EstimateMeshPivotLocation(const UStaticMeshComponent* Component);
	static bool PathContains(const UStaticMeshComponent* Component, const char* Token);
	static float Approach(float Current, float Target, float MaxDelta);
	static FVector NormalizeOrFallback(const FVector& Value, const FVector& Fallback);
};
