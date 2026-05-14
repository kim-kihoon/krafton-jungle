#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Geometry/AABB.h"
#include "Engine/Geometry/Transform.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/VertexTypes.h"

static constexpr int32 MAX_SKELETAL_BONE_INFLUENCES = 4;

struct FSkeletalMeshVertex
{
    FVector Position;
    FColor Color = FColor::White();

    FVector Normal = FVector::UpVector;
    FVector2 UVs = FVector2(0.0f, 0.0f);

    FVector Tangent = FVector::ForwardVector;
    FVector Bitangent = FVector::RightVector;

    int32 BoneIndices[MAX_SKELETAL_BONE_INFLUENCES] = { -1, -1, -1, -1 };
    float BoneWeights[MAX_SKELETAL_BONE_INFLUENCES] = { 0.0f, 0.0f, 0.0f, 0.0f };

    FNormalVertex ToNormalVertex() const
    {
        FNormalVertex Out;
        Out.Position = Position;
        Out.Color = Color;
        Out.Normal = Normal;
        Out.UVs = UVs;
        Out.Tangent = Tangent;
        Out.Bitangent = Bitangent;
        return Out;
    }
};

struct FSkeletalMeshSection
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    int32 MaterialSlotIndex = -1;
};

struct FSkeletalMeshMaterialSlot
{
    FString SlotName;
    FString MaterialAssetPath;
    UMaterialInterface* Material = nullptr;
    FString ExtractedDiffusePath;
    FString ExtractedNormalPath;
    FString ExtractedSpecularPath;
};

struct FSkeletalBone
{
    FString Name;

    // -1이면 root bone.
    int32 ParentIndex = -1;

    FTransform ReferenceLocalTransform = FTransform::Identity;
};

// Cooked skeletal mesh data.
// 여러 USkeletalMeshComponent가 공유하는 원본 데이터.
struct FSkeletalMesh
{
    FString PathFileName;
    FString SkeletonAssetPath;

    TArray<FSkeletalMeshVertex> Vertices;
    TArray<uint32> Indices;

    TArray<FSkeletalMeshSection> Sections;
    TArray<FSkeletalMeshMaterialSlot> MaterialSlots;

    TArray<FSkeletalBone> Bones;
    TArray<FMatrix> InverseBindPoseMatrices;

    FAABB LocalBounds;
};
