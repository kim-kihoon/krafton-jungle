#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Object/FName.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Physics/Vehicle/VehicleTypes.h"

#include "Source/Engine/Component/Movement/WheeledVehicleMovementComponent.generated.h"

struct FPhysicsWorldSnapshot;
class USkinnedMeshComponent;
class USceneComponent;

UENUM()
enum class EWheelPositionSource : uint8
{
    FromBone = 0,
    Manual = 1,
    FromVisualComponent = 2
};

USTRUCT()
struct FVehicleWheelData
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Wheel Data|Wheel", DisplayName="Radius", Speed=0.01f)
    float Radius = 0.35f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Wheel", DisplayName="Width", Speed=0.01f)
    float Width = 0.25f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Wheel", DisplayName="Mass", Speed=0.1f)
    float Mass = 20.0f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Wheel", DisplayName="Tire Friction", Speed=0.05f)
    float TireFriction = 1.5f;

    UPROPERTY(Edit, Save, Category="Wheel Data|Suspension", DisplayName="Suspension Max Compression", Speed=0.01f)
    float SuspensionMaxCompression = 0.30f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Suspension", DisplayName="Suspension Max Droop", Speed=0.01f)
    float SuspensionMaxDroop = 0.10f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Suspension", DisplayName="Spring Strength", Speed=100.0f)
    float SuspensionSpringStrength = 35000.0f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Suspension", DisplayName="Spring Damper Rate", Speed=10.0f)
    float SuspensionSpringDamperRate = 4500.0f;

    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Affected By Steering")
    bool bAffectedBySteering = false;
    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Affected By Engine")
    bool bAffectedByEngine = false;
    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Affected By Brake")
    bool bAffectedByBrake = true;
    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Affected By Handbrake")
    bool bAffectedByHandbrake = false;

    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Max Steer Angle (deg)", Speed=0.5f)
    float MaxSteerAngleDegrees = 0.0f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Max Brake Torque", Speed=10.0f)
    float MaxBrakeTorque = 1500.0f;
    UPROPERTY(Edit, Save, Category="Wheel Data|Vehicle Input", DisplayName="Max Handbrake Torque", Speed=10.0f)
    float MaxHandbrakeTorque = 0.0f;
};

USTRUCT()
struct FVehicleWheelSetup
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Wheel Name")
    FName WheelName = FName::None;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Bone Name", BonePicker=true)
    FName BoneName = FName::None;

    // Stable component reference used by editor save/load and PIE duplication.
    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Visual Component", Type=ObjectRef, AllowedClass=USceneComponent)
    TObjectPtr<USceneComponent> VisualComponent = nullptr;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Position Source", Enum=EWheelPositionSource)
    EWheelPositionSource PositionSource = EWheelPositionSource::FromBone;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Manual Local Position", Type=Vec3, Speed=0.01f)
    FVector ManualLocalPosition = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Additional Offset", Type=Vec3, Speed=0.01f)
    FVector AdditionalOffset = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Use Bone Influence Surface Center")
    bool bUseBoneInfluenceSurfaceCenter = false;

    UPROPERTY(Edit, Save, Category="Wheel", DisplayName="Wheel Data", Type=Struct, Struct=FVehicleWheelData)
    FVehicleWheelData WheelData;

};

/**
 * Backend-neutral wheeled vehicle movement component.
 *
 * This component owns gameplay input and vehicle simulation state only. It builds an
 * FVehicleDesc and talks to IPhysicsScene; it does not know PhysX public types and it
 * does not guess or directly drive wheel visuals. Wheel visuals are handled by
 * UVehicleWheelPoseComponent using WheelSetups + LastSnapshot.
 */
UCLASS()
class UWheeledVehicleMovementComponent : public UMovementComponent
{
public:
    GENERATED_BODY()

    UWheeledVehicleMovementComponent() = default;
    ~UWheeledVehicleMovementComponent() override = default;

