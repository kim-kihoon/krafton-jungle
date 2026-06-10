#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/FName.h"

enum class EClothCollisionSource : uint8
{
    PhysicsAsset,
    WorldStatic,
    WorldDynamic
};

enum class EClothCollisionPrimitiveType : uint8
{
    Sphere,
    Capsule,
    Box
};

enum class EClothCollisionSelectState : uint8
{
    Gathered,
    Selected,
    RejectedByParticipationFilter,
    TruncatedByBudget,
    SkippedInvalidTransform,
    SkippedNonUniformScale,
    SkippedFilter,
    RejectedBySectionBounds
};

struct FClothCollisionSourceId
{
    EClothCollisionSource Source = EClothCollisionSource::PhysicsAsset;
    uint32 OwnerActorId = 0;
    uint32 OwnerComponentId = 0;
    FName BoneName = FName::None;
    int32 BodyIndex = -1;
    int32 ShapeIndex = -1;
    ECollisionChannel ObjectChannel = ECollisionChannel::WorldStatic;
};

struct FClothCollisionSphere
{
    FVector LocalCenter = FVector::ZeroVector;
    float Radius = 0.0f;
};

struct FClothCollisionCapsule
{
    FVector LocalPoint0 = FVector::ZeroVector;
    FVector LocalPoint1 = FVector::ZeroVector;
    float Radius = 0.0f;
};

struct FClothCollisionBox
{
    FTransform LocalTransform;
    FVector HalfExtent = FVector::ZeroVector;
};

struct FClothCollisionPrimitiveSet
{
    TArray<FClothCollisionSphere> Spheres;
    TArray<FClothCollisionCapsule> Capsules;
    TArray<FClothCollisionBox> Boxes;
};

struct FClothCollisionCandidate
{
    EClothCollisionPrimitiveType Type = EClothCollisionPrimitiveType::Sphere;
    FClothCollisionSourceId SourceId;

    FTransform LocalTransform;
    FTransform PreviousLocalTransform;
    FTransform CurrentLocalTransform;
    FVector HalfExtent = FVector::ZeroVector;
    float Radius = 0.0f;
    float CapsuleHalfHeight = 0.0f;

    FBoundingBox WorldBounds;

    int32 OverlapRank = 1;
    float DistanceScore = 0.0f;
    float CenterDistanceScore = 0.0f;
    int32 TypeCost = 1;
    uint64 StableTieBreaker = 0;

    EClothCollisionSelectState State = EClothCollisionSelectState::Gathered;
};

struct FClothCollisionDebugStats
{
    uint32 GatheredSpheres = 0;
    uint32 GatheredCapsules = 0;
    uint32 GatheredBoxes = 0;
    uint32 SelectedSpheres = 0;
    uint32 SelectedCapsules = 0;
    uint32 SelectedBoxes = 0;
    uint32 UploadedSpheres = 0;
    uint32 UploadedCapsules = 0;
    uint32 UploadedPlanes = 0;
    uint32 UploadedConvexes = 0;
    uint32 SkippedNonUniformScale = 0;
    uint32 Rejected = 0;
    uint32 Truncated = 0;
    uint32 GatheredWorldStatic = 0;
    uint32 SelectedWorldStatic = 0;
    uint32 RejectedWorldStatic = 0;
    uint32 TruncatedWorldStatic = 0;
    uint32 GatheredWorldDynamic = 0;
    uint32 SelectedWorldDynamic = 0;
    uint32 RejectedWorldDynamic = 0;
    uint32 TruncatedWorldDynamic = 0;

    void Reset()
    {
        *this = FClothCollisionDebugStats();
    }
};

struct FClothCollisionGatherResult
{
    TArray<FClothCollisionCandidate> Candidates;
    FClothCollisionPrimitiveSet SelectedPrimitives;
    FClothCollisionDebugStats Stats;
};

struct FClothCollisionSectionResult
{
    uint32 LODIndex = 0;
    int32 SectionIndex = -1;
    bool bWorldStaticCollisionEnabled = false;
    bool bWorldDynamicCollisionEnabled = false;
    FClothCollisionGatherResult GatherResult;
};

struct FClothCollisionBudget
{
    uint32 MaxAuthoredSpheres = 32;
    uint32 MaxAuthoredCapsules = 16;
    uint32 MaxAuthoredBoxes = 5;
    uint32 MaxNvClothPlanes = 32;
    uint32 MaxNvClothConvexes = 5;
};
