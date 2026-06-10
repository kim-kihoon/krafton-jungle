#include "BoneTransformGizmoTarget.h"

#include "Animation/Skeleton/Skeleton.h"
#include "Component/Primitive/SkeletalMeshComponent.h"

#include <algorithm>

FBoneTransformGizmoTarget::FBoneTransformGizmoTarget()
	: MeshComponent(nullptr), BoneIndex(-1)
{
}

FBoneTransformGizmoTarget::FBoneTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, int32 InBoneIndex)
	: MeshComponent(InMeshComp), BoneIndex(InBoneIndex)
{
}

bool FBoneTransformGizmoTarget::IsValid() const
{
	return MeshComponent != nullptr && BoneIndex >= 0;
}

UWorld* FBoneTransformGizmoTarget::GetWorld() const
{
	return MeshComponent ? MeshComponent->GetWorld() : nullptr;
}

FVector FBoneTransformGizmoTarget::GetWorldLocation() const
{
	return MeshComponent ? MeshComponent->GetBoneLocationByIndex(BoneIndex) : FVector::ZeroVector;
}

FRotator FBoneTransformGizmoTarget::GetWorldRotation() const
{
	return MeshComponent ? MeshComponent->GetBoneRotationByIndex(BoneIndex) : FRotator::ZeroRotator;
}

FQuat FBoneTransformGizmoTarget::GetWorldQuat() const
{
	return MeshComponent ? MeshComponent->GetBoneQuatByIndex(BoneIndex) : FQuat::Identity;
}

FVector FBoneTransformGizmoTarget::GetWorldScale() const
{
	return MeshComponent ? MeshComponent->GetBoneScaleByIndex(BoneIndex) : FVector::OneVector;
}

void FBoneTransformGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	// 본 트랜스폼은 직접 세팅이 불가능하므로 오프셋으로만 적용한다.
	if (MeshComponent)
	{
		FVector CurrentLocation = GetWorldLocation();
		FVector Delta = NewLocation - CurrentLocation;
		AddWorldOffset(Delta);
	}
}

void FBoneTransformGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
	if (MeshComponent)
	{
		FRotator CurrentRotation = GetWorldRotation();
		FQuat DeltaQuat = (NewRotation - CurrentRotation).ToQuaternion();
		AddWorldRotation(DeltaQuat, true);
	}
}

void FBoneTransformGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
	if (MeshComponent)
	{
		FQuat CurrentQuat = GetWorldQuat();
		FQuat DeltaQuat = NewQuat * CurrentQuat.Inverse();
		AddWorldRotation(DeltaQuat, true);
	}
}

void FBoneTransformGizmoTarget::SetWorldScale(const FVector& NewScale)
{
	if (MeshComponent)
	{
		FVector CurrentScale = GetWorldScale();
		FVector Delta = NewScale - CurrentScale;
		AddScaleDelta(Delta);
	}
}

void FBoneTransformGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	if (MeshComponent)
	{
		FVector CurrentLocation = GetWorldLocation();
		FVector NewLocation = CurrentLocation + Delta;
		MeshComponent->SetBoneLocationByIndex(BoneIndex, NewLocation);
	}
}

void FBoneTransformGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
	if (MeshComponent)
	{
		FQuat CurrentRotation = GetWorldQuat();
		FQuat NewRotation = bWorldSpace ? Delta * CurrentRotation : CurrentRotation * Delta;
		MeshComponent->SetBoneRotationByIndex(BoneIndex, NewRotation);
	}
}

void FBoneTransformGizmoTarget::AddScaleDelta(const FVector& Delta)
{
	if (MeshComponent)
	{
		FVector CurrentScale = GetWorldScale();
		FVector NewScale = CurrentScale + Delta;
		MeshComponent->SetBoneScaleByIndex(BoneIndex, NewScale);
	}
}

FSocketTransformGizmoTarget::FSocketTransformGizmoTarget()
	: MeshComponent(nullptr), Skeleton(nullptr), SocketIndex(-1)
{
}

FSocketTransformGizmoTarget::FSocketTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, USkeleton* InSkeleton, int32 InSocketIndex)
	: MeshComponent(InMeshComp), Skeleton(InSkeleton), SocketIndex(InSocketIndex)
{
}

void FSocketTransformGizmoTarget::SetSocket(USkeletalMeshComponent* MeshComp, USkeleton* InSkeleton, int32 InSocketIndex)
{
	MeshComponent = MeshComp;
	Skeleton = InSkeleton;
	SocketIndex = InSocketIndex;
	bModified = false;
}

bool FSocketTransformGizmoTarget::ConsumeModified()
{
	const bool bWasModified = bModified;
	bModified = false;
	return bWasModified;
}

bool FSocketTransformGizmoTarget::IsValid() const
{
	if (!MeshComponent || !Skeleton || SocketIndex < 0 || SocketIndex >= static_cast<int32>(Skeleton->GetSockets().size()))
	{
		return false;
	}

	int32 BoneIndex = -1;
	return Skeleton->ResolveSocketBoneIndex(Skeleton->GetSockets()[SocketIndex], BoneIndex);
}