    void BeginPlay() override;
    void EndPlay() override;
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
    void PostDuplicate() override;
    void AddReferencedObjects(FReferenceCollector& Collector) override;

    UFUNCTION(Callable, Category="Vehicle|Input")
    void SetThrottleInput(float InThrottle);
    UFUNCTION(Callable, Category="Vehicle|Input")
    void SetBrakeInput(float InBrake);
    UFUNCTION(Callable, Category="Vehicle|Input")
    void SetSteeringInput(float InSteering);
    UFUNCTION(Callable, Category="Vehicle|Input")
    void SetHandbrakeInput(float InHandbrake);

    UFUNCTION(Callable, Category="Vehicle")
    void ResetVehicle();
    UFUNCTION(Pure, Category="Vehicle")
    float GetForwardSpeed() const;

    UFUNCTION(Callable, Category="Vehicle|Wheel Setup")
    bool AutoGenerateWheelSetupsFromSkeleton();
    UFUNCTION(Callable, Category="Vehicle|Wheel Setup")
    bool AutoGenerateWheelSetupsFromStaticMeshComponents();
    UFUNCTION(Callable, Category="Vehicle|Wheel Setup")
    int32 RefreshWheelLocalPositionsFromBones();

    UFUNCTION(Pure, Category="Vehicle")
    bool IsVehicleCreated() const { return VehicleHandle.IsValid(); }
    FVehicleHandle GetVehicleHandle() const { return VehicleHandle; }

    UFUNCTION(Callable, Category="Vehicle|Debug")
    void SetDebugSelectedWheelIndex(int32 WheelIndex);
    UFUNCTION(Pure, Category="Vehicle|Debug")
    int32 GetDebugSelectedWheelIndex() const { return DebugSelectedWheelIndex; }
    UFUNCTION(Callable, Category="Vehicle|Debug")
    void SetDebugSelectedWheelEnabled(bool bEnabled) { bDebugDrawSelectedWheel = bEnabled; }
    UFUNCTION(Pure, Category="Vehicle|Debug")
    bool IsDebugSelectedWheelEnabled() const { return bDebugDrawSelectedWheel; }
    UFUNCTION(Callable, Category="Vehicle|Debug")
    void DrawSelectedWheelDebug() const;
    UFUNCTION(Callable, Category="Vehicle|Debug")
    void DrawWheelDebug(int32 WheelIndex) const;

    const FVehicleSnapshot* GetLastVehicleSnapshot() const { return bHasLastSnapshot ? &LastSnapshot : nullptr; }
    const TArray<FVehicleWheelSetup>& GetWheelSetups() const { return WheelSetups; }
    const FVehicleWheelSetup* FindWheelSetup(const FName& WheelName) const;
    USceneComponent* ResolveWheelVisualComponent(const FVehicleWheelSetup& Setup) const;

