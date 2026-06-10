#pragma once

#include "Component/ActorComponent.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Vehicle/VehicleWheelPoseComponent.generated.h"

class UWheeledVehicleMovementComponent;
class USceneComponent;
class USkeletalMeshComponent;
struct FVehicleSnapshot;

/**
 * Applies vehicle wheel visual poses after physics snapshots have been consumed.
 *
 * This component intentionally owns the visual side of wheels. Vehicle movement no longer
 * guesses child components or writes wheel transforms directly. Skeletal wheels are driven
 * through FVehicleWheelSetup::BoneName, and static/scene-component wheels are driven only
 * through stable FVehicleWheelSetup::VisualComponent object references.
 */
UCLASS()
class UVehicleWheelPoseComponent : public UActorComponent
{
public:
    GENERATED_BODY()

    UVehicleWheelPoseComponent();
    ~UVehicleWheelPoseComponent() override = default;

    void BeginPlay() override;
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    UFUNCTION(Callable, Category="Vehicle|Visual")
    void SetVehicleMovement(UWheeledVehicleMovementComponent* InMovement);
    UFUNCTION(Pure, Category="Vehicle|Visual")
    UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement.Get(); }

    UFUNCTION(Callable, Category="Vehicle|Visual")
    void SetSkeletalMeshComponent(USkeletalMeshComponent* InMesh);
    UFUNCTION(Pure, Category="Vehicle|Visual")
    USkeletalMeshComponent* GetSkeletalMeshComponent() const { return Mesh.Get(); }

private:
    struct FSceneWheelVisualPoseCache
    {
        TWeakObjectPtr<USceneComponent> VisualComponent;
        FVector CenterOffsetLocal = FVector::ZeroVector;
        FVector RestCenterLocal = FVector::ZeroVector;
        FQuat RestRotationLocal = FQuat::Identity;
    };

    FSceneWheelVisualPoseCache* FindOrCreateSceneWheelPoseCache(
        USceneComponent* VisualComponent,
        const FVehicleSnapshot& VehicleSnapshot
    );
    void PurgeInvalidSceneWheelPoseCaches();

private:
    TWeakObjectPtr<UWheeledVehicleMovementComponent> VehicleMovement;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TArray<FSceneWheelVisualPoseCache> SceneWheelPoseCaches;
};
