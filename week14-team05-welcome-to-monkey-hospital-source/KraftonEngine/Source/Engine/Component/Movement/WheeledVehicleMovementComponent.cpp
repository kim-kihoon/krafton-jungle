#include "Component/Movement/WheeledVehicleMovementComponent.h"

#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Debug/DrawDebugHelpers.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsWorldSnapshot.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include "Object/Ptr/WeakObjectPtr.h"
#include "Object/GarbageCollection.h"

namespace
{
    constexpr float VehicleDegToRad = 3.14159265358979323846f / 180.0f;
    constexpr int32 VehicleSetupWheelCount = 4;

    enum class EWheelSetupSlot : uint8
    {
        FrontLeft = 0,
        FrontRight,
        RearLeft,
        RearRight
    };

    struct FWheelBoneCandidate
    {
        FName   BoneName = FName::None;
        FString Name;
        FString LowerName;
        FVector LocalPosition = FVector::ZeroVector;
        USceneComponent* VisualComponent = nullptr;
        int32 ParentBoneIndex = -1;
        bool bHasChildBone = false;
    };

    IPhysicsScene* GetPhysicsScene(const UActorComponent* Component)
    {
        UWorld* World = Component ? Component->GetWorld() : nullptr;
        return World ? World->GetPhysicsScene() : nullptr;
    }

    FQuat GetWorldRotationQuat(const USceneComponent* Component)
    {
        return Component ? Component->GetWorldRotation().ToQuaternion().GetNormalized() : FQuat::Identity;
    }

    FVector TransformPositionNoScale(const USceneComponent* Frame, const FVector& LocalPosition)
    {
        if (!Frame)
        {
            return LocalPosition;
        }

        return Frame->GetWorldLocation() + GetWorldRotationQuat(Frame).RotateVector(LocalPosition);
    }

    FVector InverseTransformPositionNoScale(const USceneComponent* Frame, const FVector& WorldPosition)
    {
        if (!Frame)
        {
            return WorldPosition;
        }

        return GetWorldRotationQuat(Frame).Inverse().RotateVector(WorldPosition - Frame->GetWorldLocation());
    }

    bool IsEmptySkeletalMeshComponent(const USceneComponent* Component)
    {
        const USkinnedMeshComponent* SkinnedMesh = Cast<USkinnedMeshComponent>(Component);
        return SkinnedMesh && !SkinnedMesh->GetSkeletalMesh();
    }

    bool NameEquals(const char* Value, const char* Expected)
    {
        return Value && Expected && std::strcmp(Value, Expected) == 0;
    }

    bool IsWheelSetupsPropertyChange(const FPropertyChangedEvent& Event)
    {
        const FString Path = Event.PropertyPath;
        const char* PropertyName = Event.PropertyName ? Event.PropertyName : "";
        return Path.find("WheelSetups") != FString::npos ||
            NameEquals(PropertyName, "WheelSetups") ||
            NameEquals(PropertyName, "Wheel Setups");
    }

    bool PathContainsProperty(const FString& Path, const char* PropertyName)
    {
        return PropertyName && PropertyName[0] != '\0' &&
            (Path.find(FString(".") + PropertyName) != FString::npos ||
                Path.find(FString("]") + PropertyName) != FString::npos);
    }

    bool IsWheelSetupAutoPositionPropertyChange(const FPropertyChangedEvent& Event)
    {
        if (!IsWheelSetupsPropertyChange(Event))
        {
            return false;
        }

        const FString Path = Event.PropertyPath;
        const char* PropertyName = Event.PropertyName ? Event.PropertyName : "";

        if (Event.ChangeType == EPropertyChangeType::ArrayAdd ||
            Event.ChangeType == EPropertyChangeType::ArrayRemove ||
            Event.ChangeType == EPropertyChangeType::Duplicate ||
            Event.ChangeType == EPropertyChangeType::Load)
        {
            return true;
        }

        if (NameEquals(PropertyName, "WheelSetups") || NameEquals(PropertyName, "Wheel Setups"))
        {
            return Path.find('.') == FString::npos;
        }

        return
            NameEquals(PropertyName, "PositionSource") ||
            NameEquals(PropertyName, "BoneName") ||
            NameEquals(PropertyName, "VisualComponent") ||
            NameEquals(PropertyName, "bUseBoneInfluenceSurfaceCenter") ||
            PathContainsProperty(Path, "PositionSource") ||
            PathContainsProperty(Path, "BoneName") ||
            PathContainsProperty(Path, "VisualComponent") ||
            PathContainsProperty(Path, "bUseBoneInfluenceSurfaceCenter");
    }

    bool IsVehicleDescPropertyChange(const FPropertyChangedEvent& Event)
    {
        const char* PropertyName = Event.PropertyName ? Event.PropertyName : "";

        if (IsWheelSetupsPropertyChange(Event))
        {
            return true;
        }

        return
            NameEquals(PropertyName, "ChassisComponent") ||
            NameEquals(PropertyName, "Chassis Component") ||
            NameEquals(PropertyName, "ChassisHalfExtents") ||
            NameEquals(PropertyName, "ChassisMass") ||
            NameEquals(PropertyName, "bAutoFitChassisCollisionOffset") ||
            NameEquals(PropertyName, "ChassisCollisionOffset") ||
            NameEquals(PropertyName, "ChassisGroundClearance") ||
            NameEquals(PropertyName, "ChassisCenterOfMassOffset") ||
            NameEquals(PropertyName, "bAutoAlignSimulationForwardFromWheelLayout") ||
            NameEquals(PropertyName, "EnginePeakTorque") ||
            NameEquals(PropertyName, "EngineMaxOmega") ||
            NameEquals(PropertyName, "ClutchStrength") ||
            NameEquals(PropertyName, "bEnableReverseGear") ||
            NameEquals(PropertyName, "Enable Reverse Gear") ||
            NameEquals(PropertyName, "ReverseGearSwitchSpeed") ||
            NameEquals(PropertyName, "Reverse Gear Switch Speed") ||
            NameEquals(PropertyName, "TireFriction") ||
            NameEquals(PropertyName, "bWheelsTraceWorldStatic") ||
            NameEquals(PropertyName, "Wheels Trace World Static") ||
            NameEquals(PropertyName, "bWheelsTraceWorldDynamic") ||
            NameEquals(PropertyName, "Wheels Trace World Dynamic") ||
            NameEquals(PropertyName, "bWheelsTracePawn") ||
            NameEquals(PropertyName, "Wheels Trace Pawn/Skeletal") ||
            NameEquals(PropertyName, "bWheelsTraceProjectile") ||
            NameEquals(PropertyName, "Wheels Trace Projectile");
    }