UWorld* FSocketTransformGizmoTarget::GetWorld() const
{
	return MeshComponent ? MeshComponent->GetWorld() : nullptr;
}

FVector FSocketTransformGizmoTarget::GetWorldLocation() const
{
	FTransform SocketWorld;
	return GetSocketWorldTransform(SocketWorld) ? SocketWorld.Location : FVector::ZeroVector;
}

FRotator FSocketTransformGizmoTarget::GetWorldRotation() const
{
	FTransform SocketWorld;
	return GetSocketWorldTransform(SocketWorld) ? SocketWorld.GetRotator() : FRotator::ZeroRotator;
}

FQuat FSocketTransformGizmoTarget::GetWorldQuat() const
{
	FTransform SocketWorld;
	return GetSocketWorldTransform(SocketWorld) ? SocketWorld.Rotation : FQuat::Identity;
}

FVector FSocketTransformGizmoTarget::GetWorldScale() const
{
	FTransform SocketWorld;
	return GetSocketWorldTransform(SocketWorld) ? SocketWorld.Scale : FVector::OneVector;
}

void FSocketTransformGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	FTransform SocketWorld;
	if (GetSocketWorldTransform(SocketWorld))
	{
		SocketWorld.Location = NewLocation;
		ApplySocketWorldTransform(SocketWorld);
	}
}

void FSocketTransformGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
	SetWorldRotation(NewRotation.ToQuaternion());
}

void FSocketTransformGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
	FTransform SocketWorld;
	if (GetSocketWorldTransform(SocketWorld))
	{
		SocketWorld.Rotation = NewQuat.GetNormalized();
		ApplySocketWorldTransform(SocketWorld);
	}
}

void FSocketTransformGizmoTarget::SetWorldScale(const FVector& NewScale)
{
	FTransform SocketWorld;
	if (GetSocketWorldTransform(SocketWorld))
	{
		SocketWorld.Scale = FVector(
			std::max(NewScale.X, 0.01f),
			std::max(NewScale.Y, 0.01f),
			std::max(NewScale.Z, 0.01f));
		ApplySocketWorldTransform(SocketWorld);
	}
}

void FSocketTransformGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	SetWorldLocation(GetWorldLocation() + Delta);
}

void FSocketTransformGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
	FTransform SocketWorld;
	if (GetSocketWorldTransform(SocketWorld))
	{
		SocketWorld.Rotation = bWorldSpace
			? (Delta * SocketWorld.Rotation).GetNormalized()
			: (SocketWorld.Rotation * Delta).GetNormalized();
		ApplySocketWorldTransform(SocketWorld);
	}
}

void FSocketTransformGizmoTarget::AddScaleDelta(const FVector& Delta)
{
	SetWorldScale(GetWorldScale() + Delta);
}

bool FSocketTransformGizmoTarget::GetSocketWorldTransform(FTransform& OutTransform) const
{
	if (!IsValid())
	{
		return false;
	}

	const FSkeletalMeshSocket& Socket = Skeleton->GetSockets()[SocketIndex];

	int32 BoneIndex = -1;
	if (!Skeleton->ResolveSocketBoneIndex(Socket, BoneIndex))
	{
		return false;
	}

	TArray<FMatrix> BoneGlobals;
	MeshComponent->GetCurrentBoneGlobalMatrices(BoneGlobals);
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneGlobals.size()))
	{
		return false;
	}

	const FMatrix SocketWorldMatrix = Socket.GetRelativeTransform() * BoneGlobals[BoneIndex] * MeshComponent->GetWorldMatrix();
	OutTransform = FTransform(SocketWorldMatrix);
	return true;
}

bool FSocketTransformGizmoTarget::ApplySocketWorldTransform(const FTransform& NewWorldTransform)
{
	if (!IsValid())
	{
		return false;
	}

	FSkeletalMeshSocket& Socket = Skeleton->GetMutableSockets()[SocketIndex];

	int32 BoneIndex = -1;
	if (!Skeleton->ResolveSocketBoneIndex(Socket, BoneIndex))
	{
		return false;
	}

	TArray<FMatrix> BoneGlobals;
	MeshComponent->GetCurrentBoneGlobalMatrices(BoneGlobals);
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneGlobals.size()))
	{
		return false;
	}

	const FMatrix SocketBaseWorldMatrix = BoneGlobals[BoneIndex] * MeshComponent->GetWorldMatrix();
	const FMatrix RelativeMatrix = NewWorldTransform.ToMatrix() * SocketBaseWorldMatrix.GetInverse();
	const FTransform RelativeTransform(RelativeMatrix);

	Socket.RelativeLocation = RelativeTransform.Location;
	Socket.RelativeRotation = RelativeTransform.GetRotator();
	Socket.RelativeScale = FVector(
		std::max(RelativeTransform.Scale.X, 0.01f),
		std::max(RelativeTransform.Scale.Y, 0.01f),
		std::max(RelativeTransform.Scale.Z, 0.01f));
	bModified = true;
	return true;
}
