#include "Component/Vehicle/VehicleWheelPoseComponent.h"

#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Physics/Vehicle/VehicleTypes.h"

#include <cfloat>
#include <cmath>

namespace
{
    bool BuildBaseComponentSpaceMatrices(USkeletalMeshComponent* Mesh, TArray<FMatrix>& OutGlobals)
    {
        OutGlobals.clear();
        if (!Mesh)
        {
            return false;
        }

        USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMesh();
        FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
        if (!Asset || Asset->Bones.empty())
        {
            return false;
        }

        const int32 BoneCount = static_cast<int32>(Asset->Bones.size());
        OutGlobals.resize(BoneCount);

        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            const FMatrix LocalMatrix = Mesh->GetBoneEditBaseLocalTransformByIndex(BoneIndex).ToMatrix();
            const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
            OutGlobals[BoneIndex] = ParentIndex >= 0 && ParentIndex < BoneIndex
                ? LocalMatrix * OutGlobals[ParentIndex]
                : LocalMatrix;
        }

        return true;
    }

    FVector SafeNormalized(const FVector& Axis, const FVector& Fallback)
    {
        const float Length = Axis.Length();
        if (Length <= 1.e-6f)
        {
            return Fallback;
        }

        return Axis / Length;
    }

    FVector MakeStableAxisSign(FVector Axis)
    {
        const float AbsX = std::fabs(Axis.X);
        const float AbsY = std::fabs(Axis.Y);
        const float AbsZ = std::fabs(Axis.Z);

        if (AbsX >= AbsY && AbsX >= AbsZ)
        {
            if (Axis.X < 0.0f)
            {
                Axis *= -1.0f;
            }
        }
        else if (AbsY >= AbsX && AbsY >= AbsZ)
        {
            if (Axis.Y < 0.0f)
            {
                Axis *= -1.0f;
            }
        }
        else if (Axis.Z < 0.0f)
        {
            Axis *= -1.0f;
        }

        return Axis;
    }

    FVector TransformChassisAxisToComponent(
        USkeletalMeshComponent* Mesh,
        const FVehicleSnapshot& VehicleSnapshot,
        const FVector& ChassisAxis,
        const FVector& FallbackComponentAxis
        )
    {
        if (!Mesh)
        {
            return FallbackComponentAxis;
        }

        const FMatrix MeshWorldInverse = Mesh->GetWorldMatrix().GetInverse();
        const FMatrix ChassisWorldMatrix = VehicleSnapshot.ChassisWorldTransform.ToMatrix();
        const FVector WorldAxis = ChassisWorldMatrix.TransformVector(ChassisAxis);
        return SafeNormalized(MeshWorldInverse.TransformVector(WorldAxis), FallbackComponentAxis);
    }

    FVector InferWheelSpinAxisInChassisSpace(
        const FVehicleSnapshot& VehicleSnapshot,
        int32 WheelIndex
        )
    {
        if (WheelIndex < 0 || WheelIndex >= static_cast<int32>(VehicleSnapshot.Wheels.size()))
        {
            return FVector::YAxisVector;
        }

        const FVehicleWheelSnapshot& Wheel = VehicleSnapshot.Wheels[WheelIndex];

        int32 PairIndex = -1;
        if (VehicleSnapshot.Wheels.size() == 2)
        {
            PairIndex = WheelIndex == 0 ? 1 : 0;
        }
        else if (VehicleSnapshot.Wheels.size() >= 4)
        {
            if (WheelIndex == 0)
            {
                PairIndex = 1;
            }
            else if (WheelIndex == 1)
            {
                PairIndex = 0;
            }
            else if (WheelIndex == 2)
            {
                PairIndex = 3;
            }
            else if (WheelIndex == 3)
            {
                PairIndex = 2;
            }
        }

        if (PairIndex < 0 || PairIndex >= static_cast<int32>(VehicleSnapshot.Wheels.size()))
        {
            float BestDistanceSq = FLT_MAX;
            for (int32 OtherIndex = 0; OtherIndex < static_cast<int32>(VehicleSnapshot.Wheels.size()); ++OtherIndex)
            {
                if (OtherIndex == WheelIndex)
                {
                    continue;
                }

                const FVector Delta = VehicleSnapshot.Wheels[OtherIndex].RestLocalPosition - Wheel.RestLocalPosition;
                const float DistanceSq = Delta.Dot(Delta);
                if (DistanceSq > 1.e-6f && DistanceSq < BestDistanceSq)
                {
                    BestDistanceSq = DistanceSq;
                    PairIndex = OtherIndex;
                }
            }
        }

        if (PairIndex < 0 || PairIndex >= static_cast<int32>(VehicleSnapshot.Wheels.size()))
        {
            return FVector::YAxisVector;
        }

        FVector Axis = VehicleSnapshot.Wheels[PairIndex].RestLocalPosition - Wheel.RestLocalPosition;
        Axis = MakeStableAxisSign(Axis);
        return SafeNormalized(Axis, FVector::YAxisVector);
    }

    FTransform MakeWheelVisualComponentTransform(
        const FTransform& BaseComponentTransform,
        const FVector& ComponentLocationDelta,
        const FVector& SpinAxisComponent,
        const FVector& SteerAxisComponent,
        const FVehicleWheelSnapshot& WheelSnapshot
        )
    {
        const FQuat InverseBaseRotation = BaseComponentTransform.Rotation.Inverse();
        const FVector SteerAxisLocal = SafeNormalized(InverseBaseRotation.RotateVector(SteerAxisComponent), FVector::ZAxisVector);
        const FVector SpinAxisLocal  = SafeNormalized(InverseBaseRotation.RotateVector(SpinAxisComponent), FVector::YAxisVector);

        const FQuat SteerRotation = FQuat::FromAxisAngle(SteerAxisLocal, WheelSnapshot.SteerAngle);
        const FQuat SpinRotation  = FQuat::FromAxisAngle(SpinAxisLocal, WheelSnapshot.RotationAngle);

        FQuat DeltaRotation = SteerRotation * SpinRotation;
        DeltaRotation.Normalize();

        FQuat TargetRotation = BaseComponentTransform.Rotation * DeltaRotation;
        TargetRotation.Normalize();

        return FTransform(BaseComponentTransform.Location + ComponentLocationDelta, TargetRotation, BaseComponentTransform.Scale);
    }

    FVector GetVisualComponentCenterWorld(USceneComponent* VisualComponent)
    {
        if (!VisualComponent)
        {
            return FVector::ZeroVector;
        }

        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(VisualComponent))
        {
            const FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
            if (Bounds.IsValid())
            {
                return Bounds.GetCenter();
            }
        }

        return VisualComponent->GetWorldLocation();
    }

    FVector TransformWorldPositionToChassisLocalNoScale(
        const FVehicleSnapshot& VehicleSnapshot,
        const FVector& WorldPosition
        )
    {
        return VehicleSnapshot.ChassisWorldTransform.Rotation.Inverse().RotateVector(
            WorldPosition - VehicleSnapshot.ChassisWorldTransform.Location
        );
    }

    FQuat ComposeChassisLocalRotationToWorld(
        const FVehicleSnapshot& VehicleSnapshot,
        const FQuat& ChassisLocalRotation
        )
    {
        FQuat WorldRotation = ChassisLocalRotation * VehicleSnapshot.ChassisWorldTransform.Rotation;
        WorldRotation.Normalize();
        return WorldRotation;
    }

    void ApplySceneComponentWheelPose(
        USceneComponent* VisualComponent,
        const FVehicleSnapshot& VehicleSnapshot,
        const FVehicleWheelSnapshot& WheelSnapshot,
        const FVector& CachedCenterOffsetLocal,
        const FVector& CachedRestCenterLocal,
        const FQuat& CachedRestRotationLocal
        )
    {
        if (!VisualComponent)
        {
            return;
        }

        int32 WheelIndex = -1;
        for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(VehicleSnapshot.Wheels.size()); ++CandidateIndex)
        {
            if (VehicleSnapshot.Wheels[CandidateIndex].WheelName == WheelSnapshot.WheelName)
            {
                WheelIndex = CandidateIndex;
                break;
            }
        }

        const FVector SpinAxisChassis = InferWheelSpinAxisInChassisSpace(VehicleSnapshot, WheelIndex);
        const FVector SteerAxisChassis = FVector::ZAxisVector;

        const FTransform RestLocalTransform(
            CachedRestCenterLocal,
            CachedRestRotationLocal,
            FVector::OneVector
        );

        const FVector ComponentLocationDelta = WheelSnapshot.CurrentLocalPosition - CachedRestCenterLocal;
        const FTransform TargetLocalTransform = MakeWheelVisualComponentTransform(
            RestLocalTransform,
            ComponentLocationDelta,
            SpinAxisChassis,
            SteerAxisChassis,
            WheelSnapshot
        );

        const FQuat TargetWorldRotation = ComposeChassisLocalRotationToWorld(
            VehicleSnapshot,
            TargetLocalTransform.Rotation
        );
        const FVector TargetOriginWorld = WheelSnapshot.WorldTransform.Location -
            TargetWorldRotation.RotateVector(CachedCenterOffsetLocal);

        VisualComponent->SetWorldLocation(TargetOriginWorld);
        VisualComponent->SetWorldRotation(TargetWorldRotation);
    }

    bool ApplyWheelBonePoseFromSnapshot(
        USkeletalMeshComponent* Mesh,
        const FVehicleWheelSetup& Setup,
        const FVehicleSnapshot& VehicleSnapshot,
        const FVehicleWheelSnapshot& WheelSnapshot
        )
    {
        if (!Mesh || !Setup.BoneName.IsValid())
        {
            return false;
        }

        USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMesh();
        FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
        if (!Asset)
        {
            return false;
        }

        const int32 BoneIndex = Mesh->FindBoneIndex(Setup.BoneName);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
        {
            return false;
        }

        TArray<FMatrix> BaseGlobals;
        if (!BuildBaseComponentSpaceMatrices(Mesh, BaseGlobals) || BoneIndex >= static_cast<int32>(BaseGlobals.size()))
        {
            return false;
        }

        const FMatrix MeshWorldInverse = Mesh->GetWorldMatrix().GetInverse();
        const FMatrix ChassisWorldMatrix = VehicleSnapshot.ChassisWorldTransform.ToMatrix();
        const FVector RestWheelWorldPosition = ChassisWorldMatrix.TransformPositionWithW(WheelSnapshot.RestLocalPosition);
        const FVector WheelWorldDelta = WheelSnapshot.WorldTransform.Location - RestWheelWorldPosition;
        const FVector ComponentLocationDelta = MeshWorldInverse.TransformVector(WheelWorldDelta);

        int32 WheelIndex = -1;
        for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(VehicleSnapshot.Wheels.size()); ++CandidateIndex)
        {
            if (VehicleSnapshot.Wheels[CandidateIndex].WheelName == WheelSnapshot.WheelName)
            {
                WheelIndex = CandidateIndex;
                break;
            }
        }

        const FVector SpinAxisChassis = InferWheelSpinAxisInChassisSpace(VehicleSnapshot, WheelIndex);
        const FVector SpinAxisComponent = TransformChassisAxisToComponent(Mesh, VehicleSnapshot, SpinAxisChassis, FVector::YAxisVector);
        const FVector SteerAxisComponent = TransformChassisAxisToComponent(Mesh, VehicleSnapshot, FVector::ZAxisVector, FVector::ZAxisVector);

        const FTransform BaseComponentTransform(BaseGlobals[BoneIndex]);
        const FTransform TargetComponentTransform = MakeWheelVisualComponentTransform(
            BaseComponentTransform,
            ComponentLocationDelta,
            SpinAxisComponent,
            SteerAxisComponent,
            WheelSnapshot
        );

        const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
        const FMatrix TargetComponentMatrix = TargetComponentTransform.ToMatrix();
        const FMatrix LocalMatrix = ParentIndex >= 0 && ParentIndex < static_cast<int32>(BaseGlobals.size())
            ? TargetComponentMatrix * BaseGlobals[ParentIndex].GetInverse()
            : TargetComponentMatrix;

        Mesh->SetBoneLocalTransformByIndex(BoneIndex, FTransform(LocalMatrix));
        return true;
    }
}