    FString ToLowerName(FString Value)
    {
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
        {
            return static_cast<char>(std::tolower(Ch));
        });
        return Value;
    }

    bool ContainsToken(const FString& LowerName, const char* Token)
    {
        return LowerName.find(Token) != FString::npos;
    }

    bool ContainsName(const TArray<FName>& Names, const FName& Target)
    {
        return std::find(Names.begin(), Names.end(), Target) != Names.end();
    }

    bool IsLikelyWheelBoneName(const FString& LowerName)
    {
        return ContainsToken(LowerName, "wheel") ||
            ContainsToken(LowerName, "tire") ||
            ContainsToken(LowerName, "tyre");
    }

    bool HasDelimitedToken(const FString& LowerName, const char* Token)
    {
        if (LowerName == Token)
        {
            return true;
        }

        const size_t TokenLength = std::strlen(Token);
        if (TokenLength == 0 || LowerName.size() < TokenLength)
        {
            return false;
        }

        for (size_t Offset = 0; Offset + TokenLength <= LowerName.size(); ++Offset)
        {
            if (LowerName.compare(Offset, TokenLength, Token) != 0)
            {
                continue;
            }

            const bool bLeftBoundary = Offset == 0 || LowerName[Offset - 1] == '_' || LowerName[Offset - 1] == '.' || LowerName[Offset - 1] == '-' || LowerName[Offset - 1] == ' ';
            const size_t Right = Offset + TokenLength;
            const bool bRightBoundary = Right >= LowerName.size() || LowerName[Right] == '_' || LowerName[Right] == '.' || LowerName[Right] == '-' || LowerName[Right] == ' ';
            if (bLeftBoundary && bRightBoundary)
            {
                return true;
            }
        }

        return false;
    }

    bool IsLikelyWheelHelperBoneName(const FString& LowerName)
    {
        // Blender/FBX commonly exports non-deforming end-site bones such as
        // wheel_Front_L_end and wheel_Front_L_end_end. They contain "wheel"
        // but are not wheel simulation centers. Steering direction/locator bones
        // are also helpers and must not become PhysX wheels.
        return HasDelimitedToken(LowerName, "end") ||
            HasDelimitedToken(LowerName, "end_end") ||
            HasDelimitedToken(LowerName, "dir") ||
            HasDelimitedToken(LowerName, "direction") ||
            HasDelimitedToken(LowerName, "locator") ||
            HasDelimitedToken(LowerName, "helper") ||
            HasDelimitedToken(LowerName, "target") ||
            HasDelimitedToken(LowerName, "pivot") ||
            HasDelimitedToken(LowerName, "ik");
    }

    bool IsPrimaryWheelBoneName(const FString& LowerName)
    {
        return IsLikelyWheelBoneName(LowerName) && !IsLikelyWheelHelperBoneName(LowerName);
    }

    bool IsLikelyNonWheelRootName(const FString& LowerName)
    {
        return LowerName == "root" ||
            ContainsToken(LowerName, "armature") ||
            ContainsToken(LowerName, "chassis") ||
            ContainsToken(LowerName, "body") ||
            ContainsToken(LowerName, "pelvis");
    }

    bool IsUsableUnnamedWheelFallbackCandidate(const FWheelBoneCandidate& Candidate)
    {
        return !IsLikelyNonWheelRootName(Candidate.LowerName) &&
            !Candidate.LocalPosition.IsNearlyZero(0.01f);
    }

    bool TryComputeBoneInfluenceSurfaceCenter(const FSkeletalMesh* Asset, int32 BoneIndex, FVector& OutMeshLocalCenter)
    {
        if (!Asset || BoneIndex < 0 || Asset->Vertices.empty())
        {
            return false;
        }

        constexpr float MinimumInfluenceWeight = 0.25f;

        bool bHasAnyVertex = false;
        FVector LocalMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        FVector LocalMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

        for (const FVertexPNCTBW& Vertex : Asset->Vertices)
        {
            float BoneInfluence = 0.0f;
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                if (Vertex.BoneIndices[InfluenceIndex] == BoneIndex)
                {
                    BoneInfluence = std::max(BoneInfluence, Vertex.BoneWeights[InfluenceIndex]);
                }
            }

            if (BoneInfluence < MinimumInfluenceWeight)
            {
                continue;
            }

            const FVector& Position = Vertex.Position;
            LocalMin.X = std::min(LocalMin.X, Position.X);
            LocalMin.Y = std::min(LocalMin.Y, Position.Y);
            LocalMin.Z = std::min(LocalMin.Z, Position.Z);
            LocalMax.X = std::max(LocalMax.X, Position.X);
            LocalMax.Y = std::max(LocalMax.Y, Position.Y);
            LocalMax.Z = std::max(LocalMax.Z, Position.Z);
            bHasAnyVertex = true;
        }

        if (!bHasAnyVertex)
        {
            return false;
        }

        OutMeshLocalCenter = (LocalMin + LocalMax) * 0.5f;
        return true;
    }

    bool HasCompactToken(const FString& LowerName, const char* Token)
    {
        if (LowerName == Token)
        {
            return true;
        }

        const FString NeedleA = FString("_") + Token;
        const FString NeedleB = FString(".") + Token;
        const FString NeedleC = FString("-") + Token;
        const FString NeedleD = FString(" ") + Token;
        if (LowerName.find(NeedleA) != FString::npos ||
            LowerName.find(NeedleB) != FString::npos ||
            LowerName.find(NeedleC) != FString::npos ||
            LowerName.find(NeedleD) != FString::npos)
        {
            return true;
        }

        if (LowerName.size() >= std::strlen(Token))
        {
            const size_t Offset = LowerName.size() - std::strlen(Token);
            if (LowerName.compare(Offset, std::strlen(Token), Token) == 0)
            {
                return true;
            }
        }

        return false;
    }

    bool NameSuggestsFront(const FString& LowerName)
    {
        return ContainsToken(LowerName, "front") ||
            ContainsToken(LowerName, "forward") ||
            HasCompactToken(LowerName, "fl") ||
            HasCompactToken(LowerName, "fr") ||
            HasCompactToken(LowerName, "lf") ||
            HasCompactToken(LowerName, "rf");
    }

    bool NameSuggestsRear(const FString& LowerName)
    {
        return ContainsToken(LowerName, "rear") ||
            ContainsToken(LowerName, "back") ||
            HasCompactToken(LowerName, "rl") ||
            HasCompactToken(LowerName, "rr") ||
            HasCompactToken(LowerName, "lr");
    }

    bool NameSuggestsLeft(const FString& LowerName)
    {
        return ContainsToken(LowerName, "left") ||
            HasCompactToken(LowerName, "fl") ||
            HasCompactToken(LowerName, "rl") ||
            HasCompactToken(LowerName, "lf") ||
            HasCompactToken(LowerName, "lr");
    }

    bool NameSuggestsRight(const FString& LowerName)
    {
        return ContainsToken(LowerName, "right") ||
            HasCompactToken(LowerName, "fr") ||
            HasCompactToken(LowerName, "rr") ||
            HasCompactToken(LowerName, "rf");
    }

    bool SlotWantsFront(EWheelSetupSlot Slot)
    {
        return Slot == EWheelSetupSlot::FrontLeft || Slot == EWheelSetupSlot::FrontRight;
    }

    bool SlotWantsLeft(EWheelSetupSlot Slot)
    {
        return Slot == EWheelSetupSlot::FrontLeft || Slot == EWheelSetupSlot::RearLeft;
    }

    const char* SlotWheelName(EWheelSetupSlot Slot)
    {
        switch (Slot)
        {
        case EWheelSetupSlot::FrontLeft: return "FrontLeft";
        case EWheelSetupSlot::FrontRight: return "FrontRight";
        case EWheelSetupSlot::RearLeft: return "RearLeft";
        case EWheelSetupSlot::RearRight: return "RearRight";
        default: return "Wheel";
        }
    }

    const char* SlotDefaultBoneName(EWheelSetupSlot Slot)
    {
        switch (Slot)
        {
        case EWheelSetupSlot::FrontLeft: return "wheel_fl";
        case EWheelSetupSlot::FrontRight: return "wheel_fr";
        case EWheelSetupSlot::RearLeft: return "wheel_rl";
        case EWheelSetupSlot::RearRight: return "wheel_rr";
        default: return "wheel";
        }
    }

    FVector SlotDefaultManualPosition(EWheelSetupSlot Slot)
    {
        switch (Slot)
        {
        case EWheelSetupSlot::FrontLeft: return FVector(1.1f, -0.75f, -0.35f);
        case EWheelSetupSlot::FrontRight: return FVector(1.1f, 0.75f, -0.35f);
        case EWheelSetupSlot::RearLeft: return FVector(-1.1f, -0.75f, -0.35f);
        case EWheelSetupSlot::RearRight: return FVector(-1.1f, 0.75f, -0.35f);
        default: return FVector::ZeroVector;
        }
    }

    FVehicleWheelData MakeDefaultWheelData(EWheelSetupSlot Slot)
    {
        FVehicleWheelData Data;
        const bool bFront = SlotWantsFront(Slot);

        Data.bAffectedBySteering = bFront;
        Data.bAffectedByEngine = !bFront;
        Data.bAffectedByBrake = true;
        Data.bAffectedByHandbrake = !bFront;
        Data.MaxSteerAngleDegrees = bFront ? 35.0f : 0.0f;
        Data.MaxBrakeTorque = 1500.0f;
        Data.MaxHandbrakeTorque = bFront ? 0.0f : 4000.0f;
        return Data;
    }

    FVehicleWheelSetup MakeDefaultWheelSetup(EWheelSetupSlot Slot)
    {
        FVehicleWheelSetup Setup;
        Setup.WheelName = FName(SlotWheelName(Slot));
        Setup.BoneName = FName(SlotDefaultBoneName(Slot));
        Setup.PositionSource = EWheelPositionSource::FromBone;
        Setup.ManualLocalPosition = SlotDefaultManualPosition(Slot);
        Setup.AdditionalOffset = FVector::ZeroVector;
        Setup.WheelData = MakeDefaultWheelData(Slot);
        return Setup;
    }

    struct FWheelDetectionBounds
    {
        float MinX = 0.0f;
        float MaxX = 0.0f;
        float MinY = 0.0f;
        float MaxY = 0.0f;
        float MinZ = 0.0f;
        float MaxZ = 0.0f;

        float CenterX() const { return (MinX + MaxX) * 0.5f; }
        float CenterY() const { return (MinY + MaxY) * 0.5f; }
        float ExtentX() const { return MaxX - MinX; }
        float ExtentY() const { return MaxY - MinY; }
        float ExtentZ() const { return MaxZ - MinZ; }
    };

    struct FWheelAxlePairCandidate
    {
        int32 LeftCandidateIndex = -1;
        int32 RightCandidateIndex = -1;
        float AverageX = 0.0f;
        float AverageZ = 0.0f;
        float Score = 0.0f;
    };

    bool IsCandidateIndexUsedByPair(const TArray<FWheelAxlePairCandidate>& Pairs, int32 CandidateIndex)
    {
        for (const FWheelAxlePairCandidate& Pair : Pairs)
        {
            if (Pair.LeftCandidateIndex == CandidateIndex || Pair.RightCandidateIndex == CandidateIndex)
            {
                return true;
            }
        }
        return false;
    }

    bool ComputeDetectionBounds(const TArray<FWheelBoneCandidate>& Candidates, const TArray<int32>& CandidateIndices, FWheelDetectionBounds& OutBounds)
    {
        if (CandidateIndices.empty())
        {
            return false;
        }

        OutBounds.MinX = OutBounds.MinY = OutBounds.MinZ = std::numeric_limits<float>::max();
        OutBounds.MaxX = OutBounds.MaxY = OutBounds.MaxZ = -std::numeric_limits<float>::max();

        for (int32 CandidateIndex : CandidateIndices)
        {
            if (CandidateIndex < 0 || CandidateIndex >= static_cast<int32>(Candidates.size()))
            {
                continue;
            }

            const FVector& P = Candidates[CandidateIndex].LocalPosition;
            OutBounds.MinX = std::min(OutBounds.MinX, P.X);
            OutBounds.MaxX = std::max(OutBounds.MaxX, P.X);
            OutBounds.MinY = std::min(OutBounds.MinY, P.Y);
            OutBounds.MaxY = std::max(OutBounds.MaxY, P.Y);
            OutBounds.MinZ = std::min(OutBounds.MinZ, P.Z);
            OutBounds.MaxZ = std::max(OutBounds.MaxZ, P.Z);
        }

        return OutBounds.MinX <= OutBounds.MaxX && OutBounds.MinY <= OutBounds.MaxY && OutBounds.MinZ <= OutBounds.MaxZ;
    }

    TArray<int32> BuildStrictWheelCandidateIndexPool(const TArray<FWheelBoneCandidate>& Candidates)
    {
        TArray<int32> NamedPool;
        for (int32 Index = 0; Index < static_cast<int32>(Candidates.size()); ++Index)
        {
            const FWheelBoneCandidate& Candidate = Candidates[Index];
            if (IsPrimaryWheelBoneName(Candidate.LowerName) && IsUsableUnnamedWheelFallbackCandidate(Candidate))
            {
                NamedPool.push_back(Index);
            }
        }

        if (NamedPool.size() >= 2)
        {
            return NamedPool;
        }

        // Names like Bone_001 are common in quick-exported vehicle rigs. Use geometry fallback only
        // for leaf bones and let the symmetry matcher below reject ambiguous or asymmetric groups.
        TArray<int32> GeometryPool;
        for (int32 Index = 0; Index < static_cast<int32>(Candidates.size()); ++Index)
        {
            const FWheelBoneCandidate& Candidate = Candidates[Index];
            if (!Candidate.bHasChildBone &&
                !IsLikelyWheelHelperBoneName(Candidate.LowerName) &&
                IsUsableUnnamedWheelFallbackCandidate(Candidate))
            {
                GeometryPool.push_back(Index);
            }
        }
        return GeometryPool;
    }

    TArray<FWheelAxlePairCandidate> FindSymmetricWheelAxlePairs(const TArray<FWheelBoneCandidate>& Candidates, const TArray<int32>& CandidateIndices)
    {
        TArray<FWheelAxlePairCandidate> Result;
        if (CandidateIndices.size() < 2)
        {
            return Result;
        }

        FWheelDetectionBounds Bounds;
        if (!ComputeDetectionBounds(Candidates, CandidateIndices, Bounds))
        {
            return Result;
        }

        const float ExtentX = std::max(Bounds.ExtentX(), 0.0f);
        const float ExtentY = std::max(Bounds.ExtentY(), 0.0f);
        const float ExtentZ = std::max(Bounds.ExtentZ(), 0.0f);
        if (ExtentY <= 0.001f)
        {
            return Result;
        }

        const float CenterY = Bounds.CenterY();
        const float MinHalfTrack = std::max(0.03f, ExtentY * 0.08f);
        const float MinTrackWidth = std::max(0.08f, ExtentY * 0.20f);
        const float XTolerance = std::max(0.05f, ExtentX * 0.12f);
        const float ZTolerance = std::max(0.05f, ExtentZ * 0.20f);
        const float YSymmetryTolerance = std::max(0.05f, ExtentY * 0.15f);

        TArray<FWheelAxlePairCandidate> PairCandidates;
        for (int32 APoolIndex = 0; APoolIndex < static_cast<int32>(CandidateIndices.size()); ++APoolIndex)
        {
            for (int32 BPoolIndex = APoolIndex + 1; BPoolIndex < static_cast<int32>(CandidateIndices.size()); ++BPoolIndex)
            {
                const int32 AIndex = CandidateIndices[APoolIndex];
                const int32 BIndex = CandidateIndices[BPoolIndex];
                if (AIndex < 0 || BIndex < 0 || AIndex >= static_cast<int32>(Candidates.size()) || BIndex >= static_cast<int32>(Candidates.size()))
                {
                    continue;
                }

                const FWheelBoneCandidate& A = Candidates[AIndex];
                const FWheelBoneCandidate& B = Candidates[BIndex];
                const float AOffsetY = A.LocalPosition.Y - CenterY;
                const float BOffsetY = B.LocalPosition.Y - CenterY;

                if (AOffsetY * BOffsetY >= 0.0f)
                {
                    continue;
                }
                if (std::fabs(AOffsetY) < MinHalfTrack || std::fabs(BOffsetY) < MinHalfTrack)
                {
                    continue;
                }
                if (std::fabs(A.LocalPosition.Y - B.LocalPosition.Y) < MinTrackWidth)
                {
                    continue;
                }

                const float XDelta = std::fabs(A.LocalPosition.X - B.LocalPosition.X);
                const float ZDelta = std::fabs(A.LocalPosition.Z - B.LocalPosition.Z);
                const float YSymmetryDelta = std::fabs(std::fabs(AOffsetY) - std::fabs(BOffsetY));
                if (XDelta > XTolerance || ZDelta > ZTolerance || YSymmetryDelta > YSymmetryTolerance)
                {
                    continue;
                }

                const bool bANameLeft = NameSuggestsLeft(A.LowerName) && !NameSuggestsRight(A.LowerName);
                const bool bANameRight = NameSuggestsRight(A.LowerName) && !NameSuggestsLeft(A.LowerName);
                const bool bBNameLeft = NameSuggestsLeft(B.LowerName) && !NameSuggestsRight(B.LowerName);
                const bool bBNameRight = NameSuggestsRight(B.LowerName) && !NameSuggestsLeft(B.LowerName);

                bool bAIsLeft = A.LocalPosition.Y < B.LocalPosition.Y;
                if (bANameLeft && bBNameRight)
                {
                    bAIsLeft = true;
                }
                else if (bANameRight && bBNameLeft)
                {
                    bAIsLeft = false;
                }

                const FWheelBoneCandidate& LeftCandidate = bAIsLeft ? A : B;
                const FWheelBoneCandidate& RightCandidate = bAIsLeft ? B : A;

                float NamePenalty = 0.0f;
                if (!IsPrimaryWheelBoneName(LeftCandidate.LowerName)) NamePenalty += 0.5f;
                if (!IsPrimaryWheelBoneName(RightCandidate.LowerName)) NamePenalty += 0.5f;

                const bool bLeftNameConflicts = NameSuggestsRight(LeftCandidate.LowerName) && !NameSuggestsLeft(LeftCandidate.LowerName);
                const bool bRightNameConflicts = NameSuggestsLeft(RightCandidate.LowerName) && !NameSuggestsRight(RightCandidate.LowerName);
                if (bLeftNameConflicts || bRightNameConflicts)
                {
                    NamePenalty += 2.0f;
                }

                FWheelAxlePairCandidate Pair;
                Pair.LeftCandidateIndex = bAIsLeft ? AIndex : BIndex;
                Pair.RightCandidateIndex = bAIsLeft ? BIndex : AIndex;
                Pair.AverageX = (A.LocalPosition.X + B.LocalPosition.X) * 0.5f;
                Pair.AverageZ = (A.LocalPosition.Z + B.LocalPosition.Z) * 0.5f;
                Pair.Score = (XDelta / XTolerance) + (ZDelta / ZTolerance) + (YSymmetryDelta / YSymmetryTolerance) + NamePenalty;
                PairCandidates.push_back(Pair);
            }
        }

        std::sort(PairCandidates.begin(), PairCandidates.end(), [](const FWheelAxlePairCandidate& A, const FWheelAxlePairCandidate& B)
        {
            return A.Score < B.Score;
        });

        for (const FWheelAxlePairCandidate& Pair : PairCandidates)
        {
            if (IsCandidateIndexUsedByPair(Result, Pair.LeftCandidateIndex) || IsCandidateIndexUsedByPair(Result, Pair.RightCandidateIndex))
            {
                continue;
            }
            Result.push_back(Pair);
        }

        auto ClassifyPairLongitudinal = [&Candidates](const FWheelAxlePairCandidate& Pair) -> int32
        {
            int32 FrontScore = 0;
            int32 RearScore = 0;
            const int32 Indices[2] = { Pair.LeftCandidateIndex, Pair.RightCandidateIndex };
            for (int32 CandidateIndex : Indices)
            {
                if (CandidateIndex < 0 || CandidateIndex >= static_cast<int32>(Candidates.size()))
                {
                    continue;
                }
                const FString& Name = Candidates[CandidateIndex].LowerName;
                if (NameSuggestsFront(Name) && !NameSuggestsRear(Name)) ++FrontScore;
                if (NameSuggestsRear(Name) && !NameSuggestsFront(Name)) ++RearScore;
            }
            if (FrontScore > RearScore) return 1;
            if (RearScore > FrontScore) return -1;
            return 0;
        };

        std::sort(Result.begin(), Result.end(), [&ClassifyPairLongitudinal](const FWheelAxlePairCandidate& A, const FWheelAxlePairCandidate& B)
        {
            const int32 AClass = ClassifyPairLongitudinal(A);
            const int32 BClass = ClassifyPairLongitudinal(B);
            if (AClass != BClass)
            {
                if (AClass == 1) return true;
                if (BClass == 1) return false;
                if (AClass == -1) return false;
                if (BClass == -1) return true;
            }
            return A.AverageX > B.AverageX;
        });
        return Result;
    }

    FString MakeDetectedWheelName(int32 PairIndex, int32 PairCount, bool bLeft)
    {
        if (PairCount == 1)
        {
            return bLeft ? "Left" : "Right";
        }

        if (PairCount == 2)
        {
            if (PairIndex == 0) return bLeft ? "FrontLeft" : "FrontRight";
            return bLeft ? "RearLeft" : "RearRight";
        }

        return "Axle" + std::to_string(PairIndex + 1) + (bLeft ? "Left" : "Right");
    }

    FVehicleWheelData MakeDetectedWheelData(int32 PairIndex, int32 PairCount)
    {
        FVehicleWheelData Data;
        Data.bAffectedByBrake = true;
        Data.MaxBrakeTorque = 1500.0f;

        if (PairCount == 2)
        {
            const bool bFront = PairIndex == 0;
            Data.bAffectedBySteering = bFront;
            Data.bAffectedByEngine = !bFront;
            Data.bAffectedByHandbrake = !bFront;
            Data.MaxSteerAngleDegrees = bFront ? 35.0f : 0.0f;
            Data.MaxHandbrakeTorque = bFront ? 0.0f : 4000.0f;
        }
        else if (PairCount > 2)
        {
            const bool bFrontMost = PairIndex == 0;
            const bool bRearMost = PairIndex == PairCount - 1;
            Data.bAffectedBySteering = bFrontMost;
            Data.bAffectedByEngine = bRearMost;
            Data.bAffectedByHandbrake = bRearMost;
            Data.MaxSteerAngleDegrees = bFrontMost ? 35.0f : 0.0f;
            Data.MaxHandbrakeTorque = bRearMost ? 4000.0f : 0.0f;
        }

        return Data;
    }

    FVehicleWheelSetup MakeDetectedWheelSetup(const FWheelBoneCandidate& Candidate, int32 PairIndex, int32 PairCount, bool bLeft)
    {
        FVehicleWheelSetup Setup;
        Setup.WheelName = FName(MakeDetectedWheelName(PairIndex, PairCount, bLeft));
        Setup.BoneName = Candidate.BoneName;
        Setup.VisualComponent = Candidate.VisualComponent;
        Setup.PositionSource = Candidate.BoneName.IsValid()
            ? EWheelPositionSource::FromBone
            : EWheelPositionSource::FromVisualComponent;
        Setup.ManualLocalPosition = Candidate.LocalPosition;
        Setup.AdditionalOffset = FVector::ZeroVector;
        Setup.bUseBoneInfluenceSurfaceCenter = false;
        Setup.WheelData = MakeDetectedWheelData(PairIndex, PairCount);
        return Setup;
    }

    bool AreWheelPositionsSymmetricEnough(const TArray<FVector>& Positions)
    {
        if (Positions.size() < 4)
        {
            return true;
        }

        TArray<FWheelBoneCandidate> SyntheticCandidates;
        TArray<int32> CandidateIndices;
        SyntheticCandidates.reserve(Positions.size());
        CandidateIndices.reserve(Positions.size());
        for (int32 Index = 0; Index < static_cast<int32>(Positions.size()); ++Index)
        {
            FWheelBoneCandidate Candidate;
            Candidate.BoneName = FName("WheelPosition");
            Candidate.Name = "WheelPosition";
            Candidate.LowerName = "wheelposition";
            Candidate.LocalPosition = Positions[Index];
            SyntheticCandidates.push_back(Candidate);
            CandidateIndices.push_back(Index);
        }

        const TArray<FWheelAxlePairCandidate> Pairs = FindSymmetricWheelAxlePairs(SyntheticCandidates, CandidateIndices);
        return Pairs.size() * 2 == Positions.size();
    }

    void ApplyWheelDescFromSetup(const FVehicleWheelSetup& Setup, const FVector& LocalPosition, FVehicleWheelDesc& Wheel)
    {
        const FVehicleWheelData& Data = Setup.WheelData;

        Wheel.WheelName     = Setup.WheelName;
        Wheel.BoneName      = Setup.BoneName;
        Wheel.LocalPosition = LocalPosition;
        Wheel.Radius        = Data.Radius;
        Wheel.Width         = Data.Width;
        Wheel.Mass          = Data.Mass;
        Wheel.MOI           = 0.5f * Data.Mass * Data.Radius * Data.Radius;

        Wheel.MaxBrakeTorque     = Data.bAffectedByBrake ? Data.MaxBrakeTorque : 0.0f;
        Wheel.MaxHandbrakeTorque = Data.bAffectedByHandbrake ? Data.MaxHandbrakeTorque : 0.0f;
        Wheel.MaxSteerRadians    = Data.bAffectedBySteering ? Data.MaxSteerAngleDegrees * VehicleDegToRad : 0.0f;

        Wheel.SuspensionMaxCompression   = Data.SuspensionMaxCompression;
        Wheel.SuspensionMaxDroop         = Data.SuspensionMaxDroop;
        Wheel.SuspensionSpringStrength   = Data.SuspensionSpringStrength;
        Wheel.SuspensionSpringDamperRate = Data.SuspensionSpringDamperRate;
        Wheel.TireType                   = 0;
    }
}