    FVector ResolveWheelLocalPosition(const FVehicleWheelSetup& Setup) const;
    bool ValidateWheelSetups(TArray<FString>& OutMessages) const;

protected:
    FVector ComputeAutoChassisCollisionOffset(const TArray<FVector>& ResolvedWheelPositions) const;
    FVector ResolveChassisCollisionOffset(const TArray<FVector>& ResolvedWheelPositions) const;
    FQuat ResolveVisualToSimulationRotation(const TArray<FVector>& ResolvedWheelPositions) const;
    uint32 BuildDrivableSurfaceMask() const;
    FVehicleDesc BuildVehicleDesc() const;
    void EnsureDefaultWheelSetups();
    void RegisterPhysicsSnapshotReceiver();
    void UnregisterPhysicsSnapshotReceiver();
    void RecreateVehicleSimulation();
    void ConsumeVehicleSnapshot(const FPhysicsWorldSnapshot& Snapshot);
    void ApplyVehicleSnapshot(const FVehicleSnapshot& Snapshot);
    USceneComponent* ResolveOwnedSceneComponent(USceneComponent* Component) const;
    USceneComponent* FindAutoStaticChassisComponent() const;
    USceneComponent* ResolveExplicitChassisComponent() const;
    USceneComponent* ResolveVehicleChassisComponent() const;
    USceneComponent* ResolveVehicleSimulationComponent() const;
    USkinnedMeshComponent* FindWheelSetupSkinnedMeshComponent() const;
    USceneComponent* FindWheelVisualSceneComponent(const FVehicleWheelSetup& Setup) const;
    bool TryResolveVisualComponentLocalPosition(const FVehicleWheelSetup& Setup, FVector& OutLocalPosition) const;
    bool TryResolveBoneLocalPosition(const FName& BoneName, bool bUseInfluenceSurfaceCenter, FVector& OutLocalPosition) const;
    void ClampWheelData(FVehicleWheelData& WheelData) const;
    void ClampWheelSetups();

protected:
    // Stable component reference used by editor save/load and PIE duplication.
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Chassis Component", Type=ObjectRef, AllowedClass=USceneComponent)
    TObjectPtr<USceneComponent> ChassisComponent = nullptr;

    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Chassis Half Extents", Type=Vec3, Speed=0.05f)
    FVector ChassisHalfExtents = FVector(1.25f, 0.6f, 0.35f);
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Chassis Mass", Speed=1.0f)
    float ChassisMass = 1200.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Auto Fit Collision Offset From Wheels")
    bool bAutoFitChassisCollisionOffset = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Chassis Collision Offset", Type=Vec3, Speed=0.01f)
    FVector ChassisCollisionOffset = FVector::ZeroVector;
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Chassis Ground Clearance", Speed=0.01f)
    float ChassisGroundClearance = 0.08f;
    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Center Of Mass Offset", Type=Vec3, Speed=0.01f)
    FVector ChassisCenterOfMassOffset = FVector(0.0f, 0.0f, -0.25f);

    UPROPERTY(Edit, Save, Category="Vehicle|Chassis", DisplayName="Auto Align Simulation Forward From Wheel Layout")
    bool bAutoAlignSimulationForwardFromWheelLayout = true;

    UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Engine Peak Torque", Speed=1.0f)
    float EnginePeakTorque = 500.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Engine Max Omega", Speed=1.0f)
    float EngineMaxOmega = 600.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Clutch Strength", Speed=0.1f)
    float ClutchStrength = 10.0f;

    UPROPERTY(Edit, Save, Category="Vehicle|Transmission", DisplayName="Enable Reverse Gear")
    bool bEnableReverseGear = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Transmission", DisplayName="Reverse Gear Switch Speed", Speed=0.05f)
    float ReverseGearSwitchSpeed = 0.5f;

    UPROPERTY(Edit, Save, Category="Vehicle|Tire", DisplayName="Fallback Tire Friction", Speed=0.05f)
    float TireFriction = 1.5f;

    UPROPERTY(Edit, Save, Category="Vehicle|Collision", DisplayName="Wheels Trace World Static")
    bool bWheelsTraceWorldStatic = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Collision", DisplayName="Wheels Trace World Dynamic")
    bool bWheelsTraceWorldDynamic = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Collision", DisplayName="Wheels Trace Pawn/Skeletal")
    bool bWheelsTracePawn = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Collision", DisplayName="Wheels Trace Projectile")
    bool bWheelsTraceProjectile = false;


    UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Wheel Setups", Type=Array, Struct=FVehicleWheelSetup)
    TArray<FVehicleWheelSetup> WheelSetups;

    FVehicleHandle     VehicleHandle;
    FVehicleInputState CurrentInput;
    FVehicleSnapshot   LastSnapshot;
    bool               bHasLastSnapshot = false;
    bool               bVehicleSimulationStarted = false;
    bool               bDebugDrawSelectedWheel = false;
    int32              DebugSelectedWheelIndex = -1;
    uint64             PhysicsSnapshotReceiverHandle = 0;
};