UVehicleWheelPoseComponent::FSceneWheelVisualPoseCache* UVehicleWheelPoseComponent::FindOrCreateSceneWheelPoseCache(
    USceneComponent* VisualComponent,
    const FVehicleSnapshot& VehicleSnapshot
    )
{
    if (!VisualComponent)
    {
        return nullptr;
    }

    PurgeInvalidSceneWheelPoseCaches();

    for (FSceneWheelVisualPoseCache& ExistingCache : SceneWheelPoseCaches)
    {
        if (ExistingCache.VisualComponent.Get() == VisualComponent)
        {
            return &ExistingCache;
        }
    }

    FSceneWheelVisualPoseCache NewCache;
    NewCache.VisualComponent.Reset(VisualComponent);

    const FVector CurrentCenterWorld = GetVisualComponentCenterWorld(VisualComponent);
    NewCache.CenterOffsetLocal = VisualComponent->GetWorldMatrix().GetInverse().TransformPositionWithW(CurrentCenterWorld);
    NewCache.RestCenterLocal = TransformWorldPositionToChassisLocalNoScale(VehicleSnapshot, CurrentCenterWorld);

    const FQuat CurrentWorldRotation = VisualComponent->GetWorldMatrix().ToQuat().GetNormalized();
    NewCache.RestRotationLocal = (CurrentWorldRotation * VehicleSnapshot.ChassisWorldTransform.Rotation.Inverse()).GetNormalized();

    SceneWheelPoseCaches.push_back(NewCache);
    return &SceneWheelPoseCaches.back();
}