void UWheeledVehicleMovementComponent::BeginPlay()
{
    UMovementComponent::BeginPlay();

    bVehicleSimulationStarted = true;
    EnsureDefaultWheelSetups();
    ClampWheelSetups();
    RefreshWheelLocalPositionsFromBones();

    if (VehicleHandle.IsValid())
    {
        RegisterPhysicsSnapshotReceiver();
        return;
    }

    IPhysicsScene* PhysicsScene = GetPhysicsScene(this);
    if (!PhysicsScene || !ResolveVehicleSimulationComponent())
    {
        return;
    }

    VehicleHandle = PhysicsScene->CreateVehicle(BuildVehicleDesc());
    RegisterPhysicsSnapshotReceiver();
}

void UWheeledVehicleMovementComponent::EndPlay()
{
    bVehicleSimulationStarted = false;
    UnregisterPhysicsSnapshotReceiver();

    if (VehicleHandle.IsValid())
    {
        if (IPhysicsScene* PhysicsScene = GetPhysicsScene(this))
        {
            PhysicsScene->DestroyVehicle(VehicleHandle);
        }
        VehicleHandle = FVehicleHandle {};
    }

    bHasLastSnapshot = false;
    UMovementComponent::EndPlay();
}

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!VehicleHandle.IsValid())
    {
        DrawSelectedWheelDebug();
        return;
    }

    if (IPhysicsScene* PhysicsScene = GetPhysicsScene(this))
    {
        PhysicsScene->SetVehicleInput(VehicleHandle, CurrentInput);
    }

    DrawSelectedWheelDebug();
}

void UWheeledVehicleMovementComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    UMovementComponent::PostEditChangeProperty(Event);

    const char* ChangedPropertyName = Event.PropertyName ? Event.PropertyName : "";
    const bool bWheelChanged = IsWheelSetupsPropertyChange(Event);
    const bool bAutoWheelPositionChanged = IsWheelSetupAutoPositionPropertyChange(Event);
    const bool bChassisComponentChanged = NameEquals(ChangedPropertyName, "ChassisComponent") ||
        NameEquals(ChangedPropertyName, "Chassis Component");
    const bool bVehicleDescChanged = IsVehicleDescPropertyChange(Event);

    if (bWheelChanged)
    {
        ClampWheelSetups();
        if (bAutoWheelPositionChanged)
        {
            RefreshWheelLocalPositionsFromBones();
        }
    }

    if (bChassisComponentChanged)
    {
        RefreshWheelLocalPositionsFromBones();
    }

    if (bVehicleDescChanged)
    {
        RecreateVehicleSimulation();
    }
}

void UWheeledVehicleMovementComponent::RecreateVehicleSimulation()
{
    if (!bVehicleSimulationStarted)
    {
        return;
    }

    IPhysicsScene* PhysicsScene = GetPhysicsScene(this);
    if (!PhysicsScene || !ResolveVehicleSimulationComponent())
    {
        return;
    }

    UnregisterPhysicsSnapshotReceiver();

    if (VehicleHandle.IsValid())
    {
        PhysicsScene->DestroyVehicle(VehicleHandle);
        VehicleHandle = FVehicleHandle {};
    }

    bHasLastSnapshot = false;
    VehicleHandle = PhysicsScene->CreateVehicle(BuildVehicleDesc());
    RegisterPhysicsSnapshotReceiver();

    if (VehicleHandle.IsValid())
    {
        PhysicsScene->SetVehicleInput(VehicleHandle, CurrentInput);
    }
}

void UWheeledVehicleMovementComponent::SetDebugSelectedWheelIndex(int32 WheelIndex)
{
    if (WheelIndex < 0 || WheelIndex >= static_cast<int32>(WheelSetups.size()))
    {
        DebugSelectedWheelIndex = -1;
        return;
    }

    DebugSelectedWheelIndex = WheelIndex;
}

void UWheeledVehicleMovementComponent::DrawSelectedWheelDebug() const
{
    if (!bDebugDrawSelectedWheel)
    {
        return;
    }

    DrawWheelDebug(DebugSelectedWheelIndex);
}

void UWheeledVehicleMovementComponent::DrawWheelDebug(int32 WheelIndex) const
{
    UWorld* World = GetWorld();
    const USceneComponent* Chassis = ResolveVehicleSimulationComponent();
    if (!World || !Chassis || WheelIndex < 0 || WheelIndex >= static_cast<int32>(WheelSetups.size()))
    {
        return;
    }

    const FVehicleWheelSetup& Setup = WheelSetups[WheelIndex];
    const FVehicleWheelData& Data = Setup.WheelData;
    const float Radius = std::max(Data.Radius, 0.01f);

    FVector CenterWorld = FVector::ZeroVector;
    FVector RestCenterWorld = FVector::ZeroVector;
    FVector UpWorld = Chassis->GetUpVector();
    FVector ContactPoint = FVector::ZeroVector;
    FVector ContactNormal = FVector::UpVector;
    bool bHasRuntimeWheel = false;
    bool bInAir = true;

    if (bHasLastSnapshot && WheelIndex < static_cast<int32>(LastSnapshot.Wheels.size()))
    {
        const FVehicleWheelSnapshot& WheelSnapshot = LastSnapshot.Wheels[WheelIndex];
        CenterWorld = WheelSnapshot.WorldTransform.Location;
        RestCenterWorld = LastSnapshot.ChassisWorldTransform.Location +
            LastSnapshot.ChassisWorldTransform.Rotation.RotateVector(WheelSnapshot.RestLocalPosition);
        UpWorld = LastSnapshot.ChassisWorldTransform.Rotation.GetUpVector();
        ContactPoint = WheelSnapshot.ContactPoint;
        ContactNormal = WheelSnapshot.ContactNormal;
        bInAir = WheelSnapshot.bInAir;
        bHasRuntimeWheel = true;
    }
    else
    {
        const FVector LocalPosition = ResolveWheelLocalPosition(Setup);
        CenterWorld = TransformPositionNoScale(Chassis, LocalPosition);
        RestCenterWorld = CenterWorld;
    }

    const FVector TireBottomWorld = CenterWorld - UpWorld * Radius;
    const FVector SuspensionTopWorld = RestCenterWorld + UpWorld * std::max(Data.SuspensionMaxCompression, 0.0f);
    const FVector SuspensionBottomWorld = RestCenterWorld - UpWorld * std::max(Data.SuspensionMaxDroop, 0.0f);
    FVector VisualComponentLocalCenter;
    if (TryResolveVisualComponentLocalPosition(Setup, VisualComponentLocalCenter))
    {
        const FVector VisualComponentWorldCenter = TransformPositionNoScale(Chassis, VisualComponentLocalCenter);
        DrawDebugPoint(World, VisualComponentWorldCenter, 0.08f, FColor(255, 0, 255));
        DrawDebugLine(World, VisualComponentWorldCenter, CenterWorld, FColor(255, 0, 255));
    }

    if (Setup.PositionSource == EWheelPositionSource::FromBone && Setup.bUseBoneInfluenceSurfaceCenter)
    {
        FVector BonePivotLocalCenter;
        if (TryResolveBoneLocalPosition(Setup.BoneName, false, BonePivotLocalCenter))
        {
            const FVector BonePivotWorldCenter = TransformPositionNoScale(Chassis, BonePivotLocalCenter);
            DrawDebugPoint(World, BonePivotWorldCenter, 0.07f, FColor(255, 150, 0));
            DrawDebugLine(World, BonePivotWorldCenter, CenterWorld, FColor(255, 150, 0));
        }
    }

    DrawDebugSphere(World, CenterWorld, Radius, 24, FColor(0, 210, 255));
    DrawDebugPoint(World, CenterWorld, 0.06f, FColor::Yellow());
    DrawDebugLine(World, SuspensionTopWorld, SuspensionBottomWorld, FColor(120, 120, 120));
    DrawDebugLine(World, RestCenterWorld, CenterWorld, FColor(80, 160, 255));
    DrawDebugLine(World, CenterWorld, TireBottomWorld, FColor::Yellow());

    if (bHasRuntimeWheel)
    {
        if (bInAir)
        {
            DrawDebugLine(World, TireBottomWorld, TireBottomWorld - UpWorld * 0.20f, FColor::Red());
        }
        else
        {
            DrawDebugPoint(World, ContactPoint, 0.08f, FColor::Green());
            DrawDebugLine(World, CenterWorld, ContactPoint, FColor::Green());
            DrawDebugLine(World, ContactPoint, ContactPoint + ContactNormal * 0.30f, FColor::Blue());
        }
    }
}

