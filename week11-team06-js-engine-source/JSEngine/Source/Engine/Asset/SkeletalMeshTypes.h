#pragma once
#include "Core/CoreMinimal.h"
#include "SkeletonTypes.h"
#include "StaticMeshTypes.h"

struct FSkeletalMesh
{
    FString PathFileName;
    FString SkeletonSourcePath;

    TArray<FSkeletalMeshVertex> Vertices;
    TArray<uint32> Indices;

    // Transitional cache until skeletal meshes read bone data from USkeletonAsset.
    TArray<FBoneInfo> Bones;

    // Transitional socket storage until sockets move fully to shared skeleton assets.
    TArray<FSkeletalMeshSocket> Sockets;

    // Material
    // StaticMeshSection 이긴 하나, StaticMesh 에 종속된 개념이 아니라 이름은 나중에 바꿔야할것으로 보임
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> MaterialSlots;

    // Bounds
    FAABB LocalBounds;
    FAABB ConservativeLocalBounds;
};