void UVehicleWheelPoseComponent::PurgeInvalidSceneWheelPoseCaches()
{
    for (auto It = SceneWheelPoseCaches.begin(); It != SceneWheelPoseCaches.end(); )
    {
        if (!IsValid(It->VisualComponent.Get()))
        {
            It = SceneWheelPoseCaches.erase(It);
        }
        else
        {
            ++It;
        }
    }
}

UVehicleWheelPoseComponent::UVehicleWheelPoseComponent()
{
    PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
    PrimaryComponentTick.SetEndTickGroup(TG_PostUpdateWork);
}

void UVehicleWheelPoseComponent::BeginPlay()
{
    UActorComponent::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    if (!VehicleMovement)
    {
        VehicleMovement = Owner->GetComponentByClass<UWheeledVehicleMovementComponent>();
    }

    if (!Mesh)
    {
        Mesh = Owner->GetComponentByClass<USkeletalMeshComponent>();
    }
}

void UVehicleWheelPoseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UWheeledVehicleMovementComponent* Movement = VehicleMovement.Get();
    if (!IsValid(Movement))
    {
        return;
    }

    const FVehicleSnapshot* Snapshot = Movement->GetLastVehicleSnapshot();
    if (!Snapshot)
    {
        return;
    }

    for (const FVehicleWheelSnapshot& WheelSnapshot : Snapshot->Wheels)
    {
        const FVehicleWheelSetup* Setup = Movement->FindWheelSetup(WheelSnapshot.WheelName);
        if (!Setup)
        {
            continue;
        }

        if (Setup->BoneName.IsValid() && IsValid(Mesh.Get()))
        {
            if (ApplyWheelBonePoseFromSnapshot(Mesh.Get(), *Setup, *Snapshot, WheelSnapshot))
            {
                continue;
            }
        }

        if (USceneComponent* VisualComponent = Movement->ResolveWheelVisualComponent(*Setup))
        {
            if (FSceneWheelVisualPoseCache* PoseCache = FindOrCreateSceneWheelPoseCache(VisualComponent, *Snapshot))
            {
                ApplySceneComponentWheelPose(
                    VisualComponent,
                    *Snapshot,
                    WheelSnapshot,
                    PoseCache->CenterOffsetLocal,
                    PoseCache->RestCenterLocal,
                    PoseCache->RestRotationLocal
                );
            }
        }
    }
}

void UVehicleWheelPoseComponent::SetVehicleMovement(UWheeledVehicleMovementComponent* InMovement)
{
    VehicleMovement = InMovement;
}

void UVehicleWheelPoseComponent::SetSkeletalMeshComponent(USkeletalMeshComponent* InMesh)
{
    Mesh = InMesh;
}