void UWheeledVehicleMovementComponent::PostDuplicate()
{
    UMovementComponent::PostDuplicate();
    ClampWheelSetups();
    RefreshWheelLocalPositionsFromBones();
}

void UWheeledVehicleMovementComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
    UMovementComponent::AddReferencedObjects(Collector);

    Collector.AddReferencedObject(ChassisComponent, "UWheeledVehicleMovementComponent.ChassisComponent");
    for (FVehicleWheelSetup& Setup : WheelSetups)
    {
        Collector.AddReferencedObject(Setup.VisualComponent, "UWheeledVehicleMovementComponent.WheelVisualComponent");
    }
}

void UWheeledVehicleMovementComponent::SetThrottleInput(float InThrottle)
{
    CurrentInput.Throttle = std::clamp(InThrottle, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetBrakeInput(float InBrake)
{
    CurrentInput.Brake = std::clamp(InBrake, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetSteeringInput(float InSteering)
{
    CurrentInput.Steering = std::clamp(InSteering, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetHandbrakeInput(float InHandbrake)
{
    CurrentInput.Handbrake = std::clamp(InHandbrake, 0.0f, 1.0f);
}

float UWheeledVehicleMovementComponent::GetForwardSpeed() const
{
    if (bHasLastSnapshot)
    {
        return LastSnapshot.LinearVelocity.Dot(LastSnapshot.ChassisWorldTransform.Rotation.GetForwardVector());
    }

    if (const USceneComponent* SimulationComponent = ResolveVehicleSimulationComponent())
    {
        return SimulationComponent->GetForwardVector().Dot(FVector::ZeroVector);
    }

    return 0.0f;
}

void UWheeledVehicleMovementComponent::ResetVehicle()
{
    if (!VehicleHandle.IsValid())
    {
        return;
    }

    IPhysicsScene*   PhysicsScene = GetPhysicsScene(this);
    USceneComponent* Chassis      = ResolveVehicleSimulationComponent();
    if (!PhysicsScene || !Chassis)
    {
        return;
    }

    PhysicsScene->ResetVehicle(VehicleHandle, BuildVehicleDesc().WorldTransform);
}

const FVehicleWheelSetup* UWheeledVehicleMovementComponent::FindWheelSetup(const FName& WheelName) const
{
    for (const FVehicleWheelSetup& Setup : WheelSetups)
    {
        if (Setup.WheelName == WheelName)
        {
            return &Setup;
        }
    }
    return nullptr;
}

USceneComponent* UWheeledVehicleMovementComponent::ResolveOwnedSceneComponent(USceneComponent* Component) const
{
    if (!IsValid(Component))
    {
        return nullptr;
    }

    AActor* Owner = GetOwner();
    if (!Owner || Component->GetOwner() != Owner)
    {
        return nullptr;
    }

    for (UActorComponent* OwnedComponent : Owner->GetComponents())
    {
        if (OwnedComponent == Component)
        {
            return Component;
        }
    }

    return nullptr;
}

USceneComponent* UWheeledVehicleMovementComponent::FindAutoStaticChassisComponent() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    UStaticMeshComponent* BestComponent = nullptr;
    float BestVolume = -1.0f;
    for (UActorComponent* Component : Owner->GetComponents())
    {
        UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
        if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh())
        {
            continue;
        }

        const FString LowerName = ToLowerName(StaticMeshComponent->GetName());
        if (IsPrimaryWheelBoneName(LowerName) || IsLikelyWheelBoneName(LowerName))
        {
            continue;
        }

        float Volume = 0.0f;
        const FBoundingBox Bounds = StaticMeshComponent->GetWorldBoundingBox();
        if (Bounds.IsValid())
        {
            const FVector Extents = Bounds.GetExtent();
            Volume = std::max(Extents.X, 0.0f) * std::max(Extents.Y, 0.0f) * std::max(Extents.Z, 0.0f);
        }

        if (Volume > BestVolume)
        {
            BestVolume = Volume;
            BestComponent = StaticMeshComponent;
        }
    }

    return BestComponent;
}

USceneComponent* UWheeledVehicleMovementComponent::ResolveExplicitChassisComponent() const
{
    return ResolveOwnedSceneComponent(ChassisComponent.GetRaw());
}

USceneComponent* UWheeledVehicleMovementComponent::ResolveVehicleChassisComponent() const
{
    if (USceneComponent* ExplicitChassis = ResolveExplicitChassisComponent())
    {
        return ExplicitChassis;
    }

    // This is the visual/collision body selector, not necessarily the component that is
    // driven by the vehicle simulation. For static-mesh-only vehicles the body is often a
    // scaled child under an empty skeletal root; using that child as the simulation frame
    // folds the child scale into wheel coordinates and leaves the root behind.
    if (USceneComponent* AutoStaticChassis = FindAutoStaticChassisComponent())
    {
        return AutoStaticChassis;
    }

    USceneComponent* UpdatedComponent = UMovementComponent::GetUpdatedComponent();
    if (UpdatedComponent)
    {
        if (USkinnedMeshComponent* UpdatedSkinnedMesh = Cast<USkinnedMeshComponent>(UpdatedComponent))
        {
            return UpdatedSkinnedMesh->GetSkeletalMesh() ? UpdatedComponent : nullptr;
        }

        const FString UpdatedNameLower = ToLowerName(UpdatedComponent->GetName());
        if (Cast<UStaticMeshComponent>(UpdatedComponent) &&
            !IsPrimaryWheelBoneName(UpdatedNameLower) &&
            !IsLikelyWheelBoneName(UpdatedNameLower))
        {
            return UpdatedComponent;
        }
    }

    return UpdatedComponent;
}

USceneComponent* UWheeledVehicleMovementComponent::ResolveVehicleSimulationComponent() const
{
    AActor* Owner = GetOwner();
    USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;

    // Movement must drive the actor/root frame. The chassis visual may be a scaled child,
    // and driving that child directly makes the root stay behind while wheel coordinates
    // are measured through the child's non-uniform scale. Even if UpdatedComponent was
    // accidentally set to a child body, prefer the owner root as the simulation frame.
    if (Root)
    {
        return Root;
    }

    if (USceneComponent* Updated = UMovementComponent::GetUpdatedComponent())
    {
        if (ResolveOwnedSceneComponent(Updated))
        {
            return Updated;
        }
    }

    if (USceneComponent* ExplicitChassis = ResolveExplicitChassisComponent())
    {
        return ExplicitChassis;
    }

    return FindAutoStaticChassisComponent();
}

void UWheeledVehicleMovementComponent::EnsureDefaultWheelSetups()
{
    if (!WheelSetups.empty())
    {
        return;
    }

    // Prefer the actual component layout over fabricating bone-based defaults. This keeps
    // static-mesh-only vehicle actors usable when PIE starts before the user has saved
    // generated WheelSetups into the scene.
    if (AutoGenerateWheelSetupsFromSkeleton())
    {
        return;
    }

    WheelSetups.push_back(MakeDefaultWheelSetup(EWheelSetupSlot::FrontLeft));
    WheelSetups.push_back(MakeDefaultWheelSetup(EWheelSetupSlot::FrontRight));
    WheelSetups.push_back(MakeDefaultWheelSetup(EWheelSetupSlot::RearLeft));
    WheelSetups.push_back(MakeDefaultWheelSetup(EWheelSetupSlot::RearRight));
}

USkinnedMeshComponent* UWheeledVehicleMovementComponent::FindWheelSetupSkinnedMeshComponent() const
{
    if (USkinnedMeshComponent* UpdatedMesh = Cast<USkinnedMeshComponent>(ResolveVehicleChassisComponent()))
    {
        if (UpdatedMesh->GetSkeletalMesh())
        {
            return UpdatedMesh;
        }
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    USkinnedMeshComponent* FirstSkinnedMesh = nullptr;
    for (UActorComponent* Component : Owner->GetComponents())
    {
        USkinnedMeshComponent* Candidate = Cast<USkinnedMeshComponent>(Component);
        if (!Candidate || !Candidate->GetSkeletalMesh())
        {
            continue;
        }

        if (!FirstSkinnedMesh)
        {
            FirstSkinnedMesh = Candidate;
        }

        if (Candidate == Owner->GetRootComponent())
        {
            return Candidate;
        }
    }

    return FirstSkinnedMesh;
}

USceneComponent* UWheeledVehicleMovementComponent::ResolveWheelVisualComponent(const FVehicleWheelSetup& Setup) const
{
    return FindWheelVisualSceneComponent(Setup);
}

USceneComponent* UWheeledVehicleMovementComponent::FindWheelVisualSceneComponent(const FVehicleWheelSetup& Setup) const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    return ResolveOwnedSceneComponent(Setup.VisualComponent.GetRaw());
}

bool UWheeledVehicleMovementComponent::TryResolveVisualComponentLocalPosition(const FVehicleWheelSetup& Setup, FVector& OutLocalPosition) const
{
    USceneComponent* VisualComponent = FindWheelVisualSceneComponent(Setup);
    if (!VisualComponent)
    {
        return false;
    }

    FVector SourceWorldPosition = VisualComponent->GetWorldLocation();
    if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(VisualComponent))
    {
        const FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
        if (Bounds.IsValid())
        {
            SourceWorldPosition = Bounds.GetCenter();
        }
    }

    if (const USceneComponent* SimulationComponent = ResolveVehicleSimulationComponent())
    {
        OutLocalPosition = InverseTransformPositionNoScale(SimulationComponent, SourceWorldPosition);
        return true;
    }

    return false;
}

bool UWheeledVehicleMovementComponent::TryResolveBoneLocalPosition(const FName& BoneName, bool bUseInfluenceSurfaceCenter, FVector& OutLocalPosition) const
{
    if (!BoneName.IsValid() || BoneName == FName::None)
    {
        return false;
    }

    USkinnedMeshComponent* Mesh = FindWheelSetupSkinnedMeshComponent();
    if (!Mesh || !Mesh->GetSkeletalMesh())
    {
        return false;
    }

    const int32 BoneIndex = Mesh->FindBoneIndex(BoneName);
    if (BoneIndex < 0)
    {
        return false;
    }

    FVector SourceWorldPosition = Mesh->GetBoneLocationByIndex(BoneIndex);
    if (bUseInfluenceSurfaceCenter)
    {
        FSkeletalMesh* MeshAsset = Mesh->GetSkeletalMesh()->GetSkeletalMeshAsset();
        FVector SurfaceMeshLocalCenter;
        if (TryComputeBoneInfluenceSurfaceCenter(MeshAsset, BoneIndex, SurfaceMeshLocalCenter))
        {
            SourceWorldPosition = Mesh->GetWorldMatrix().TransformPositionWithW(SurfaceMeshLocalCenter);
        }
    }

    if (const USceneComponent* SimulationComponent = ResolveVehicleSimulationComponent())
    {
        OutLocalPosition = InverseTransformPositionNoScale(SimulationComponent, SourceWorldPosition);
        return true;
    }

    OutLocalPosition = Mesh->GetWorldMatrix().GetInverse().TransformPositionWithW(SourceWorldPosition);
    return true;
}

FVector UWheeledVehicleMovementComponent::ResolveWheelLocalPosition(const FVehicleWheelSetup& Setup) const
{
    FVector ResolvedLocalPosition;

    if (Setup.PositionSource == EWheelPositionSource::FromVisualComponent)
    {
        if (TryResolveVisualComponentLocalPosition(Setup, ResolvedLocalPosition))
        {
            return ResolvedLocalPosition + Setup.AdditionalOffset;
        }
    }
    else if (Setup.PositionSource == EWheelPositionSource::FromBone)
    {
        if (TryResolveBoneLocalPosition(Setup.BoneName, Setup.bUseBoneInfluenceSurfaceCenter, ResolvedLocalPosition))
        {
            return ResolvedLocalPosition + Setup.AdditionalOffset;
        }

        // Skeletal-less fallback: keep existing wheel setups usable when a vehicle is built
        // from separate StaticMeshComponent wheels instead of a single skinned mesh.
        if (TryResolveVisualComponentLocalPosition(Setup, ResolvedLocalPosition))
        {
            return ResolvedLocalPosition + Setup.AdditionalOffset;
        }
    }

    return Setup.ManualLocalPosition + Setup.AdditionalOffset;
}

int32 UWheeledVehicleMovementComponent::RefreshWheelLocalPositionsFromBones()
{
    int32 ResolvedCount = 0;
    for (FVehicleWheelSetup& Setup : WheelSetups)
    {
        if (Setup.PositionSource == EWheelPositionSource::Manual)
        {
            continue;
        }

        FVector ResolvedLocalPosition;
        bool bResolved = false;
        if (Setup.PositionSource == EWheelPositionSource::FromVisualComponent)
        {
            bResolved = TryResolveVisualComponentLocalPosition(Setup, ResolvedLocalPosition);
        }
        else if (Setup.PositionSource == EWheelPositionSource::FromBone)
        {
            bResolved = TryResolveBoneLocalPosition(Setup.BoneName, Setup.bUseBoneInfluenceSurfaceCenter, ResolvedLocalPosition) ||
                TryResolveVisualComponentLocalPosition(Setup, ResolvedLocalPosition);
        }

        if (bResolved)
        {
            Setup.ManualLocalPosition = ResolvedLocalPosition;
            ++ResolvedCount;
        }
    }
    return ResolvedCount;
}

void UWheeledVehicleMovementComponent::ClampWheelData(FVehicleWheelData& WheelData) const
{
    WheelData.Radius = std::max(WheelData.Radius, 0.01f);
    WheelData.Width = std::max(WheelData.Width, 0.01f);
    WheelData.Mass = std::max(WheelData.Mass, 0.01f);
    WheelData.TireFriction = std::max(WheelData.TireFriction, 0.01f);
    WheelData.SuspensionMaxCompression = std::max(WheelData.SuspensionMaxCompression, 0.0f);
    WheelData.SuspensionMaxDroop = std::max(WheelData.SuspensionMaxDroop, 0.0f);
    WheelData.SuspensionSpringStrength = std::max(WheelData.SuspensionSpringStrength, 0.0f);
    WheelData.SuspensionSpringDamperRate = std::max(WheelData.SuspensionSpringDamperRate, 0.0f);
    WheelData.MaxBrakeTorque = std::max(WheelData.MaxBrakeTorque, 0.0f);
    WheelData.MaxHandbrakeTorque = std::max(WheelData.MaxHandbrakeTorque, 0.0f);
    WheelData.MaxSteerAngleDegrees = std::max(WheelData.MaxSteerAngleDegrees, 0.0f);
}

void UWheeledVehicleMovementComponent::ClampWheelSetups()
{
    for (FVehicleWheelSetup& Setup : WheelSetups)
    {
        ClampWheelData(Setup.WheelData);
    }

    if (DebugSelectedWheelIndex >= static_cast<int32>(WheelSetups.size()))
    {
        DebugSelectedWheelIndex = WheelSetups.empty() ? -1 : static_cast<int32>(WheelSetups.size()) - 1;
    }
}

bool UWheeledVehicleMovementComponent::AutoGenerateWheelSetupsFromSkeleton()
{
    USkinnedMeshComponent* Mesh = FindWheelSetupSkinnedMeshComponent();
    USkeletalMesh* SkeletalMesh = Mesh ? Mesh->GetSkeletalMesh() : nullptr;
    const FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    if (!Mesh || !SkeletalMesh || !Asset || Asset->Bones.empty())
    {
        return AutoGenerateWheelSetupsFromStaticMeshComponents();
    }

    TArray<FWheelBoneCandidate> Candidates;
    Candidates.reserve(Asset->Bones.size());

    TArray<int32> BoneIndexToCandidateIndex(Asset->Bones.size(), -1);
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
    {
        const FBone& Bone = Asset->Bones[BoneIndex];
        if (Bone.Name.empty())
        {
            continue;
        }

        FWheelBoneCandidate Candidate;
        Candidate.BoneName = FName(Bone.Name);
        Candidate.Name = Bone.Name;
        Candidate.LowerName = ToLowerName(Bone.Name);
        Candidate.ParentBoneIndex = Bone.ParentIndex;
        TryResolveBoneLocalPosition(Candidate.BoneName, false, Candidate.LocalPosition);
        BoneIndexToCandidateIndex[BoneIndex] = static_cast<int32>(Candidates.size());
        Candidates.push_back(Candidate);
    }

    if (Candidates.empty())
    {
        return AutoGenerateWheelSetupsFromStaticMeshComponents();
    }

    for (FWheelBoneCandidate& Candidate : Candidates)
    {
        const int32 ParentBoneIndex = Candidate.ParentBoneIndex;
        if (ParentBoneIndex < 0 || ParentBoneIndex >= static_cast<int32>(BoneIndexToCandidateIndex.size()))
        {
            continue;
        }

        const int32 ParentCandidateIndex = BoneIndexToCandidateIndex[ParentBoneIndex];
        if (ParentCandidateIndex >= 0 && ParentCandidateIndex < static_cast<int32>(Candidates.size()))
        {
            Candidates[ParentCandidateIndex].bHasChildBone = true;
        }
    }

    const TArray<int32> CandidatePool = BuildStrictWheelCandidateIndexPool(Candidates);
    if (CandidatePool.size() < 2)
    {
        return AutoGenerateWheelSetupsFromStaticMeshComponents();
    }

    const TArray<FWheelAxlePairCandidate> AxlePairs = FindSymmetricWheelAxlePairs(Candidates, CandidatePool);
    if (AxlePairs.empty())
    {
        return AutoGenerateWheelSetupsFromStaticMeshComponents();
    }

    TArray<FVehicleWheelSetup> GeneratedSetups;
    GeneratedSetups.reserve(AxlePairs.size() * 2);

    const int32 PairCount = static_cast<int32>(AxlePairs.size());
    for (int32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
    {
        const FWheelAxlePairCandidate& Pair = AxlePairs[PairIndex];
        if (Pair.LeftCandidateIndex < 0 || Pair.RightCandidateIndex < 0 ||
            Pair.LeftCandidateIndex >= static_cast<int32>(Candidates.size()) ||
            Pair.RightCandidateIndex >= static_cast<int32>(Candidates.size()))
        {
            continue;
        }

        GeneratedSetups.push_back(MakeDetectedWheelSetup(Candidates[Pair.LeftCandidateIndex], PairIndex, PairCount, true));
        GeneratedSetups.push_back(MakeDetectedWheelSetup(Candidates[Pair.RightCandidateIndex], PairIndex, PairCount, false));
    }

    if (GeneratedSetups.empty())
    {
        return AutoGenerateWheelSetupsFromStaticMeshComponents();
    }

    if (!ResolveOwnedSceneComponent(ChassisComponent.GetRaw()) && Mesh)
    {
        ChassisComponent = Mesh;
    }

    WheelSetups = std::move(GeneratedSetups);
    ClampWheelSetups();
    RefreshWheelLocalPositionsFromBones();
    return true;
}

bool UWheeledVehicleMovementComponent::AutoGenerateWheelSetupsFromStaticMeshComponents()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    const USceneComponent* SimulationComponent = ResolveVehicleSimulationComponent();
    if (!SimulationComponent)
    {
        return false;
    }

    const USceneComponent* Chassis = ResolveExplicitChassisComponent();
    if (!Chassis)
    {
        Chassis = FindAutoStaticChassisComponent();
    }

    TArray<FWheelBoneCandidate> Candidates;
    TArray<int32> NamedCandidateIndices;
    for (UActorComponent* Component : Owner->GetComponents())
    {
        UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
        if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh())
        {
            continue;
        }

        // The chassis body is commonly a StaticMeshComponent too. Exclude the resolved body
        // so unnamed wheels can be detected by layout instead of being polluted by the body cube.
        if (StaticMeshComponent == Chassis)
        {
            continue;
        }

        FVector WorldCenter = StaticMeshComponent->GetWorldLocation();
        const FBoundingBox Bounds = StaticMeshComponent->GetWorldBoundingBox();
        if (Bounds.IsValid())
        {
            WorldCenter = Bounds.GetCenter();
        }

        FWheelBoneCandidate Candidate;
        Candidate.BoneName = FName::None;
        Candidate.VisualComponent = StaticMeshComponent;
        Candidate.Name = StaticMeshComponent->GetName();
        Candidate.LowerName = ToLowerName(Candidate.Name);
        Candidate.LocalPosition = InverseTransformPositionNoScale(SimulationComponent, WorldCenter);

        const int32 CandidateIndex = static_cast<int32>(Candidates.size());
        if (IsPrimaryWheelBoneName(Candidate.LowerName))
        {
            NamedCandidateIndices.push_back(CandidateIndex);
        }
        Candidates.push_back(Candidate);
    }

    if (Candidates.size() < 2)
    {
        return false;
    }

    // Prefer explicit wheel/tire names when available, but fall back to pure geometry.
    // This is required for imported/demo scenes where every component is Name_None.
    TArray<int32> CandidateIndices = NamedCandidateIndices.size() >= 2
        ? NamedCandidateIndices
        : BuildStrictWheelCandidateIndexPool(Candidates);
    if (CandidateIndices.size() < 2)
    {
        return false;
    }

    TArray<FWheelAxlePairCandidate> Pairs = FindSymmetricWheelAxlePairs(Candidates, CandidateIndices);
    if (Pairs.empty())
    {
        return false;
    }

    TArray<FVehicleWheelSetup> GeneratedSetups;
    const int32 PairCount = static_cast<int32>(Pairs.size());
    GeneratedSetups.reserve(PairCount * 2);
    for (int32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
    {
        const FWheelAxlePairCandidate& Pair = Pairs[PairIndex];
        if (Pair.LeftCandidateIndex < 0 || Pair.RightCandidateIndex < 0 ||
            Pair.LeftCandidateIndex >= static_cast<int32>(Candidates.size()) ||
            Pair.RightCandidateIndex >= static_cast<int32>(Candidates.size()))
        {
            continue;
        }

        GeneratedSetups.push_back(MakeDetectedWheelSetup(Candidates[Pair.LeftCandidateIndex], PairIndex, PairCount, true));
        GeneratedSetups.push_back(MakeDetectedWheelSetup(Candidates[Pair.RightCandidateIndex], PairIndex, PairCount, false));
    }

    if (GeneratedSetups.empty())
    {
        return false;
    }

    if (!ResolveOwnedSceneComponent(ChassisComponent.GetRaw()) && Chassis)
    {
        ChassisComponent = const_cast<USceneComponent*>(Chassis);
    }

    WheelSetups = std::move(GeneratedSetups);
    ClampWheelSetups();
    RefreshWheelLocalPositionsFromBones();
    return true;
}

bool UWheeledVehicleMovementComponent::ValidateWheelSetups(TArray<FString>& OutMessages) const
{
    OutMessages.clear();

    if (WheelSetups.size() != VehicleSetupWheelCount)
    {
        OutMessages.push_back("PhysX Drive4W backend can simulate only exactly 4 wheel setups. Auto detection keeps the real detected count instead of fabricating missing wheels.");
    }

    TArray<FName> UsedWheelNames;
    TArray<FName> UsedBoneNames;
    TArray<FVector> ResolvedWheelPositions;
    for (int32 Index = 0; Index < static_cast<int32>(WheelSetups.size()); ++Index)
    {
        const FVehicleWheelSetup& Setup = WheelSetups[Index];
        const FString Prefix = "WheelSetups[" + std::to_string(Index) + "]: ";

        if (!Setup.WheelName.IsValid() || Setup.WheelName == FName::None)
        {
            OutMessages.push_back(Prefix + "Wheel Name is empty.");
        }
        else if (ContainsName(UsedWheelNames, Setup.WheelName))
        {
            OutMessages.push_back(Prefix + "Duplicate Wheel Name: " + Setup.WheelName.ToString());
        }
        UsedWheelNames.push_back(Setup.WheelName);

        if (Setup.PositionSource == EWheelPositionSource::FromBone)
        {
            FVector LocalPosition;
            if (!TryResolveBoneLocalPosition(Setup.BoneName, Setup.bUseBoneInfluenceSurfaceCenter, LocalPosition))
            {
                if (!TryResolveVisualComponentLocalPosition(Setup, LocalPosition))
                {
                    OutMessages.push_back(Prefix + "Bone Name cannot be resolved from the current skeletal mesh and no Visual Component reference was assigned. Set Position Source to FromVisualComponent and choose Visual Component, or use Manual: " + Setup.BoneName.ToString());
                }
            }
        }
        else if (Setup.PositionSource == EWheelPositionSource::FromVisualComponent)
        {
            FVector LocalPosition;
            if (!TryResolveVisualComponentLocalPosition(Setup, LocalPosition))
            {
                OutMessages.push_back(Prefix + "Visual Component reference cannot be resolved. Assign a component owned by the vehicle actor.");
            }
        }

        if (Setup.BoneName.IsValid() && Setup.BoneName != FName::None)
        {
            if (ContainsName(UsedBoneNames, Setup.BoneName))
            {
                OutMessages.push_back(Prefix + "Duplicate Bone Name: " + Setup.BoneName.ToString());
            }
            UsedBoneNames.push_back(Setup.BoneName);
        }

        if (Setup.WheelData.Radius <= 0.0f) OutMessages.push_back(Prefix + "Radius must be greater than 0.");
        if (Setup.WheelData.Width <= 0.0f) OutMessages.push_back(Prefix + "Width must be greater than 0.");
        if (Setup.WheelData.Mass <= 0.0f) OutMessages.push_back(Prefix + "Mass must be greater than 0.");
        if (Setup.WheelData.bAffectedBySteering && Setup.WheelData.MaxSteerAngleDegrees <= 0.0f)
        {
            OutMessages.push_back(Prefix + "Affected By Steering is enabled but Max Steer Angle is 0.");
        }
        if (Setup.WheelData.bAffectedByBrake && Setup.WheelData.MaxBrakeTorque <= 0.0f)
        {
            OutMessages.push_back(Prefix + "Affected By Brake is enabled but Max Brake Torque is 0.");
        }
        if (Setup.WheelData.bAffectedByHandbrake && Setup.WheelData.MaxHandbrakeTorque <= 0.0f)
        {
            OutMessages.push_back(Prefix + "Affected By Handbrake is enabled but Max Handbrake Torque is 0.");
        }

        ResolvedWheelPositions.push_back(ResolveWheelLocalPosition(Setup));
    }

    if (!AreWheelPositionsSymmetricEnough(ResolvedWheelPositions))
    {
        OutMessages.push_back("Wheel positions do not form clean symmetric left/right axle pairs. Check Bone Name / Visual Component assignments, or use Manual Local Position overrides.");
    }

    if (!ResolvedWheelPositions.empty())
    {
        const FVector CollisionOffset = ResolveChassisCollisionOffset(ResolvedWheelPositions);
        float LowestTireBottom = std::numeric_limits<float>::max();
        for (int32 WheelIndex = 0; WheelIndex < static_cast<int32>(ResolvedWheelPositions.size()); ++WheelIndex)
        {
            float Radius = 0.35f;
            if (WheelIndex >= 0 && WheelIndex < static_cast<int32>(WheelSetups.size()))
            {
                Radius = std::max(WheelSetups[WheelIndex].WheelData.Radius, 0.01f);
            }
            LowestTireBottom = std::min(LowestTireBottom, ResolvedWheelPositions[WheelIndex].Z - Radius);
        }

        const float ChassisBottom = CollisionOffset.Z - std::max(ChassisHalfExtents.Z, 0.01f);
        if (ChassisBottom < LowestTireBottom - 0.01f)
        {
            OutMessages.push_back("Chassis collision box is below the tire bottom. Enable Auto Fit Collision Offset From Wheels or raise Chassis Collision Offset Z, otherwise the chassis can hit the ground before the wheels settle.");
        }
    }

    return OutMessages.empty();
}

FVector UWheeledVehicleMovementComponent::ComputeAutoChassisCollisionOffset(const TArray<FVector>& ResolvedWheelPositions) const
{
    if (ResolvedWheelPositions.empty())
    {
        return ChassisCollisionOffset;
    }

    FVector LocalMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    FVector LocalMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

    float LowestTireBottom = std::numeric_limits<float>::max();
    for (int32 WheelIndex = 0; WheelIndex < static_cast<int32>(ResolvedWheelPositions.size()); ++WheelIndex)
    {
        const FVector& Position = ResolvedWheelPositions[WheelIndex];
        LocalMin.X = std::min(LocalMin.X, Position.X);
        LocalMin.Y = std::min(LocalMin.Y, Position.Y);
        LocalMin.Z = std::min(LocalMin.Z, Position.Z);
        LocalMax.X = std::max(LocalMax.X, Position.X);
        LocalMax.Y = std::max(LocalMax.Y, Position.Y);
        LocalMax.Z = std::max(LocalMax.Z, Position.Z);

        float Radius = 0.35f;
        if (WheelIndex >= 0 && WheelIndex < static_cast<int32>(WheelSetups.size()))
        {
            Radius = std::max(WheelSetups[WheelIndex].WheelData.Radius, 0.01f);
        }
        LowestTireBottom = std::min(LowestTireBottom, Position.Z - Radius);
    }

    FVector Offset = ChassisCollisionOffset;
    Offset.X = (LocalMin.X + LocalMax.X) * 0.5f;
    Offset.Y = (LocalMin.Y + LocalMax.Y) * 0.5f;
    Offset.Z = LowestTireBottom + std::max(ChassisGroundClearance, 0.0f) + std::max(ChassisHalfExtents.Z, 0.01f);
    return Offset;
}

FVector UWheeledVehicleMovementComponent::ResolveChassisCollisionOffset(const TArray<FVector>& ResolvedWheelPositions) const
{
    if (bAutoFitChassisCollisionOffset)
    {
        return ComputeAutoChassisCollisionOffset(ResolvedWheelPositions);
    }

    return ChassisCollisionOffset;
}

FQuat UWheeledVehicleMovementComponent::ResolveVisualToSimulationRotation(const TArray<FVector>& ResolvedWheelPositions) const
{
    if (!bAutoAlignSimulationForwardFromWheelLayout || ResolvedWheelPositions.size() < 4)
    {
        return FQuat::Identity;
    }

    const float FrontX = (ResolvedWheelPositions[0].X + ResolvedWheelPositions[1].X) * 0.5f;
    const float RearX = (ResolvedWheelPositions[2].X + ResolvedWheelPositions[3].X) * 0.5f;

    if (FrontX < RearX)
    {
        return FQuat::FromAxisAngle(FVector::ZAxisVector, 3.14159265358979323846f);
    }

    return FQuat::Identity;
}

uint32 UWheeledVehicleMovementComponent::BuildDrivableSurfaceMask() const
{
    uint32 Mask = 0;

    if (bWheelsTraceWorldStatic)
    {
        Mask |= ObjectTypeBit(ECollisionChannel::WorldStatic);
    }
    if (bWheelsTraceWorldDynamic)
    {
        Mask |= ObjectTypeBit(ECollisionChannel::WorldDynamic);
    }
    if (bWheelsTracePawn)
    {
        Mask |= ObjectTypeBit(ECollisionChannel::Pawn);
    }
    if (bWheelsTraceProjectile)
    {
        Mask |= ObjectTypeBit(ECollisionChannel::Projectile);
    }

    return Mask != 0 ? Mask : ObjectTypeBit(ECollisionChannel::WorldStatic);
}

FVehicleDesc UWheeledVehicleMovementComponent::BuildVehicleDesc() const
{
    FVehicleDesc Desc;

    const AActor*          OwnerActor = GetOwner();
    const USceneComponent* Chassis    = ResolveVehicleSimulationComponent();

    Desc.Owner.ActorId             = OwnerActor ? OwnerActor->GetUUID() : 0;
    Desc.Owner.ComponentId         = GetUUID();
    Desc.Owner.ComponentGeneration = 1;
    Desc.Owner.Domain              = EPhysicsBodyDomain::Vehicle;

    FTransform VisualWorldTransform;
    if (Chassis)
    {
        VisualWorldTransform = FTransform(Chassis->GetWorldLocation(), Chassis->GetWorldRotation(), FVector(1.0f, 1.0f, 1.0f));
    }

    Desc.ChassisHalfExtents = ChassisHalfExtents;
    Desc.ChassisMass        = std::max(ChassisMass, 1.0f);

    Desc.EnginePeakTorque = EnginePeakTorque;
    Desc.EngineMaxOmega   = EngineMaxOmega;
    Desc.ClutchStrength   = ClutchStrength;
    Desc.bEnableReverseGear = bEnableReverseGear;
    Desc.ReverseGearSwitchSpeed = std::max(ReverseGearSwitchSpeed, 0.0f);
    Desc.TireFriction     = std::max(TireFriction, 0.01f);
    Desc.DrivableSurfaceMask = BuildDrivableSurfaceMask();
    Desc.bFrontWheelDrive = false;
    Desc.bRearWheelDrive  = false;

    Desc.Wheels.clear();
    Desc.Wheels.reserve(WheelSetups.size());

    TArray<FVector> ResolvedPositions;
    ResolvedPositions.reserve(WheelSetups.size());

    for (const FVehicleWheelSetup& Setup : WheelSetups)
    {
        ResolvedPositions.push_back(ResolveWheelLocalPosition(Setup));
    }

    Desc.VisualToSimulationRotation = ResolveVisualToSimulationRotation(ResolvedPositions);
    FQuat SimulationWorldRotation = VisualWorldTransform.Rotation * Desc.VisualToSimulationRotation.Inverse();
    SimulationWorldRotation.Normalize();
    Desc.WorldTransform = FTransform(VisualWorldTransform.Location, SimulationWorldRotation, FVector(1.0f, 1.0f, 1.0f));

    const FVector VisualChassisShapeOffset = ResolveChassisCollisionOffset(ResolvedPositions);
    Desc.ChassisShapeLocalOffset = Desc.VisualToSimulationRotation.RotateVector(VisualChassisShapeOffset);
    Desc.ChassisCMOffset = Desc.VisualToSimulationRotation.RotateVector(VisualChassisShapeOffset + ChassisCenterOfMassOffset);

    float TireFrictionSum = 0.0f;
    int32 TireFrictionCount = 0;

    for (int32 WheelIndex = 0; WheelIndex < static_cast<int32>(WheelSetups.size()); ++WheelIndex)
    {
        const FVehicleWheelSetup& Setup = WheelSetups[WheelIndex];
        const FVector LocalPosition = Desc.VisualToSimulationRotation.RotateVector(ResolvedPositions[WheelIndex]);

        FVehicleWheelDesc Wheel;
        ApplyWheelDescFromSetup(Setup, LocalPosition, Wheel);
        Desc.Wheels.push_back(Wheel);

        if (Setup.WheelData.TireFriction > 0.0f)
        {
            TireFrictionSum += Setup.WheelData.TireFriction;
            ++TireFrictionCount;
        }

        if (Setup.WheelData.bAffectedByEngine)
        {
            if (WheelIndex < 2)
            {
                Desc.bFrontWheelDrive = true;
            }
            else
            {
                Desc.bRearWheelDrive = true;
            }
        }
    }

    if (TireFrictionCount > 0)
    {
        Desc.TireFriction = TireFrictionSum / static_cast<float>(TireFrictionCount);
    }

    if (Desc.Wheels.size() == VehicleSetupWheelCount && !Desc.bFrontWheelDrive && !Desc.bRearWheelDrive)
    {
        Desc.bRearWheelDrive = true;
    }

    return Desc;
}

void UWheeledVehicleMovementComponent::RegisterPhysicsSnapshotReceiver()
{
    if (PhysicsSnapshotReceiverHandle != 0 || !VehicleHandle.IsValid())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TWeakObjectPtr<UWheeledVehicleMovementComponent> WeakThis(this);
    PhysicsSnapshotReceiverHandle = World->RegisterPhysicsSnapshotReceiver(
        [WeakThis](const FPhysicsWorldSnapshot& Snapshot)
        {
            UWheeledVehicleMovementComponent* Component = WeakThis.Get();
            if (!IsValid(Component))
            {
                return;
            }

            Component->ConsumeVehicleSnapshot(Snapshot);
        });
}

void UWheeledVehicleMovementComponent::UnregisterPhysicsSnapshotReceiver()
{
    if (PhysicsSnapshotReceiverHandle == 0)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->UnregisterPhysicsSnapshotReceiver(PhysicsSnapshotReceiverHandle);
    }
    PhysicsSnapshotReceiverHandle = 0;
}

void UWheeledVehicleMovementComponent::ConsumeVehicleSnapshot(const FPhysicsWorldSnapshot& Snapshot)
{
    bHasLastSnapshot = false;

    if (!VehicleHandle.IsValid())
    {
        return;
    }

    const FVehicleSnapshot* Vehicle = Snapshot.FindVehicleByComponent(GetUUID());
    if (!Vehicle || Vehicle->Vehicle != VehicleHandle)
    {
        return;
    }

    ApplyVehicleSnapshot(*Vehicle);
}

void UWheeledVehicleMovementComponent::ApplyVehicleSnapshot(const FVehicleSnapshot& Snapshot)
{
    LastSnapshot = Snapshot;
    bHasLastSnapshot = true;

    if (USceneComponent* SimulationComponent = ResolveVehicleSimulationComponent())
    {
        SimulationComponent->SetWorldLocation(Snapshot.ChassisWorldTransform.Location);
        SimulationComponent->SetWorldRotation(Snapshot.ChassisWorldTransform.Rotation);
    }
}
