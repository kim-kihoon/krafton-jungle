#pragma once

#include "Core/CoreMinimal.h"
#include "Math/Rotator.h"
#include "Object/FName.h"

struct FBoneInfo
{
    FString Name;

    int32 ParentIndex = -1;

    FMatrix LocalBindTransform;
    FMatrix GlobalBindTransform;

    FMatrix InverseBindPose;
};

// Named attach point bound to a bone. T/R/S are relative to the bone local space.
struct FSkeletalMeshSocket
{
    FName    Name;
    int32    BoneIndex = -1;
    FVector  RelativeLocation = FVector::ZeroVector;
    FRotator RelativeRotation;
    FVector  RelativeScale = FVector(1.0f, 1.0f, 1.0f);

    // Engine row-vector convention: v * S * R * T.
    FMatrix GetRelativeTransform() const;
};

struct FSkeleton
{
    FString PathFileName;
    FString RootNodeName;

    TArray<FBoneInfo> Bones;
    TArray<FSkeletalMeshSocket> Sockets;
};
