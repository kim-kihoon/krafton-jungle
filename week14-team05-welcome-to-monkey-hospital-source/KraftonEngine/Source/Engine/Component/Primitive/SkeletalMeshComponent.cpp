#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/PoseContext.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Asset/AssetRegistry.h"
#include "Cloth/ClothCollisionGatherer.h"
#include "Cloth/SkeletalClothRuntime.h"
#include "Component/PrimitiveComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldSettings.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsRuntime.h"
#include "Profiling/Stats/ClothCollisionStats.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Render/Types/ViewTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace
{
    float Clamp01(float Value)
    {
        return std::clamp(Value, 0.0f, 1.0f);
    }

    FBoundingBox BuildClothSectionWorldBounds(
        const FSkeletalClothData& ClothData,
        const TArray<FVertexPNCTT>& SkinnedVertices,
        const FMatrix& ComponentToWorld)
    {
        FBoundingBox Bounds;
        const TArray<uint32>& SourceIndices = !ClothData.ParticleToRenderVertex.empty()
            ? ClothData.ParticleToRenderVertex
            : ClothData.RenderVertexIndices;
        for (uint32 VertexIndex : SourceIndices)
        {
            if (VertexIndex >= SkinnedVertices.size())
            {
                continue;
            }
            Bounds.Expand(ComponentToWorld.TransformPositionWithW(SkinnedVertices[VertexIndex].Position));
        }
        return Bounds;
    }

    void DrawClothWorldCollisionCandidates(UWorld* World, const FClothCollisionGatherResult& GatherResult)
    {
        if (!World)
        {
            return;
        }

        for (const FClothCollisionCandidate& Candidate : GatherResult.Candidates)
        {
            if ((Candidate.SourceId.Source != EClothCollisionSource::WorldStatic &&
                Candidate.SourceId.Source != EClothCollisionSource::WorldDynamic) ||
                !Candidate.WorldBounds.IsValid())
            {
                continue;
            }

            FColor Color = FColor::Gray();
            if (Candidate.State == EClothCollisionSelectState::Selected)
            {
                Color = Candidate.SourceId.Source == EClothCollisionSource::WorldDynamic
                    ? FColor(0, 255, 255)
                    : FColor::Green();
            }
            else if (Candidate.State == EClothCollisionSelectState::TruncatedByBudget)
            {
                Color = FColor::Yellow();
            }
            else if (Candidate.State == EClothCollisionSelectState::RejectedBySectionBounds ||
                Candidate.State == EClothCollisionSelectState::SkippedFilter)
            {
                Color = FColor::Red();
            }

            DrawDebugBox(
                World,
                Candidate.WorldBounds.GetCenter(),
                Candidate.WorldBounds.GetExtent(),
                Color,
                0.0f);
        }
    }

    float AdvanceTowardTarget(float CurrentValue, float TargetValue, float DeltaTime, float Duration)
    {
        if (std::fabs(TargetValue - CurrentValue) <= 1.0e-6f)
        {
            return TargetValue;
        }

        if (Duration <= 1.0e-6f)
        {
            return TargetValue;
        }

        const float Step = DeltaTime / Duration;
        if (CurrentValue < TargetValue)
        {
            return std::min(CurrentValue + Step, TargetValue);
        }

        return std::max(CurrentValue - Step, TargetValue);
    }

    float SmoothStep01(float Value)
    {
        const float ClampedValue = Clamp01(Value);
        return ClampedValue * ClampedValue * (3.0f - 2.0f * ClampedValue);
    }

    float EaseOutQuadratic01(float Value)
    {
        const float ClampedValue = Clamp01(Value);
        const float OneMinusValue = 1.0f - ClampedValue;
        return 1.0f - OneMinusValue * OneMinusValue;
    }

    FTransform BlendTransforms(const FTransform& AnimationPose, const FTransform& PhysicsPose, float BlendAlpha)
    {
        const float ClampedAlpha = Clamp01(BlendAlpha);
        if (ClampedAlpha <= 0.0f)
        {
            return AnimationPose;
        }

        if (ClampedAlpha >= 1.0f)
        {
            return PhysicsPose;
        }

        FTransform Result;
        Result.Location = FVector::Lerp(AnimationPose.Location, PhysicsPose.Location, ClampedAlpha);
        Result.Rotation = FQuat::Slerp(AnimationPose.Rotation, PhysicsPose.Rotation, ClampedAlpha);
        Result.Scale = FVector::Lerp(AnimationPose.Scale, PhysicsPose.Scale, ClampedAlpha);
        return Result;
    }

    constexpr float PoseSyncDecomposeTolerance = 1.0e-6f;

    // Physics pose reconstruction needs an affine inverse that preserves authored bone
    // scale/orientation well enough for local-pose rebuilding from simulated world space.
    FMatrix GetAffineInverseForPoseSync(const FMatrix& Matrix)
    {
        const double A = Matrix.M[0][0];
        const double B = Matrix.M[0][1];
        const double C = Matrix.M[0][2];
        const double D = Matrix.M[1][0];
        const double E = Matrix.M[1][1];
        const double F = Matrix.M[1][2];
        const double G = Matrix.M[2][0];
        const double H = Matrix.M[2][1];
        const double I = Matrix.M[2][2];

        const double Det = A * (E * I - F * H) - B * (D * I - F * G) + C * (D * H - E * G);
        if (std::fabs(Det) < 1.0e-12)
        {
            return Matrix.GetInverse();
        }

        const double InvDet = 1.0 / Det;

        FMatrix Result = FMatrix::Identity;
        Result.M[0][0] = static_cast<float>((E * I - F * H) * InvDet);
        Result.M[0][1] = static_cast<float>((C * H - B * I) * InvDet);
        Result.M[0][2] = static_cast<float>((B * F - C * E) * InvDet);
        Result.M[1][0] = static_cast<float>((F * G - D * I) * InvDet);
        Result.M[1][1] = static_cast<float>((A * I - C * G) * InvDet);
        Result.M[1][2] = static_cast<float>((C * D - A * F) * InvDet);
        Result.M[2][0] = static_cast<float>((D * H - E * G) * InvDet);
        Result.M[2][1] = static_cast<float>((B * G - A * H) * InvDet);
        Result.M[2][2] = static_cast<float>((A * E - B * D) * InvDet);

        const FVector Translation = Matrix.GetLocation();
        Result.M[3][0] = -(Translation.X * Result.M[0][0] + Translation.Y * Result.M[1][0] + Translation.Z * Result.M[2][0]);
        Result.M[3][1] = -(Translation.X * Result.M[0][1] + Translation.Y * Result.M[1][1] + Translation.Z * Result.M[2][1]);
        Result.M[3][2] = -(Translation.X * Result.M[0][2] + Translation.Y * Result.M[1][2] + Translation.Z * Result.M[2][2]);
        return Result;
    }

    FString ToLowerCopy(const FString& Value)
    {
        FString Result = Value;
        std::transform(
            Result.begin(),
            Result.end(),
            Result.begin(),
            [](unsigned char Character)
            {
                return static_cast<char>(std::tolower(Character));
            });
        return Result;
    }

    bool ContainsAnyToken(const FString& Value, std::initializer_list<const char*> Tokens)
    {
        const FString LowerValue = ToLowerCopy(Value);
        for (const char* Token : Tokens)
        {
            if (!Token)
            {
                continue;
            }

            if (LowerValue.find(ToLowerCopy(Token)) != FString::npos)
            {
                return true;
            }
        }

        return false;
    }

    bool HasPositiveClothPaintRadius(const FSkeletalClothData& ClothData)
    {
        for (float MaxDistance : ClothData.Paint.MaxDistanceValues)
        {
            if (MaxDistance > 0.0f)
            {
                return true;
            }
        }
        return false;
    }

    int32 FindBoneIndexByPriority(const FSkeletalMesh* MeshAsset, std::initializer_list<const char*> CandidateTokens)
    {
        if (!MeshAsset)
        {
            return -1;
        }

        for (const char* CandidateToken : CandidateTokens)
        {
            const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
            {
                if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name) == Token)
                {
                    return BoneIndex;
                }
            }
        }

        for (const char* CandidateToken : CandidateTokens)
        {
            const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
            {
                if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name).find(Token) != FString::npos)
                {
                    return BoneIndex;
                }
            }
        }

        return -1;
    }

    int32 FindBoneIndexByExactName(const FSkeletalMesh* MeshAsset, const FName& BoneName)
    {
        if (!MeshAsset || BoneName == FName::None)
        {
            return -1;
        }

        const FString TargetName = BoneName.ToString();
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            if (MeshAsset->Bones[BoneIndex].Name == TargetName)
            {
                return BoneIndex;
            }
        }

        return -1;
    }

    FName ResolvePreferredRagdollRepresentativeBodyBoneName(const FPhysicsAssetInstance* Instance)
    {
        if (!Instance)
        {
            return FName::None;
        }

        UPhysicsAsset* Asset = Instance->GetAsset();
        if (!Asset)
        {
            return FName::None;
        }

        const TArray<FPhysicsAssetBodySetup>& BodySetups = Asset->GetBodySetups();
        const char* PreferredTokens[] = { "pelvis", "hips", "hip", "root", "spine_01", "spine", "torso" };

        for (const char* PreferredToken : PreferredTokens)
        {
            const FString Token = ToLowerCopy(PreferredToken ? FString(PreferredToken) : FString());
            for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
            {
                const FName CandidateBoneName = BodySetups[BodyIndex].BoneName;
                if (!Instance->HasValidBodyForBone(CandidateBoneName))
                {
                    continue;
                }

                if (ToLowerCopy(CandidateBoneName.ToString()) == Token)
                {
                    return CandidateBoneName;
                }
            }
        }

        for (const char* PreferredToken : PreferredTokens)
        {
            const FString Token = ToLowerCopy(PreferredToken ? FString(PreferredToken) : FString());
            for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
            {
                const FName CandidateBoneName = BodySetups[BodyIndex].BoneName;
                if (!Instance->HasValidBodyForBone(CandidateBoneName))
                {
                    continue;
                }

                if (ToLowerCopy(CandidateBoneName.ToString()).find(Token) != FString::npos)
                {
                    return CandidateBoneName;
                }
            }
        }

        return FName::None;
    }

    bool IsSameOrDescendantBone(const FSkeletalMesh* MeshAsset, int32 BoneIndex, int32 PossibleAncestorIndex)
    {
        if (!MeshAsset || BoneIndex < 0 || PossibleAncestorIndex < 0)
        {
            return false;
        }

        int32 CurrentBoneIndex = BoneIndex;
        while (CurrentBoneIndex >= 0 && CurrentBoneIndex < static_cast<int32>(MeshAsset->Bones.size()))
        {
            if (CurrentBoneIndex == PossibleAncestorIndex)
            {
                return true;
            }

            CurrentBoneIndex = MeshAsset->Bones[CurrentBoneIndex].ParentIndex;
        }

        return false;
    }

    const char* LexToString(ERagdollStandUpType StandUpType)
    {
        switch (StandUpType)
        {
        case ERagdollStandUpType::FaceUp:
            return "FaceUp";
        case ERagdollStandUpType::FaceDown:
            return "FaceDown";
        default:
            return "Unknown";
        }
    }

    const char* LexToString(EPartialRagdollPreset Preset)
    {
        switch (Preset)
        {
        case EPartialRagdollPreset::UpperBody:
            return "UpperBody";
        case EPartialRagdollPreset::LeftArm:
            return "LeftArm";
        case EPartialRagdollPreset::RightArm:
            return "RightArm";
        case EPartialRagdollPreset::HeadNeck:
            return "HeadNeck";
        default:
            return "Unknown";
        }
    }

    const char* LexToString(EPartialRagdollPhase Phase)
    {
        switch (Phase)
        {
        case EPartialRagdollPhase::BlendingIn:
            return "BlendingIn";
        case EPartialRagdollPhase::Active:
            return "Active";
        case EPartialRagdollPhase::BlendingOut:
            return "BlendingOut";
        default:
            return "None";
        }
    }

    const char* LexToString(ERagdollMode Mode)
    {
        switch (Mode)
        {
        case ERagdollMode::FullBody:
            return "FullBody";
        case ERagdollMode::Partial:
            return "Partial";
        default:
            return "None";
        }
    }

    FRagdollReactionRequest MakeRagdollReactionRequest(const FRagdollImpulseRequest& Request)
    {
        FRagdollReactionRequest ReactionRequest;
        ReactionRequest.EventKind = ERagdollReactionEventKind::DirectHit;
        ReactionRequest.HitBoneName = Request.HitBoneName;
        ReactionRequest.HitWorldLocation = Request.WorldLocation;
        ReactionRequest.HitWorldDirection = Request.WorldDirection;
        ReactionRequest.Strength = Request.Strength;
        ReactionRequest.HoldTimeOverride = Request.HoldTimeOverride;
        ReactionRequest.PreferredPreset = Request.PreferredPreset;
        ReactionRequest.bUsePreferredPreset = Request.bUsePreferredPreset;
        ReactionRequest.bAllowEscalationToFullBody = Request.bAllowEscalationToFullBody;
        ReactionRequest.bAllowWhileMoving = Request.bAllowWhileMoving;
        return ReactionRequest;
    }

    bool MakeRagdollShockwaveReactionRequest(
        const USkeletalMeshComponent* Component,
        const FRagdollShockwaveRequest& ShockwaveRequest,
        FRagdollReactionRequest& OutReactionRequest)
    {
        constexpr float MinRadius = 1.0e-3f;
        constexpr float MinEffectiveStrength = 1.0e-3f;
        if (!Component || ShockwaveRequest.Radius <= MinRadius)
        {
            return false;
        }

        FName TargetBoneName = FName::None;
        FVector TargetLocation = Component->GetWorldLocation();
        if (FPhysicsAssetInstance* Instance = Component->GetPhysicsAssetInstance())
        {
            FVector NearestBodyLocation = TargetLocation;
            FName NearestBodyBoneName = FName::None;
            if (Instance->HasLivePhysicsObjects() &&
                Instance->FindNearestBodyToWorldLocation(
                    ShockwaveRequest.Origin,
                    NearestBodyBoneName,
                    NearestBodyLocation))
            {
                TargetBoneName = NearestBodyBoneName;
                TargetLocation = NearestBodyLocation;
            }
        }

        const float Distance = FVector::Distance(ShockwaveRequest.Origin, TargetLocation);
        if (Distance > ShockwaveRequest.Radius)
        {
            return false;
        }

        const float DistanceAlpha = Clamp01(Distance / ShockwaveRequest.Radius);
        const float Falloff = 1.0f - DistanceAlpha;
        const float EffectiveStrength = ShockwaveRequest.Strength * Falloff;
        if (EffectiveStrength < ShockwaveRequest.MinStrength || EffectiveStrength <= MinEffectiveStrength)
        {
            return false;
        }

        FVector Direction = TargetLocation - ShockwaveRequest.Origin;
        if (Direction.Dot(Direction) <= 1.0e-6f)
        {
            Direction = FVector::UpVector;
        }
        else
        {
            Direction = Direction.Normalized();
        }

        OutReactionRequest.EventKind = ERagdollReactionEventKind::RadialShockwave;
        OutReactionRequest.HitBoneName = TargetBoneName;
        OutReactionRequest.HitWorldLocation = TargetLocation;
        OutReactionRequest.HitWorldDirection = Direction;
        OutReactionRequest.Strength = EffectiveStrength;
        OutReactionRequest.HoldTimeOverride = ShockwaveRequest.HoldTimeOverride;
        OutReactionRequest.PreferredPreset = ShockwaveRequest.PreferredPreset;
        OutReactionRequest.bUsePreferredPreset = ShockwaveRequest.bUsePreferredPreset;
        OutReactionRequest.bAllowEscalationToFullBody = ShockwaveRequest.bAllowEscalationToFullBody;
        OutReactionRequest.bAllowWhileMoving = ShockwaveRequest.bAllowWhileMoving;
        return true;
    }

    FTransform DecomposePoseMatrixPreservingScale(const FMatrix& Matrix, const FVector& PreservedScale)
    {
        FTransform Result;
        Result.Location = Matrix.GetLocation();
        Result.Scale = PreservedScale;

        FMatrix RotationMatrix = Matrix;
        RotationMatrix.M[3][0] = 0.0f;
        RotationMatrix.M[3][1] = 0.0f;
        RotationMatrix.M[3][2] = 0.0f;
        RotationMatrix.M[3][3] = 1.0f;

        const FVector ExtractedScale = RotationMatrix.GetScale();
        if (std::fabs(ExtractedScale.X) > PoseSyncDecomposeTolerance)
        {
            RotationMatrix.M[0][0] /= ExtractedScale.X;
            RotationMatrix.M[0][1] /= ExtractedScale.X;
            RotationMatrix.M[0][2] /= ExtractedScale.X;
        }
        if (std::fabs(ExtractedScale.Y) > PoseSyncDecomposeTolerance)
        {
            RotationMatrix.M[1][0] /= ExtractedScale.Y;
            RotationMatrix.M[1][1] /= ExtractedScale.Y;
            RotationMatrix.M[1][2] /= ExtractedScale.Y;
        }
        if (std::fabs(ExtractedScale.Z) > PoseSyncDecomposeTolerance)
        {
            RotationMatrix.M[2][0] /= ExtractedScale.Z;
            RotationMatrix.M[2][1] /= ExtractedScale.Z;
            RotationMatrix.M[2][2] /= ExtractedScale.Z;
        }

        Result.Rotation = RotationMatrix.ToQuat().GetNormalized();
        return Result;
    }

    FVector GetReferenceLocalScale(const FSkeletalMesh* MeshAsset, int32 BoneIndex)
    {
        if (!MeshAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            return FVector::OneVector;
        }

        // Physics bodies author position/rotation only. Scale must come from the
        // skeletal pose data, otherwise decomposing simulated world matrices can bake
        // arbitrary scale into BoneEditLocalMatrices and make some meshes shrink.
        return FTransform(MeshAsset->Bones[BoneIndex].GetReferenceLocalPose()).Scale;
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    DestroyPhysicsAssetInstance();
    ClearAnimInstance();
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // 게임 시작 시점에는 씬 로드와 모든 프로퍼티 복원이 끝나 있으므로,
    // 저장된 AnimInstance/Lua 그래프를 런타임용으로 다시 준비한다.
    InitializeAnimation();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

bool USkeletalMeshComponent::HasEnabledClothSections() const
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset)
    {
        return false;
    }

    for (const FSkeletalClothLODData& LODData : Asset->ClothPayload.LODs)
    {
        for (const FSkeletalClothData& ClothData : LODData.Cloths)
        {
            if (ClothData.bEnabled &&
                (!ClothData.ParticleToRenderVertex.empty() || !ClothData.RenderVertexIndices.empty()))
            {
                return true;
            }
        }
    }

    return false;
}

ESkinningMode USkeletalMeshComponent::GetEffectiveSkinningMode() const
{
    const ESkinningMode GlobalMode = Super::GetEffectiveSkinningMode();
    if (GlobalMode == ESkinningMode::GPU && HasEnabledClothSections())
    {
        return ESkinningMode::CPU;
    }

    return GlobalMode;
}

void USkeletalMeshComponent::TickClothSimulationForEditorPreview(float DeltaTime)
{
    if (GetEffectiveSkinningMode() != ESkinningMode::CPU)
    {
        if (ClothRuntime)
        {
            ClothRuntime->Reset();
        }
        return;
    }

    UpdateCPUSkinning();
    TickClothSimulation(DeltaTime);
}

void USkeletalMeshComponent::ResetClothSimulation()
{
    if (ClothRuntime)
    {
        ClothRuntime->Reset();
    }
}

void USkeletalMeshComponent::SetClothPreviewWindOverride(bool bEnable, const FVector& WorldWindVelocity)
{
    bClothPreviewWindOverride = bEnable;
    ClothPreviewWorldWindVelocity = bEnable ? WorldWindVelocity : FVector::ZeroVector;
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    if (ClothRuntime)
    {
        ClothRuntime->Reset();
    }

    Super::SetSkeletalMesh(InMesh);
    if (InMesh)
    {
        ResolvePhysicsAssetOverride();
    }
    else
    {
        PhysicsAssetOverride.Reset();
    }

    OnPhysicsAssetChanged();
    // Mesh 가 바뀌면 이전 AnimInstance 가 가리키던 본 인덱스/카운트가 무의미해진다.
    // 새 SkeletalMesh 기준으로 AnimInstance 를 재인스턴스화한다.
    InitializeAnimation();
}

void USkeletalMeshComponent::SetPhysicsAssetOverride(UPhysicsAsset* InPhysicsAsset)
{
    if (!InPhysicsAsset)
    {
        ClearPhysicsAssetOverride();
        return;
    }

    FSkeletonCompatibilityReport Report;
    if (!CanUsePhysicsAsset(InPhysicsAsset, &Report))
    {
        UE_LOG("SetPhysicsAssetOverride rejected: skeleton mismatch. Component=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            InPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAssetOverride();
        return;
    }

    PhysicsAssetOverride = InPhysicsAsset;
    PhysicsAssetOverridePath = InPhysicsAsset->GetAssetPathFileName().empty()
        ? FString("None")
        : InPhysicsAsset->GetAssetPathFileName();
    OnPhysicsAssetChanged();
}

void USkeletalMeshComponent::SetPhysicsAssetOverridePath(const FString& InPath)
{
    PhysicsAssetOverridePath.SetPath(InPath.empty() ? FString("None") : InPath);
    PhysicsAssetOverride.Reset();

    if (!PhysicsAssetOverridePath.IsNull())
    {
        ResolvePhysicsAssetOverride();
    }
    else
    {
        OnPhysicsAssetChanged();
    }
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAssetOverride() const
{
    if (PhysicsAssetOverride.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (CanUsePhysicsAsset(PhysicsAssetOverride.Get(), &Report))
        {
            return PhysicsAssetOverride.Get();
        }

        UE_LOG("GetPhysicsAssetOverride cleared incompatible cached PhysicsAsset. Component=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            PhysicsAssetOverride->GetName().c_str(),
            Report.Reason.c_str());
        const_cast<USkeletalMeshComponent*>(this)->ClearPhysicsAssetOverride();
        return nullptr;
    }

    if (PhysicsAssetOverridePath.IsNull())
    {
        return nullptr;
    }

    USkeletalMeshComponent* MutableThis = const_cast<USkeletalMeshComponent*>(this);
    return MutableThis->ResolvePhysicsAssetOverride() ? PhysicsAssetOverride.Get() : nullptr;
}

UPhysicsAsset* USkeletalMeshComponent::GetEffectivePhysicsAsset() const
{
    if (UPhysicsAsset* OverridePhysicsAsset = GetPhysicsAssetOverride())
    {
        return OverridePhysicsAsset;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return nullptr;
    }

    if (UPhysicsAsset* MeshPhysicsAsset = Mesh->GetPhysicsAsset())
    {
        return MeshPhysicsAsset;
    }

    // Components resolve the final fallback order so later ragdoll code can ask only once.
    USkeleton* Skeleton = Mesh->GetSkeleton();
    return Skeleton ? Skeleton->GetDefaultPhysicsAsset() : nullptr;
}

bool USkeletalMeshComponent::ResolvePhysicsAssetOverride()
{
    if (PhysicsAssetOverride.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (CanUsePhysicsAsset(PhysicsAssetOverride.Get(), &Report))
        {
            return true;
        }

        UE_LOG("ResolvePhysicsAssetOverride cleared incompatible cached PhysicsAsset. Component=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            PhysicsAssetOverride->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAssetOverride();
        return false;
    }

    if (PhysicsAssetOverridePath.IsNull())
    {
        PhysicsAssetOverride.Reset();
        return false;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        PhysicsAssetOverride.Reset();
        return false;
    }

    UPhysicsAsset* LoadedPhysicsAsset = FPhysicsAssetManager::Get().LoadPhysicsAsset(PhysicsAssetOverridePath.ToString());
    if (!LoadedPhysicsAsset)
    {
        PhysicsAssetOverride.Reset();
        return false;
    }

    FSkeletonCompatibilityReport Report;
    if (!CanUsePhysicsAsset(LoadedPhysicsAsset, &Report))
    {
        UE_LOG("ResolvePhysicsAssetOverride rejected loaded PhysicsAsset: skeleton mismatch. Component=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            LoadedPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAssetOverride();
        return false;
    }

    PhysicsAssetOverride = LoadedPhysicsAsset;
    return true;
}

void USkeletalMeshComponent::ClearPhysicsAssetOverride()
{
    PhysicsAssetOverride.Reset();
    PhysicsAssetOverridePath.Reset();
    OnPhysicsAssetChanged();
}

void USkeletalMeshComponent::ResetRagdollRuntimeState()
{
    // Resetting runtime state always drops physics-pose ownership first so the component
    // never keeps driving bones from bodies that are about to disappear.
    bUsePhysicsAssetPose = false;
    ResetPhysicsPoseBlendState();
    ResetRagdollRecoveryState();
    ActiveRagdollMode = ERagdollMode::None;
    ClearPartialRagdollState();
    if (PhysicsAssetInstance)
    {
        PhysicsAssetInstance->ResetRuntimeState();
    }
}

void USkeletalMeshComponent::OnPhysicsAssetChanged()
{
    // Asset changes invalidate the current runtime shell entirely; rebuilding is cheaper than
    // trying to salvage stale body/constraint state across different bindings.
    DestroyPhysicsAssetInstance();
}

FPhysicsAssetInstance* USkeletalMeshComponent::GetPhysicsAssetInstance() const
{
    return PhysicsAssetInstance.get();
}

bool USkeletalMeshComponent::IsPartialRagdollSelfSuppressionActive() const
{
    return PhysicsAssetInstance && PhysicsAssetInstance->IsPartialSameActorPrimitiveSuppressionActive();
}

FPhysicsAssetInstance* USkeletalMeshComponent::GetOrCreatePhysicsAssetInstance()
{
    UPhysicsAsset* EffectivePhysicsAsset = GetEffectivePhysicsAsset();
    if (!EffectivePhysicsAsset)
    {
        DestroyPhysicsAssetInstance();
        return nullptr;
    }

    if (PhysicsAssetInstance &&
        PhysicsAssetInstance->GetAsset() == EffectivePhysicsAsset &&
        PhysicsAssetInstance->IsInitialized())
    {
        return PhysicsAssetInstance.get();
    }

    // Asset switches rebuild the whole runtime shell instead of trying to migrate handles
    // across different PhysicsAsset bindings.
    DestroyPhysicsAssetInstance();

    auto NewInstance = std::make_unique<FPhysicsAssetInstance>();
    if (!NewInstance->Initialize(this, EffectivePhysicsAsset))
    {
        return nullptr;
    }

    PhysicsAssetInstance = std::move(NewInstance);
    return PhysicsAssetInstance.get();
}

void USkeletalMeshComponent::DestroyPhysicsAssetInstance()
{
    bUsePhysicsAssetPose = false;
    ResetPhysicsPoseBlendState();
    ResetRagdollRecoveryState();
    ActiveRagdollMode = ERagdollMode::None;
    ClearPartialRagdollState();
    if (!PhysicsAssetInstance)
    {
        return;
    }

    PhysicsAssetInstance->Shutdown();
    PhysicsAssetInstance.reset();
}

bool USkeletalMeshComponent::CaptureRagdollPoseBaseline()
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset || MeshAsset->Bones.empty())
    {
        ClearRagdollPoseBaseline();
        return false;
    }

    GetCurrentBoneGlobalTransforms(RagdollBaselineComponentSpacePose);
    if (RagdollBaselineComponentSpacePose.size() < MeshAsset->Bones.size())
    {
        ClearRagdollPoseBaseline();
        return false;
    }

    RagdollBaselineLocalPose.resize(MeshAsset->Bones.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        RagdollBaselineLocalPose[BoneIndex] = GetBoneLocalTransformByIndex(BoneIndex);
    }

    return true;
}

void USkeletalMeshComponent::ClearRagdollPoseBaseline()
{
    RagdollBaselineComponentSpacePose.clear();
    RagdollBaselineLocalPose.clear();
}

bool USkeletalMeshComponent::BuildPartialRagdollBoneMasks(const FPartialRagdollSelection& Selection)
{
    ClearPartialRagdollState();

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset || MeshAsset->Bones.empty() || !Selection.IsValid())
    {
        return false;
    }

    const int32 RootBoneIndex = FindBoneIndexByExactName(MeshAsset, Selection.RootBoneName);
    if (RootBoneIndex < 0)
    {
        return false;
    }

    PartialRootBoneIndex = RootBoneIndex;
    PartialBoundaryParentBoneIndex = MeshAsset->Bones[RootBoneIndex].ParentIndex;

    PartialSimulatedBoneMask.resize(MeshAsset->Bones.size(), 0);
    PartialPhysicsApplyBoneMask.resize(MeshAsset->Bones.size(), 0);

    int32 SelectedBoneCount = 0;
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        const bool bSelected = Selection.bIncludeDescendants
            ? IsSameOrDescendantBone(MeshAsset, BoneIndex, RootBoneIndex)
            : (BoneIndex == RootBoneIndex);
        if (!bSelected)
        {
            continue;
        }

        PartialSimulatedBoneMask[BoneIndex] = 1;
        PartialPhysicsApplyBoneMask[BoneIndex] = 1;
        ++SelectedBoneCount;
    }

    if (SelectedBoneCount <= 0)
    {
        ClearPartialRagdollState();
        return false;
    }

    ActivePartialRagdollSelection = Selection;
    return true;
}

bool USkeletalMeshComponent::BuildPartialRagdollSelectionFromPreset(EPartialRagdollPreset Preset, FPartialRagdollSelection& OutSelection) const
{
    OutSelection = FPartialRagdollSelection();

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset || MeshAsset->Bones.empty())
    {
        return false;
    }

    int32 RootBoneIndex = -1;
    switch (Preset)
    {
    case EPartialRagdollPreset::UpperBody:
        RootBoneIndex = FindBoneIndexByPriority(MeshAsset, { "spine_01", "spine1", "spine" });
        break;
    case EPartialRagdollPreset::LeftArm:
        RootBoneIndex = FindBoneIndexByPriority(
            MeshAsset,
            {
                "clavicle_l",
                "upperarm_l",
                "shoulder_l",
                "arm_l",
                "leftshoulder",
                "leftarm",
                "l_clavicle",
                "l_upperarm",
                "l_shoulder",
                "armleft",
                "bip001 l clavicle",
                "bip001 l upperarm",
                "l clavicle",
                "l upperarm"
            });
        break;
    case EPartialRagdollPreset::RightArm:
        RootBoneIndex = FindBoneIndexByPriority(
            MeshAsset,
            {
                "clavicle_r",
                "upperarm_r",
                "shoulder_r",
                "arm_r",
                "rightshoulder",
                "rightarm",
                "r_clavicle",
                "r_upperarm",
                "r_shoulder",
                "armright",
                "bip001 r clavicle",
                "bip001 r upperarm",
                "r clavicle",
                "r upperarm"
            });
        break;
    case EPartialRagdollPreset::HeadNeck:
        RootBoneIndex = FindBoneIndexByPriority(MeshAsset, { "neck_01", "neck1", "neck", "head" });
        break;
    default:
        break;
    }

    if (RootBoneIndex < 0 || RootBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
    {
        return false;
    }

    OutSelection.RootBoneName = FName(MeshAsset->Bones[RootBoneIndex].Name);
    OutSelection.bIncludeDescendants = true;
    return OutSelection.IsValid();
}

bool USkeletalMeshComponent::ResolvePartialRagdollPresetFromHitBone(const FName& HitBoneName, EPartialRagdollPreset& OutPreset) const
{
    if (HitBoneName == FName::None)
    {
        return false;
    }

    const FString BoneName = HitBoneName.ToString();
    if (ContainsAnyToken(BoneName, { "head", "neck" }))
    {
        OutPreset = EPartialRagdollPreset::HeadNeck;
        return true;
    }

    if (ContainsAnyToken(BoneName, {
            "clavicle_l",
            "upperarm_l",
            "lowerarm_l",
            "forearm_l",
            "hand_l",
            "_l",
            "left",
            " l clavicle",
            " l upperarm",
            " l forearm",
            " l hand",
            "bip001 l"
        }))
    {
        OutPreset = EPartialRagdollPreset::LeftArm;
        return true;
    }

    if (ContainsAnyToken(BoneName, {
            "clavicle_r",
            "upperarm_r",
            "lowerarm_r",
            "forearm_r",
            "hand_r",
            "_r",
            "right",
            " r clavicle",
            " r upperarm",
            " r forearm",
            " r hand",
            "bip001 r"
        }))
    {
        OutPreset = EPartialRagdollPreset::RightArm;
        return true;
    }

    if (ContainsAnyToken(BoneName, { "spine", "chest", "torso", "pelvis", "rib", "body" }))
    {
        OutPreset = EPartialRagdollPreset::UpperBody;
        return true;
    }

    return false;
}

void USkeletalMeshComponent::ClearPartialRagdollState()
{
    ActivePartialRagdollSelection = FPartialRagdollSelection();
    PartialRootBoneIndex = -1;
    PartialBoundaryParentBoneIndex = -1;
    PartialSimulatedBoneMask.clear();
    PartialPhysicsApplyBoneMask.clear();
    PartialRagdollPhase = EPartialRagdollPhase::None;
    bPendingPartialRagdollBlendOut = false;
    PendingPartialRagdollHoldTimeOverride = -1.0f;
    PartialRagdollHoldRemaining = 0.0f;
    if (ActiveRagdollMode == ERagdollMode::Partial)
    {
        ActiveRagdollMode = ERagdollMode::None;
    }
}

bool USkeletalMeshComponent::IsSamePartialRagdollSelection(const FPartialRagdollSelection& Selection) const
{
    return ActivePartialRagdollSelection.RootBoneName == Selection.RootBoneName &&
           ActivePartialRagdollSelection.bIncludeDescendants == Selection.bIncludeDescendants;
}

void USkeletalMeshComponent::BeginPartialRagdollBlendOut()
{
    if (ActiveRagdollMode != ERagdollMode::Partial)
    {
        return;
    }

    // Partial ragdoll is a localized reaction, so it exits by blending back to animation
    // instead of entering the full-body recovery / stand-up pipeline.
    bPendingPartialRagdollBlendOut = true;
    PartialRagdollPhase = EPartialRagdollPhase::BlendingOut;
    PartialRagdollHoldRemaining = 0.0f;
    TargetPhysicsPoseBlendWeight = 0.0f;
}

bool USkeletalMeshComponent::EnableRagdollPhysics()
{
    if (RecoveryPhase != ERagdollRecoveryPhase::None)
    {
        return false;
    }

    if (ActiveRagdollMode == ERagdollMode::Partial && PhysicsAssetInstance)
    {
        PhysicsAssetInstance->DestroyBodiesAndConstraints();
        SetUsePhysicsAssetPose(false);
        ResetPhysicsPoseBlendState();
        ClearPartialRagdollState();
    }

    // The component stays responsible for high-level ragdoll policy while the instance
    // owns the low-level runtime handles and pose source data.
    FPhysicsAssetInstance* Instance = GetOrCreatePhysicsAssetInstance();
    if (!Instance)
    {
        SetUsePhysicsAssetPose(false);
        ResetPhysicsPoseBlendState();
        ResetRagdollRecoveryState();
        return false;
    }

    CaptureRagdollPoseBaseline();

    FPhysicsAssetSimulationOptions SimulationOptions;
    SimulationOptions.bUseIndependentRagdollCollision = true;
    SimulationOptions.IndependentCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
    SimulationOptions.bIndependentGenerateOverlapEvents = false;

    if (!Instance->CreateBodiesAndConstraints(SimulationOptions))
    {
        SetUsePhysicsAssetPose(false);
        ResetPhysicsPoseBlendState();
        ResetRagdollRecoveryState();
        return false;
    }

    ResetRagdollRecoveryState();
    ActiveRagdollMode = ERagdollMode::FullBody;
    bHasReceivedValidPhysicsPose = false;
    FirstValidPhysicsPoseBlendAlpha = 0.0f;
    TargetPhysicsPoseBlendWeight = 1.0f;
    SetUsePhysicsAssetPose(true);
    return IsRagdollActive();
}

bool USkeletalMeshComponent::EnablePartialRagdoll(const FPartialRagdollSelection& Selection)
{
    if (!Selection.IsValid() || RecoveryPhase != ERagdollRecoveryPhase::None)
    {
        return false;
    }

    const float RequestedHoldDuration =
        PendingPartialRagdollHoldTimeOverride >= 0.0f
            ? PendingPartialRagdollHoldTimeOverride
            : PartialRagdollHoldTime;
    PendingPartialRagdollHoldTimeOverride = -1.0f;

    if (ActiveRagdollMode == ERagdollMode::Partial)
    {
        if (!PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
        {
            return false;
        }

        if (!IsSamePartialRagdollSelection(Selection))
        {
            return false;
        }

        bPendingPartialRagdollBlendOut = false;
        PartialRagdollHoldRemaining = RequestedHoldDuration;
        TargetPhysicsPoseBlendWeight = 1.0f;
        PartialRagdollPhase =
            (PhysicsPoseBlendWeight >= 0.999f)
                ? EPartialRagdollPhase::Active
                : EPartialRagdollPhase::BlendingIn;
        UE_LOG("Partial ragdoll refreshed. Component=%s RootBone=%s Phase=%d",
            GetName().c_str(),
            Selection.RootBoneName.ToString().c_str(),
            static_cast<int32>(PartialRagdollPhase));
        return true;
    }

    if (ActiveRagdollMode == ERagdollMode::FullBody)
    {
        return false;
    }

    FPhysicsAssetInstance* Instance = GetOrCreatePhysicsAssetInstance();
    if (!Instance)
    {
        return false;
    }

    if (!BuildPartialRagdollBoneMasks(Selection))
    {
        return false;
    }

    CaptureRagdollPoseBaseline();

    FPhysicsAssetSimulationOptions SimulationOptions;
    SimulationOptions.bUseIndependentRagdollCollision = true;
    SimulationOptions.IndependentCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
    SimulationOptions.bIndependentGenerateOverlapEvents = false;
    SimulationOptions.bPartialSimulation = true;
    SimulationOptions.PartialRootBoneName = Selection.RootBoneName;
    SimulationOptions.bIncludePartialDescendants = Selection.bIncludeDescendants;
    SimulationOptions.bSuppressSameActorPrimitiveCollisionForPartial = true;
    SimulationOptions.bSuppressSameActorPrimitiveOverlapForPartial = true;

    if (!Instance->CreateBodiesAndConstraints(SimulationOptions))
    {
        SetUsePhysicsAssetPose(false);
        ResetPhysicsPoseBlendState();
        ClearPartialRagdollState();
        return false;
    }

    ActiveRagdollMode = ERagdollMode::Partial;
    bHasReceivedValidPhysicsPose = false;
    FirstValidPhysicsPoseBlendAlpha = 0.0f;
    PartialRagdollPhase = EPartialRagdollPhase::BlendingIn;
    bPendingPartialRagdollBlendOut = false;
    PartialRagdollHoldRemaining = RequestedHoldDuration;
    TargetPhysicsPoseBlendWeight = 1.0f;
    SetUsePhysicsAssetPose(true);
    UE_LOG("Partial ragdoll started. Component=%s RootBone=%s Bodies=%d Constraints=%d SelfSuppression=%s",
        GetName().c_str(),
        Selection.RootBoneName.ToString().c_str(),
        GetLiveRagdollBodyCount(),
        GetLiveRagdollConstraintCount(),
        IsPartialRagdollSelfSuppressionActive() ? "true" : "false");
    return IsPartialRagdollActive();
}

bool USkeletalMeshComponent::EnablePartialRagdoll(const FName& RootBoneName)
{
    FPartialRagdollSelection Selection;
    Selection.RootBoneName = RootBoneName;
    Selection.bIncludeDescendants = true;
    return EnablePartialRagdoll(Selection);
}

FRagdollReactionContext USkeletalMeshComponent::BuildRagdollReactionContext(const FRagdollReactionRequest& Request) const
{
    FRagdollReactionContext Context;
    Context.CurrentMode = ActiveRagdollMode;
    Context.CurrentPartialPhase = PartialRagdollPhase;
    Context.CurrentRecoveryPhase = RecoveryPhase;
    Context.bHasLivePhysicsBodies = PhysicsAssetInstance && PhysicsAssetInstance->HasLivePhysicsObjects();
    Context.bIsRecovering = RecoveryPhase != ERagdollRecoveryPhase::None;
    Context.bPendingPartialBlendOut = bPendingPartialRagdollBlendOut;
    Context.bPartialSelfSuppressionActive = IsPartialRagdollSelfSuppressionActive();
    Context.DefaultPartialHoldTime = PartialRagdollHoldTime;
    Context.ActivePartialRootBoneName = ActivePartialRagdollSelection.RootBoneName;

    if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
    {
        Context.CharacterOwnershipMode = OwnerCharacter->GetPhysicsOwnershipMode();
        Context.CharacterPhysicsCollisionMode = OwnerCharacter->GetCharacterPhysicsCollisionMode();
        Context.CharacterQueryCollisionMode = OwnerCharacter->GetCharacterQueryCollisionMode();
        Context.CharacterGameplayOverlapMode = OwnerCharacter->GetGameplayOverlapOwnershipMode();
        Context.CapsuleRole = OwnerCharacter->GetCapsuleCollisionRole();
        Context.MeshRole = OwnerCharacter->GetMeshCollisionRole();
        Context.PartialBodyRole = OwnerCharacter->GetPartialReactionBodyCollisionRole();
    }

    if (!Request.bAllowWhileMoving)
    {
        if (AActor* OwnerActor = GetOwner())
        {
            if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
            {
                const FVector LinearVelocity = RootPrimitive->GetLinearVelocity();
                Context.bOwnerMoving = LinearVelocity.Dot(LinearVelocity) > 1.0f;
            }
        }
    }

    EPartialRagdollPreset ResolvedPreset = Request.PreferredPreset;
    bool bHasResolvedPreset = Request.bUsePreferredPreset;
    if (!Request.bUsePreferredPreset)
    {
        bHasResolvedPreset = ResolvePartialRagdollPresetFromHitBone(Request.HitBoneName, ResolvedPreset);
    }

    if (!bHasResolvedPreset)
    {
        return Context;
    }

    Context.ResolvedPreset = ResolvedPreset;

    FPartialRagdollSelection Selection;
    if (!BuildPartialRagdollSelectionFromPreset(ResolvedPreset, Selection))
    {
        return Context;
    }

    Context.bHasResolvedPartialSelection = true;
    Context.ResolvedRootBoneName = Selection.RootBoneName;
    Context.bResolvedIncludeDescendants = Selection.bIncludeDescendants;
    Context.bSamePartialSelectionActive =
        ActiveRagdollMode == ERagdollMode::Partial && IsSamePartialRagdollSelection(Selection);
    return Context;
}

bool USkeletalMeshComponent::ApplyImpulseForRagdollReaction(
    const FRagdollReactionRequest& Request,
    const FRagdollReactionDecision& Decision,
    const FName& PartialRootBoneName)
{
    if (!Decision.bApplyImpulse)
    {
        return true;
    }

    FPhysicsAssetInstance* Instance = GetPhysicsAssetInstance();
    if (!Instance)
    {
        return false;
    }

    const FVector Impulse = Decision.ImpulseDirection * Decision.ImpulseMagnitude;
    FName TargetBodyBoneName = FName::None;

    if (Decision.Type == ERagdollReactionType::FullBody ||
        Decision.Type == ERagdollReactionType::EscalateToFull)
    {
        TargetBodyBoneName = Instance->GetPrimarySimulatedBodyBoneName();
    }

    if (TargetBodyBoneName == FName::None)
    {
        TargetBodyBoneName =
            Instance->ResolveBestImpulseTargetBoneName(Request.HitBoneName, PartialRootBoneName);
    }

    if (TargetBodyBoneName == FName::None)
    {
        FName NearestBodyBoneName = FName::None;
        FVector NearestBodyLocation = FVector::ZeroVector;
        if (Instance->FindNearestBodyToWorldLocation(
                Request.HitWorldLocation,
                NearestBodyBoneName,
                NearestBodyLocation))
        {
            TargetBodyBoneName = NearestBodyBoneName;
        }
    }

    if (TargetBodyBoneName == FName::None || !Instance->AddImpulseToBone(TargetBodyBoneName, Impulse))
    {
        return false;
    }

    LastPartialHitReactionTargetBoneName = TargetBodyBoneName;
    LastPartialHitReactionDirection = Decision.ImpulseDirection;
    return true;
}

void USkeletalMeshComponent::RefreshRagdollReactionTuning()
{
    RagdollReactionTuning.BaseImpulseMagnitude = RagdollBaseImpulseMagnitude;
    RagdollReactionTuning.AdditionalImpulseMagnitude = RagdollAdditionalImpulseMagnitude;
    RagdollReactionTuning.OverdriveImpulseMagnitudeScale = RagdollOverdriveImpulseMagnitudeScale;
    RagdollReactionTuning.FullBodyLaunchImpulseMagnitude = FullBodyLaunchImpulseMagnitude;
    RagdollReactionTuning.FullBodyThreshold = DirectHitFullBodyThreshold;
    RagdollReactionTuning.EscalationThreshold = PartialEscalationThreshold;
    RagdollReactionTuning.ShockwaveFullThreshold = ShockwaveFullBodyThreshold;
    RagdollReactionTuning.ShockwaveImpulseThreshold = ShockwaveImpulseThreshold;
    RagdollReactionTuning.RecoveryEscalationThreshold = RecoveryFullBodyThreshold;
    RagdollReactionTuning.RecoveryImpulseThreshold = RecoveryImpulseThreshold;
}

void USkeletalMeshComponent::UpdateLastRagdollReactionDiagnostics(
    const FRagdollReactionRequest& Request,
    const FRagdollReactionDecision& Decision)
{
    LastRagdollReactionEventKind = Request.EventKind;
    LastRagdollReactionType = Decision.Type;
    LastRagdollReactionDecisionReason = Decision.Reason;

    LastPartialHitReactionHitBoneName = Request.HitBoneName;
    LastPartialHitReactionPreset = Decision.ResolvedPreset;
    LastPartialHitReactionRootBoneName = Decision.ResolvedRootBoneName;
    LastPartialHitReactionTargetBoneName = FName::None;
    LastPartialHitReactionStrength = Decision.NormalizedStrength;
    LastPartialHitReactionHoldTime = Decision.HoldTime;
    LastPartialHitReactionImpulseMagnitude = Decision.ImpulseMagnitude;
    LastPartialHitReactionDirection = Decision.ImpulseDirection;
    bLastPartialHitReactionEscalationCandidate = Decision.bEscalationCandidate;
}

bool USkeletalMeshComponent::ExecuteRagdollReactionDecision(
    const FRagdollReactionRequest& Request,
    const FRagdollReactionContext& Context,
    const FRagdollReactionDecision& Decision)
{
    switch (Decision.Type)
    {
    case ERagdollReactionType::Partial:
    case ERagdollReactionType::RefreshPartial:
    {
        if (!Decision.HasResolvedPartialSelection())
        {
            return false;
        }

        FPartialRagdollSelection Selection;
        Selection.RootBoneName = Decision.ResolvedRootBoneName;
        Selection.bIncludeDescendants = Decision.bIncludeDescendants;
        PendingPartialRagdollHoldTimeOverride = Decision.HoldTime;
        const bool bTriggered = EnablePartialRagdoll(Selection);
        if (!bTriggered)
        {
            return false;
        }

        const bool bAppliedImpulse = ApplyImpulseForRagdollReaction(Request, Decision, Decision.ResolvedRootBoneName);
        if (!bAppliedImpulse)
        {
            UE_LOG("Ragdoll reaction executed without impulse. Component=%s Type=%s Reason=%s RootBone=%s",
                GetName().c_str(),
                ::LexToString(Decision.Type),
                ::LexToString(Decision.Reason),
                Decision.ResolvedRootBoneName.ToString().c_str());
        }
        return true;
    }

    case ERagdollReactionType::FullBody:
    case ERagdollReactionType::EscalateToFull:
    {
        const bool bEnabled = EnableRagdollPhysics();
        if (!bEnabled)
        {
            return false;
        }

        const bool bAppliedImpulse = ApplyImpulseForRagdollReaction(Request, Decision, Decision.ResolvedRootBoneName);
        if (!bAppliedImpulse)
        {
            UE_LOG("Ragdoll full-body reaction executed without impulse. Component=%s Type=%s Reason=%s",
                GetName().c_str(),
                ::LexToString(Decision.Type),
                ::LexToString(Decision.Reason));
        }
        return true;
    }

    case ERagdollReactionType::ImpulseOnly:
    case ERagdollReactionType::RecoveryLimited:
        return ApplyImpulseForRagdollReaction(
            Request,
            Decision,
            Decision.ResolvedRootBoneName != FName::None
                ? Decision.ResolvedRootBoneName
                : Context.ActivePartialRootBoneName);

    case ERagdollReactionType::Deferred:
    case ERagdollReactionType::None:
    default:
        return false;
    }
}

bool USkeletalMeshComponent::ApplyRagdollReaction(const FRagdollReactionRequest& Request)
{
    RefreshRagdollReactionTuning();
    const FRagdollReactionContext Context = BuildRagdollReactionContext(Request);
    const FRagdollReactionDecision Decision =
        EvaluateRagdollReactionPolicy(Context, Request, RagdollReactionTuning);
    UpdateLastRagdollReactionDiagnostics(Request, Decision);

    UE_LOG("Ragdoll policy evaluated. Component=%s Event=%s Type=%s Reason=%s Strength=%.2f Hold=%.3f Impulse=%.2f Mode=%s Recovering=%s ResolvedRoot=%s PendingBlendOut=%s OverlapOwnership=%d CapsuleRole=%d PartialRole=%d",
        GetName().c_str(),
        ::LexToString(Request.EventKind),
        ::LexToString(Decision.Type),
        ::LexToString(Decision.Reason),
        Decision.NormalizedStrength,
        Decision.HoldTime,
        Decision.ImpulseMagnitude,
        LexToString(ActiveRagdollMode),
        Context.bIsRecovering ? "true" : "false",
        Decision.ResolvedRootBoneName.ToString().c_str(),
        Context.bPendingPartialBlendOut ? "true" : "false",
        static_cast<int32>(Context.CharacterGameplayOverlapMode),
        static_cast<int32>(Context.CapsuleRole),
        static_cast<int32>(Context.PartialBodyRole));

    const bool bExecuted = ExecuteRagdollReactionDecision(Request, Context, Decision);
    if (!bExecuted)
    {
        UE_LOG("Ragdoll policy execution skipped. Component=%s Event=%s Type=%s Reason=%s",
            GetName().c_str(),
            ::LexToString(Request.EventKind),
            ::LexToString(Decision.Type),
            ::LexToString(Decision.Reason));
    }

    return bExecuted;
}

bool USkeletalMeshComponent::ApplyRagdollImpulse(const FRagdollImpulseRequest& Request)
{
    const FRagdollReactionRequest ReactionRequest = MakeRagdollReactionRequest(Request);
    return ApplyRagdollReaction(ReactionRequest);
}

bool USkeletalMeshComponent::ApplyRagdollShockwave(const FRagdollShockwaveRequest& Request)
{
    FRagdollReactionRequest ReactionRequest;
    if (!MakeRagdollShockwaveReactionRequest(this, Request, ReactionRequest))
    {
        return false;
    }

    return ApplyRagdollReaction(ReactionRequest);
}

bool USkeletalMeshComponent::TriggerPartialRagdoll(const FPartialRagdollRequest& Request)
{
    FPartialRagdollSelection Selection;
    if (!BuildPartialRagdollSelectionFromPreset(Request.Preset, Selection))
    {
        UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s Reason=NoMatchingBone",
            GetName().c_str(),
            LexToString(Request.Preset));
        return false;
    }

    if (ActiveRagdollMode == ERagdollMode::FullBody)
    {
        UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s RootBone=%s Reason=FullBodyRagdollActive",
            GetName().c_str(),
            LexToString(Request.Preset),
            Selection.RootBoneName.ToString().c_str());
        return false;
    }

    if (RecoveryPhase != ERagdollRecoveryPhase::None)
    {
        UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s RootBone=%s Reason=RecoveryActive",
            GetName().c_str(),
            LexToString(Request.Preset),
            Selection.RootBoneName.ToString().c_str());
        return false;
    }

    if (!Request.bAllowWhileMoving)
    {
        if (AActor* OwnerActor = GetOwner())
        {
            if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
            {
                const FVector LinearVelocity = RootPrimitive->GetLinearVelocity();
                if (LinearVelocity.Dot(LinearVelocity) > 1.0f)
                {
                    UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s RootBone=%s Reason=OwnerIsMoving",
                        GetName().c_str(),
                        LexToString(Request.Preset),
                        Selection.RootBoneName.ToString().c_str());
                    return false;
                }
            }
        }
    }

    if (ActiveRagdollMode == ERagdollMode::Partial)
    {
        if (!IsSamePartialRagdollSelection(Selection))
        {
            UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s RootBone=%s Reason=DifferentPartialAlreadyActive ActiveRootBone=%s",
                GetName().c_str(),
                LexToString(Request.Preset),
                Selection.RootBoneName.ToString().c_str(),
                ActivePartialRagdollSelection.RootBoneName.ToString().c_str());
            return false;
        }

        if (!Request.bAllowRefreshIfSamePreset)
        {
            UE_LOG("Partial ragdoll request rejected. Component=%s Preset=%s RootBone=%s Reason=RefreshDisabled",
                GetName().c_str(),
                LexToString(Request.Preset),
                Selection.RootBoneName.ToString().c_str());
            return false;
        }
    }

    const bool bWasPartialActive = ActiveRagdollMode == ERagdollMode::Partial;
    const bool bWasBlendingOut =
        bWasPartialActive &&
        (PartialRagdollPhase == EPartialRagdollPhase::BlendingOut || bPendingPartialRagdollBlendOut);
    PendingPartialRagdollHoldTimeOverride =
        Request.HasHoldTimeOverride()
            ? Request.HoldTimeOverride
            : PartialRagdollHoldTime;

    const bool bTriggered = EnablePartialRagdoll(Selection);
    const char* ResultLabel = "failed";
    if (bTriggered)
    {
        if (bWasPartialActive)
        {
            ResultLabel = bWasBlendingOut ? "reactivated" : "refreshed";
        }
        else
        {
            ResultLabel = "accepted";
        }
    }

    UE_LOG("Partial ragdoll request %s. Component=%s Preset=%s RootBone=%s Hold=%.3f Mode=%s Phase=%s PendingBlendOut=%s",
        ResultLabel,
        GetName().c_str(),
        LexToString(Request.Preset),
        Selection.RootBoneName.ToString().c_str(),
        Request.HasHoldTimeOverride() ? Request.HoldTimeOverride : PartialRagdollHoldTime,
        LexToString(ActiveRagdollMode),
        LexToString(PartialRagdollPhase),
        bPendingPartialRagdollBlendOut ? "true" : "false");
    return bTriggered;
}

bool USkeletalMeshComponent::TriggerPartialRagdollHitReaction(const FPartialRagdollHitReactionRequest& Request)
{
    FRagdollImpulseRequest ImpulseRequest;
    ImpulseRequest.HitBoneName = Request.HitBoneName;
    ImpulseRequest.WorldLocation = Request.HitWorldLocation;
    ImpulseRequest.WorldDirection = Request.HitWorldDirection;
    ImpulseRequest.Strength = Request.Strength;
    ImpulseRequest.HoldTimeOverride = Request.HoldTimeOverride;
    ImpulseRequest.PreferredPreset = Request.PreferredPreset;
    ImpulseRequest.bUsePreferredPreset = Request.bUsePreferredPreset;
    ImpulseRequest.bAllowEscalationToFullBody = Request.bAllowEscalationToFullBody;
    ImpulseRequest.bAllowWhileMoving = true;
    return ApplyRagdollImpulse(ImpulseRequest);
}

void USkeletalMeshComponent::DisableRagdollPhysics()
{
    ActiveRagdollMode = ERagdollMode::None;
    SetUsePhysicsAssetPose(false);
    ResetPhysicsPoseBlendState();
    ResetRagdollRecoveryState();
    ClearPartialRagdollState();
    if (PhysicsAssetInstance)
    {
        PhysicsAssetInstance->DestroyBodiesAndConstraints();
    }
}

void USkeletalMeshComponent::DisablePartialRagdoll()
{
    if (ActiveRagdollMode != ERagdollMode::Partial || !PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
    {
        return;
    }

    BeginPartialRagdollBlendOut();
}

bool USkeletalMeshComponent::BeginRagdollRecovery()
{
    if (RecoveryPhase != ERagdollRecoveryPhase::None || ActiveRagdollMode != ERagdollMode::FullBody)
    {
        return false;
    }

    if (!PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
    {
        DisableRagdollPhysics();
        return false;
    }

    if (!CaptureRagdollRecoverySnapshot())
    {
        UE_LOG("Ragdoll recovery fallback: could not capture ragdoll orientation. Component=%s", GetName().c_str());
        SelectedStandUpType = ERagdollStandUpType::Unknown;
        SelectedStandUpAnimation = nullptr;
    }
    else
    {
        SelectedStandUpType = EvaluateRagdollRecoveryOrientation();
        SelectedStandUpAnimation = SelectStandUpAnimation();
    }

    UE_LOG("Ragdoll recovery started. Component=%s StandUpType=%s StandUpAnimation=%s",
        GetName().c_str(),
        LexToString(SelectedStandUpType),
        SelectedStandUpAnimation ? SelectedStandUpAnimation->GetName().c_str() : "None");

    SetUsePhysicsAssetPose(true);
    RecoveryPhase = ERagdollRecoveryPhase::BlendOutFromPhysics;
    TargetPhysicsPoseBlendWeight = 0.0f;
    return true;
}

bool USkeletalMeshComponent::IsRagdollActive() const
{
    return bUsePhysicsAssetPose && PhysicsAssetInstance && PhysicsAssetInstance->HasLivePhysicsObjects();
}

bool USkeletalMeshComponent::IsPartialRagdollActive() const
{
    return ActiveRagdollMode == ERagdollMode::Partial &&
           bUsePhysicsAssetPose &&
           PhysicsAssetInstance &&
           PhysicsAssetInstance->HasLivePhysicsObjects();
}

bool USkeletalMeshComponent::TryGetRagdollRepresentativeLocation(FVector& OutWorldLocation) const
{
    if (ActiveRagdollMode != ERagdollMode::FullBody ||
        !PhysicsAssetInstance ||
        !PhysicsAssetInstance->HasLivePhysicsObjects() ||
        !bHasReceivedValidPhysicsPose)
    {
        return false;
    }

    const FName PreferredBodyBoneName =
        ResolvePreferredRagdollRepresentativeBodyBoneName(PhysicsAssetInstance.get());
    if (PreferredBodyBoneName != FName::None &&
        PhysicsAssetInstance->HasValidBodyForBone(PreferredBodyBoneName))
    {
        OutWorldLocation =
            PhysicsAssetInstance->GetBodyWorldTransformByBoneName(PreferredBodyBoneName).Location;
        return true;
    }

    const FName PrimaryBodyBoneName = PhysicsAssetInstance->GetPrimarySimulatedBodyBoneName();
    if (PrimaryBodyBoneName != FName::None &&
        PhysicsAssetInstance->HasValidBodyForBone(PrimaryBodyBoneName))
    {
        OutWorldLocation =
            PhysicsAssetInstance->GetBodyWorldTransformByBoneName(PrimaryBodyBoneName).Location;
        return true;
    }

    FName NearestBodyBoneName = FName::None;
    FVector NearestBodyLocation = FVector::ZeroVector;
    if (PhysicsAssetInstance->FindNearestBodyToWorldLocation(
            GetWorldLocation(),
            NearestBodyBoneName,
            NearestBodyLocation))
    {
        OutWorldLocation = NearestBodyLocation;
        return true;
    }

    return false;
}

int32 USkeletalMeshComponent::GetLiveRagdollBodyCount() const
{
    return PhysicsAssetInstance ? PhysicsAssetInstance->GetLiveBodyCount() : 0;
}

int32 USkeletalMeshComponent::GetLiveRagdollConstraintCount() const
{
    return PhysicsAssetInstance ? PhysicsAssetInstance->GetLiveConstraintCount() : 0;
}

UPhysicsAsset* USkeletalMeshComponent::GetActivePhysicsAsset() const
{
    return IsRagdollActive() && PhysicsAssetInstance ? PhysicsAssetInstance->GetAsset() : nullptr;
}

bool USkeletalMeshComponent::CreatePhysicsAssetInstanceBodies()
{
    return EnableRagdollPhysics();
}

void USkeletalMeshComponent::DestroyPhysicsAssetInstanceBodies()
{
    DisableRagdollPhysics();
}

void USkeletalMeshComponent::BeginPhysicsAssetPosePreview(bool bFullBlend)
{
    if (!PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
    {
        SetUsePhysicsAssetPose(false);
        ResetPhysicsPoseBlendState();
        return;
    }

    CaptureRagdollPoseBaseline();
    ResetRagdollRecoveryState();
    ClearPartialRagdollState();
    ActiveRagdollMode = ERagdollMode::FullBody;
    TargetPhysicsPoseBlendWeight = 1.0f;
    PhysicsPoseBlendWeight = bFullBlend ? 1.0f : 0.0f;
    bHasReceivedValidPhysicsPose = bFullBlend;
    FirstValidPhysicsPoseBlendAlpha = bFullBlend ? 1.0f : 0.0f;
    SetUsePhysicsAssetPose(true);
}

void USkeletalMeshComponent::SetUsePhysicsAssetPose(bool bEnable)
{
    // Pose sync is only considered active when live runtime objects exist. This keeps
    // stale flags from pretending ragdoll is still driving the skeletal pose.
    bUsePhysicsAssetPose =
        bEnable &&
        PhysicsAssetInstance &&
        PhysicsAssetInstance->HasLivePhysicsObjects();
}

bool USkeletalMeshComponent::ApplyPhysicsAssetPose()
{
    if (!bUsePhysicsAssetPose || PhysicsPoseBlendWeight <= 0.0f)
    {
        return false;
    }

    FPhysicsAssetInstance* Instance = GetPhysicsAssetInstance();
    if (!Instance || !Instance->HasLivePhysicsObjects())
    {
        SetUsePhysicsAssetPose(false);
        return false;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset || MeshAsset->Bones.empty())
    {
        SetUsePhysicsAssetPose(false);
        return false;
    }

    const bool bRestrictPhysicsToPartialMask =
        ActiveRagdollMode == ERagdollMode::Partial &&
        PartialPhysicsApplyBoneMask.size() >= MeshAsset->Bones.size();

    const bool bUseFrozenBaselinePose =
        ActiveRagdollMode == ERagdollMode::FullBody &&
        !ShouldAdvanceAnimationDuringTick() &&
        RagdollBaselineComponentSpacePose.size() >= MeshAsset->Bones.size() &&
        RagdollBaselineLocalPose.size() >= MeshAsset->Bones.size();

    TArray<FTransform> BoneWorldTransforms;
    if (!Instance->PullPhysicsPose(
            BoneWorldTransforms,
            bUseFrozenBaselinePose ? &RagdollBaselineComponentSpacePose : nullptr,
            bUseFrozenBaselinePose ? &RagdollBaselineLocalPose : nullptr) ||
        BoneWorldTransforms.size() < MeshAsset->Bones.size())
    {
        // Ragdoll bodies are created through the physics command queue, so the first
        // few ticks after entry may not have a published snapshot yet. Treat that as
        // a transient "not ready" state instead of tearing ragdoll ownership down.
        return false;
    }

    if (!bHasReceivedValidPhysicsPose)
    {
        bHasReceivedValidPhysicsPose = true;
        FirstValidPhysicsPoseBlendAlpha = 0.0f;
    }

    TArray<FMatrix> ComponentSpaceGlobalMatrices;
    ComponentSpaceGlobalMatrices.resize(MeshAsset->Bones.size());
    TArray<FMatrix> AnimationComponentSpaceMatrices;
    AnimationComponentSpaceMatrices.resize(MeshAsset->Bones.size());
    TArray<FTransform> CurrentAnimationComponentSpaceTransforms;

    // PullPhysicsPose gives bone world transforms. They are converted back into
    // component-space and then local-space so the normal skinned-mesh pose path can
    // consume the result without learning about rigid bodies directly.
    const FMatrix& ComponentWorldInverse = GetWorldInverseMatrix();
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        ComponentSpaceGlobalMatrices[BoneIndex] =
            BoneWorldTransforms[BoneIndex].ToMatrix() * ComponentWorldInverse;
        AnimationComponentSpaceMatrices[BoneIndex] = ComponentSpaceGlobalMatrices[BoneIndex];
    }

    if (bRestrictPhysicsToPartialMask)
    {
        GetCurrentBoneGlobalTransforms(CurrentAnimationComponentSpaceTransforms);
        if (CurrentAnimationComponentSpaceTransforms.size() >= MeshAsset->Bones.size())
        {
            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
            {
                AnimationComponentSpaceMatrices[BoneIndex] =
                    CurrentAnimationComponentSpaceTransforms[BoneIndex].ToMatrix();
            }
        }
    }

    TArray<FTransform> LocalPose;
    LocalPose.resize(MeshAsset->Bones.size());
    TArray<FTransform> AnimationLocalPose;
    AnimationLocalPose.resize(MeshAsset->Bones.size());
    const float ShapedBlendWeight =
        (ActiveRagdollMode == ERagdollMode::Partial)
            ? ((PartialRagdollPhase == EPartialRagdollPhase::BlendingOut || bPendingPartialRagdollBlendOut)
                ? SmoothStep01(PhysicsPoseBlendWeight)
                : EaseOutQuadratic01(PhysicsPoseBlendWeight))
            : ((RecoveryPhase == ERagdollRecoveryPhase::BlendOutFromPhysics)
                ? SmoothStep01(PhysicsPoseBlendWeight)
                : EaseOutQuadratic01(PhysicsPoseBlendWeight));
    const float EffectiveBlendWeight = Clamp01(
        ShapedBlendWeight * SmoothStep01(FirstValidPhysicsPoseBlendAlpha));

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        AnimationLocalPose[BoneIndex] =
            bUseFrozenBaselinePose
                ? RagdollBaselineLocalPose[BoneIndex]
                : GetBoneLocalTransformByIndex(BoneIndex);

        if (bRestrictPhysicsToPartialMask && PartialPhysicsApplyBoneMask[BoneIndex] == 0)
        {
            LocalPose[BoneIndex] = AnimationLocalPose[BoneIndex];
            continue;
        }

        const int32 ParentIndex = MeshAsset->Bones[BoneIndex].ParentIndex;
        // For partial ragdoll, a physics-driven root such as spine_01 should still remain
        // visually attached to its animation-driven parent such as pelvis. When the parent
        // is outside the partial scope, derive the local pose against the animation parent
        // instead of the reconstructed physics chain to reduce the detached/floating look.
        const bool bParentOutsidePartialScope =
            bRestrictPhysicsToPartialMask &&
            ParentIndex >= 0 &&
            ParentIndex < static_cast<int32>(PartialPhysicsApplyBoneMask.size()) &&
            PartialPhysicsApplyBoneMask[ParentIndex] == 0;
        const FMatrix& ParentMatrixForLocal =
            (bParentOutsidePartialScope &&
             ParentIndex >= 0 &&
             ParentIndex < static_cast<int32>(AnimationComponentSpaceMatrices.size()))
                ? AnimationComponentSpaceMatrices[ParentIndex]
                : (ParentIndex >= 0
                    ? ComponentSpaceGlobalMatrices[ParentIndex]
                    : ComponentSpaceGlobalMatrices[BoneIndex]);
        const FMatrix LocalMatrix = (ParentIndex >= 0)
            ? ComponentSpaceGlobalMatrices[BoneIndex] * GetAffineInverseForPoseSync(ParentMatrixForLocal)
            : ComponentSpaceGlobalMatrices[BoneIndex];
        const FTransform PhysicsLocalPose = DecomposePoseMatrixPreservingScale(
            LocalMatrix,
            GetReferenceLocalScale(MeshAsset, BoneIndex));

        float BoneBlendWeight = EffectiveBlendWeight;
        if (bRestrictPhysicsToPartialMask)
        {
            // Boundary bones are the most visually sensitive region in partial ragdoll.
            // Bias the root and its immediate vicinity slightly toward animation so upper
            // body simulation feels attached instead of sharply disconnected.
            if (BoneIndex == PartialRootBoneIndex)
            {
                BoneBlendWeight *= 0.65f;
            }
            else if (ParentIndex == PartialRootBoneIndex || BoneIndex == PartialBoundaryParentBoneIndex)
            {
                BoneBlendWeight *= 0.85f;
            }
        }

        LocalPose[BoneIndex] = BlendTransforms(
            AnimationLocalPose[BoneIndex],
            PhysicsLocalPose,
            BoneBlendWeight);
    }

    // PullPhysicsPose starts from the current animation result, so bones without bodies
    // naturally stay close to animation. The final blended pose is written through the
    // bone-edit path to preserve authored local scale during physics/local-pose sync.
    EnsureBoneEditPose();
    const int32 BoneCount = std::min(
        static_cast<int32>(MeshAsset->Bones.size()),
        static_cast<int32>(LocalPose.size()));
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        BoneEditLocalMatrices[BoneIndex] = LocalPose[BoneIndex].ToMatrix();
    }

    bUseBoneEditPose = true;
    RefreshSkinningAfterPoseChanged();
    MarkWorldBoundsDirty();
    return true;
}

void USkeletalMeshComponent::ResetPhysicsPoseBlendState()
{
    PhysicsPoseBlendWeight = 0.0f;
    TargetPhysicsPoseBlendWeight = 0.0f;
    ClearRagdollPoseBaseline();
    bHasReceivedValidPhysicsPose = false;
    FirstValidPhysicsPoseBlendAlpha = 0.0f;
}

bool USkeletalMeshComponent::ShouldAdvanceAnimationDuringTick() const
{
    if (!bUsePhysicsAssetPose)
    {
        return true;
    }

    if (ActiveRagdollMode == ERagdollMode::Partial)
    {
        return true;
    }

    // Fully ragdolled bodies should not keep advancing gameplay animation state behind
    // the scenes. We still evaluate animation while blending in/out or during recovery
    // handoff so transition poses remain well-defined.
    if (RecoveryPhase != ERagdollRecoveryPhase::None)
    {
        return true;
    }

    return PhysicsPoseBlendWeight < 0.999f || TargetPhysicsPoseBlendWeight < 0.999f;
}

bool USkeletalMeshComponent::ShouldBlockExternalAnimationControl() const
{
    return !bAllowInternalRagdollAnimationControl &&
           (RecoveryPhase != ERagdollRecoveryPhase::None ||
            (ActiveRagdollMode == ERagdollMode::FullBody && bUsePhysicsAssetPose));
}

void USkeletalMeshComponent::ResetRagdollRecoveryState()
{
    RecoveryPhase = ERagdollRecoveryPhase::None;
    RecoveryCompletionHoldRemaining = 0.0f;
    SelectedStandUpType = ERagdollStandUpType::Unknown;
    RecoveryPelvisWorldTransform = FTransform();
    RecoveryChestWorldTransform = FTransform();
    SelectedStandUpAnimation = nullptr;
    bHasSavedPostRecoveryAnimationState = false;
    SavedPostRecoveryAnimationMode = EAnimationMode::None;
    SavedPostRecoveryAnimationData = FSingleAnimationPlayData();
    SavedPostRecoveryAnimInstanceClass = nullptr;
}

bool USkeletalMeshComponent::CaptureRagdollRecoverySnapshot()
{
    FPhysicsAssetInstance* Instance = GetPhysicsAssetInstance();
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Instance || !MeshAsset || MeshAsset->Bones.empty())
    {
        return false;
    }

    TArray<FTransform> BoneWorldTransforms;
    if (!Instance->PullPhysicsPose(BoneWorldTransforms) ||
        BoneWorldTransforms.size() < MeshAsset->Bones.size())
    {
        return false;
    }

    const int32 PelvisBoneIndex = FindBoneIndexByPriority(MeshAsset, { "pelvis", "hips", "hip", "root" });
    const int32 ChestBoneIndex = FindBoneIndexByPriority(MeshAsset, { "spine_03", "spine_02", "spine_01", "spine", "chest", "upperchest", "torso" });

    RecoveryPelvisWorldTransform =
        (PelvisBoneIndex >= 0 && PelvisBoneIndex < static_cast<int32>(BoneWorldTransforms.size()))
            ? BoneWorldTransforms[PelvisBoneIndex]
            : BoneWorldTransforms[0];

    RecoveryChestWorldTransform =
        (ChestBoneIndex >= 0 && ChestBoneIndex < static_cast<int32>(BoneWorldTransforms.size()))
            ? BoneWorldTransforms[ChestBoneIndex]
            : RecoveryPelvisWorldTransform;

    return true;
}

ERagdollStandUpType USkeletalMeshComponent::EvaluateRagdollRecoveryOrientation() const
{
    // Keep the classification conservative: chest and pelvis should broadly agree before
    // choosing a specific stand-up direction. Ambiguous poses are better handled by
    // fallback than by forcing the wrong get-up animation.
    const FVector ChestUp = RecoveryChestWorldTransform.Rotation.GetUpVector().Normalized();
    const FVector PelvisUp = RecoveryPelvisWorldTransform.Rotation.GetUpVector().Normalized();
    const float ChestDotUp = ChestUp.Dot(FVector::UpVector);
    const float PelvisDotUp = PelvisUp.Dot(FVector::UpVector);
    const float AverageDotUp = 0.5f * (ChestDotUp + PelvisDotUp);

    if ((ChestDotUp >= 0.2f && PelvisDotUp >= 0.0f) || AverageDotUp >= 0.25f)
    {
        return ERagdollStandUpType::FaceUp;
    }

    if ((ChestDotUp <= -0.2f && PelvisDotUp <= 0.0f) || AverageDotUp <= -0.25f)
    {
        return ERagdollStandUpType::FaceDown;
    }

    return ERagdollStandUpType::Unknown;
}

bool USkeletalMeshComponent::CanUseStandUpAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return false;
    }

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        return CanUseAnimation(Sequence);
    }

    return CanUseAnimation(InAsset);
}

UAnimSequenceBase* USkeletalMeshComponent::SelectStandUpAnimation()
{
    if (SelectedStandUpType == ERagdollStandUpType::Unknown)
    {
        return nullptr;
    }

    const FSoftObjectPtr& PreferredPath =
        (SelectedStandUpType == ERagdollStandUpType::FaceUp)
            ? BackStandUpAnimationPath
            : FrontStandUpAnimationPath;

    if (PreferredPath.IsNull())
    {
        UE_LOG("Ragdoll recovery fallback: stand-up animation path is missing. Component=%s StandUpType=%s",
            GetName().c_str(),
            LexToString(SelectedStandUpType));
        return nullptr;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(PreferredPath.ToString());
    if (!LoadedAnimation || !CanUseStandUpAnimation(LoadedAnimation))
    {
        UE_LOG("Ragdoll recovery fallback: stand-up animation could not be used. Component=%s StandUpType=%s Path=%s",
            GetName().c_str(),
            LexToString(SelectedStandUpType),
            PreferredPath.ToString().c_str());
        return nullptr;
    }

    return LoadedAnimation;
}

bool USkeletalMeshComponent::StartStandUpAnimation()
{
    if (!SelectedStandUpAnimation)
    {
        return false;
    }

    // Recovery uses a conservative single-node override so stand-up playback can be injected
    // without taking ownership of the broader gameplay animation state machine.
    if (!bHasSavedPostRecoveryAnimationState)
    {
        SavedPostRecoveryAnimationMode = AnimationMode;
        SavedPostRecoveryAnimationData = AnimationData;
        SavedPostRecoveryAnimInstanceClass = AnimInstanceClass;
        bHasSavedPostRecoveryAnimationState = true;
    }

    bAllowInternalRagdollAnimationControl = true;
    PlayAnimation(SelectedStandUpAnimation, false);
    bAllowInternalRagdollAnimationControl = false;
    RecoveryPhase = ERagdollRecoveryPhase::PlayingStandUp;
    return true;
}

bool USkeletalMeshComponent::IsStandUpAnimationFinished() const
{
    if (RecoveryPhase != ERagdollRecoveryPhase::PlayingStandUp)
    {
        return false;
    }

    const UAnimSingleNodeInstance* SingleNode = IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr;
    if (!SingleNode)
    {
        return true;
    }

    return !SingleNode->IsPlaying();
}

void USkeletalMeshComponent::RestorePostRecoveryAnimationState()
{
    if (!bHasSavedPostRecoveryAnimationState)
    {
        return;
    }

    AnimationData = SavedPostRecoveryAnimationData;
    AnimInstanceClass = SavedPostRecoveryAnimInstanceClass;
    AnimationMode = SavedPostRecoveryAnimationMode;
    InitializeAnimation();
}

void USkeletalMeshComponent::FinishRagdollRecovery()
{
    ActiveRagdollMode = ERagdollMode::None;
    SetUsePhysicsAssetPose(false);
    ResetPhysicsPoseBlendState();
    if (PhysicsAssetInstance)
    {
        PhysicsAssetInstance->DestroyBodiesAndConstraints();
    }

    RestorePostRecoveryAnimationState();
    UE_LOG("Ragdoll recovery completed. Component=%s", GetName().c_str());

    RecoveryPhase = ERagdollRecoveryPhase::Completed;
    RecoveryCompletionHoldRemaining = 0.0f;
    SelectedStandUpType = ERagdollStandUpType::Unknown;
    RecoveryPelvisWorldTransform = FTransform();
    RecoveryChestWorldTransform = FTransform();
    SelectedStandUpAnimation = nullptr;
    bHasSavedPostRecoveryAnimationState = false;
    SavedPostRecoveryAnimationMode = EAnimationMode::None;
    SavedPostRecoveryAnimationData = FSingleAnimationPlayData();
    SavedPostRecoveryAnimInstanceClass = nullptr;
}

void USkeletalMeshComponent::UpdatePhysicsPoseBlend(float DeltaTime)
{
    if (ActiveRagdollMode == ERagdollMode::Partial)
    {
        if (bHasReceivedValidPhysicsPose && FirstValidPhysicsPoseBlendAlpha < 1.0f)
        {
            FirstValidPhysicsPoseBlendAlpha = Clamp01(
                AdvanceTowardTarget(
                    FirstValidPhysicsPoseBlendAlpha,
                    1.0f,
                    DeltaTime,
                    PartialFirstValidPoseBlendInTime));
        }

        if (!PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
        {
            SetUsePhysicsAssetPose(false);
            ResetPhysicsPoseBlendState();
            ClearPartialRagdollState();
            return;
        }

        if (!bPendingPartialRagdollBlendOut &&
            PartialRagdollPhase == EPartialRagdollPhase::Active)
        {
            // Partial ragdoll should self-release after a short reaction window so gameplay
            // callers do not need to orchestrate a stand-up-style recovery every time.
            const float UpdatedHoldRemaining = PartialRagdollHoldRemaining - DeltaTime;
            PartialRagdollHoldRemaining = UpdatedHoldRemaining > 0.0f ? UpdatedHoldRemaining : 0.0f;
            if (PartialRagdollHoldRemaining <= 1.0e-4f)
            {
                UE_LOG("Partial ragdoll auto-release started. Component=%s RootBone=%s",
                    GetName().c_str(),
                    ActivePartialRagdollSelection.RootBoneName.ToString().c_str());
                BeginPartialRagdollBlendOut();
            }
        }

        const float BlendDuration = (TargetPhysicsPoseBlendWeight > PhysicsPoseBlendWeight)
            ? PartialRagdollBlendInTime
            : PartialRagdollBlendOutTime;
        PhysicsPoseBlendWeight = Clamp01(
            AdvanceTowardTarget(PhysicsPoseBlendWeight, TargetPhysicsPoseBlendWeight, DeltaTime, BlendDuration));

        if (!bPendingPartialRagdollBlendOut &&
            TargetPhysicsPoseBlendWeight >= 0.999f &&
            PhysicsPoseBlendWeight >= 0.999f)
        {
            PartialRagdollPhase = EPartialRagdollPhase::Active;
        }

        if (bPendingPartialRagdollBlendOut &&
            PhysicsPoseBlendWeight <= 1.0e-4f &&
            TargetPhysicsPoseBlendWeight <= 1.0e-4f)
        {
            const FName EndedRootBoneName = ActivePartialRagdollSelection.RootBoneName;
            SetUsePhysicsAssetPose(false);
            if (PhysicsAssetInstance)
            {
                PhysicsAssetInstance->DestroyBodiesAndConstraints();
            }
            ResetPhysicsPoseBlendState();
            UE_LOG("Partial ragdoll ended. Component=%s RootBone=%s",
                GetName().c_str(),
                EndedRootBoneName.ToString().c_str());
            ClearPartialRagdollState();
        }

        return;
    }

    if (RecoveryPhase == ERagdollRecoveryPhase::Completed)
    {
        RecoveryPhase = ERagdollRecoveryPhase::None;
        return;
    }

    if (bHasReceivedValidPhysicsPose && FirstValidPhysicsPoseBlendAlpha < 1.0f)
    {
        FirstValidPhysicsPoseBlendAlpha = Clamp01(
            AdvanceTowardTarget(
                FirstValidPhysicsPoseBlendAlpha,
                1.0f,
                DeltaTime,
                RagdollFirstValidPoseBlendInTime));
    }

    if (RecoveryPhase == ERagdollRecoveryPhase::PlayingStandUp)
    {
        if (IsStandUpAnimationFinished())
        {
            RecoveryPhase = ERagdollRecoveryPhase::HoldingFinalPose;
            RecoveryCompletionHoldRemaining = RagdollCompletionHoldTime;
        }
        return;
    }

    if (RecoveryPhase == ERagdollRecoveryPhase::HoldingFinalPose)
    {
        RecoveryCompletionHoldRemaining = std::max(0.0f, RecoveryCompletionHoldRemaining - DeltaTime);
        if (RecoveryCompletionHoldRemaining <= 1.0e-4f)
        {
            FinishRagdollRecovery();
        }
        return;
    }

    if (!PhysicsAssetInstance || !PhysicsAssetInstance->HasLivePhysicsObjects())
    {
        if (RecoveryPhase == ERagdollRecoveryPhase::BlendOutFromPhysics)
        {
            FinishRagdollRecovery();
            return;
        }

        if (!bUsePhysicsAssetPose)
        {
            ResetPhysicsPoseBlendState();
        }
        return;
    }

    const float BlendDuration = (TargetPhysicsPoseBlendWeight > PhysicsPoseBlendWeight)
        ? RagdollBlendInTime
        : RagdollRecoveryBlendOutTime;
    PhysicsPoseBlendWeight = Clamp01(
        AdvanceTowardTarget(PhysicsPoseBlendWeight, TargetPhysicsPoseBlendWeight, DeltaTime, BlendDuration));

    if (RecoveryPhase == ERagdollRecoveryPhase::BlendOutFromPhysics &&
        PhysicsPoseBlendWeight <= 1.0e-4f &&
        TargetPhysicsPoseBlendWeight <= 1.0e-4f)
    {
        SetUsePhysicsAssetPose(false);
        if (PhysicsAssetInstance)
        {
            PhysicsAssetInstance->DestroyBodiesAndConstraints();
        }
        ResetPhysicsPoseBlendState();

        if (!StartStandUpAnimation())
        {
            RecoveryPhase = ERagdollRecoveryPhase::HoldingFinalPose;
            RecoveryCompletionHoldRemaining = RagdollFallbackHoldTime;
        }
    }
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(NewAnimToPlay);
    SetLooping(bLooping);
    SetPlaying(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::StopAnimation()
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    SetAnimation(nullptr);
    SetPlaying(false);

    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        SingleNode->SetCurrentTime(0.0f);
    }
}

// ──────────────────────────────────────────────
// Animation API
// ──────────────────────────────────────────────
void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    if (AnimationMode == InMode) return;
    AnimationMode = InMode;
    InitializeAnimation();
}

bool USkeletalMeshComponent::CanUseAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return true;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return false;
    }

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        FSkeletonCompatibilityReport Report;
        const bool bCompatible = FAssetRegistry::CheckAnimationForMesh(Sequence, Mesh, &Report);
        if (!bCompatible)
        {
            UE_LOG("SetAnimation rejected: skeleton mismatch. Anim=%s Mesh=%s Reason=%s",
                Sequence->GetName().c_str(),
                Mesh->GetName().c_str(),
                Report.Reason.c_str());
        }
        return bCompatible;
    }

    return true;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequenceBase* InAsset)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    if (!CanUseAnimation(InAsset))
    {
        return;
    }

    AnimationData.AnimToPlay = InAsset;

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        AnimationData.AnimToPlayPath = Sequence->GetAssetPathFileName();
    }
    else if (!InAsset)
    {
        AnimationData.AnimToPlayPath = "None";
    }

    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        SingleNode->SetAnimationAsset(InAsset);
    }
}

bool USkeletalMeshComponent::SetAnimationByPath(const FString& AnimationPath)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return false;
    }

    if (AnimationPath.empty() || AnimationPath == "None")
    {
        SetAnimation(nullptr);
        return true;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationPath);
    if (!LoadedAnimation || !CanUseAnimation(LoadedAnimation))
    {
        return false;
    }

    SetAnimation(LoadedAnimation);
    return true;
}

bool USkeletalMeshComponent::PlayAnimationByPath(const FString& AnimationPath, bool bLooping)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return false;
    }

    if (!SetAnimationByPath(AnimationPath))
    {
        return false;
    }

    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetLooping(bLooping);
    SetPlaying(AnimationData.AnimToPlay != nullptr);
    return AnimationData.AnimToPlay != nullptr;
}

void USkeletalMeshComponent::SetPlayRate(float InRate)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    AnimationData.PlayRate = InRate;
    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        SingleNode->SetPlayRate(InRate);
    }
}

void USkeletalMeshComponent::SetLooping(bool bInLoop)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    AnimationData.bLooping = bInLoop;
    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        SingleNode->SetLooping(bInLoop);
    }
}

void USkeletalMeshComponent::SetPlaying(bool bInPlay)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    AnimationData.bPlaying = bInPlay;
    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        SingleNode->SetPlaying(bInPlay);
    }
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    if (AnimInstanceClass.Get() == InClass) return;
    AnimInstanceClass = InClass;   // TSubclassOf 가 IsA 가드로 검증 (잘못된 클래스 → nullptr).
    if (AnimationMode == EAnimationMode::AnimationCustom)
    {
        InitializeAnimation();
    }
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
    if (ShouldBlockExternalAnimationControl())
    {
        return;
    }

    if (InInstance && !IsValid(InInstance)) return;
    if (AnimInstance == InInstance) return;
    ClearAnimInstance();
    AnimInstance = InInstance;
    if (IsValid(AnimInstance))
    {
        AnimInstance->SetOuter(this);
        AnimInstance->SetOwningComponent(this);
        AnimInstance->NativeInitializeAnimation();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetAnimNodeInstance(FName NodeName) const
{
    (void)NodeName;
    return IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr;
}

void USkeletalMeshComponent::LoadAnimationFromPath()
{
    AnimationData.AnimToPlay = nullptr;

    if (AnimationData.AnimToPlayPath.empty() || AnimationData.AnimToPlayPath == "None")
    {
        return;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationData.AnimToPlayPath.ToString());
    if (LoadedAnimation && CanUseAnimation(LoadedAnimation))
    {
        AnimationData.AnimToPlay = LoadedAnimation;
    }
    else
    {
        AnimationData.AnimToPlay = nullptr;
    }
}

bool USkeletalMeshComponent::CanUsePhysicsAsset(UPhysicsAsset* InPhysicsAsset, FSkeletonCompatibilityReport* OutReport) const
{
    if (!InPhysicsAsset)
    {
        if (OutReport)
        {
            OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
            OutReport->Reason = "null physics asset";
        }
        return false;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        if (OutReport)
        {
            OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
            OutReport->Reason = "skeletal mesh is missing";
        }
        return false;
    }

    const FSkeletonCompatibilityReport Report =
        FSkeletonManager::CheckCompatibility(Mesh->GetSkeletonBinding(), InPhysicsAsset->GetSkeletonBinding());

    if (OutReport)
    {
        *OutReport = Report;
    }

    return Report.Result == ESkeletonCompatibilityResult::ExactSkeleton;
}

void USkeletalMeshComponent::InitializeAnimation()
{
    if (!GetSkeletalMesh())
    {
        ClearAnimInstance();
        return;
    }
    if (AnimationMode == EAnimationMode::None)
    {
        ClearAnimInstance();
        return;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode &&
        !AnimationData.AnimToPlay &&
        !AnimationData.AnimToPlayPath.empty() &&
        AnimationData.AnimToPlayPath != "None")
    {
        LoadAnimationFromPath();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && !CanUseAnimation(AnimationData.AnimToPlay))
    {
        AnimationData.AnimToPlay = nullptr;
        AnimationData.AnimToPlayPath = "None";
    }

    switch (AnimationMode)
    {
    case EAnimationMode::AnimationSingleNode:
    {
        ClearAnimInstance();

        UAnimSingleNodeInstance* Single =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance = Single;
        Single->SetOwningComponent(this);
        Single->SetAnimationAsset(AnimationData.AnimToPlay);
        Single->SetPlayRate(AnimationData.PlayRate);
        Single->SetLooping(AnimationData.bLooping);
        Single->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        Single->NativeInitializeAnimation();
        break;
    }
    case EAnimationMode::AnimationCustom:
    {
        UClass* DesiredClass = AnimInstanceClass.Get();
        if (!DesiredClass)
        {
            ClearAnimInstance();
            return;
        }

        if (IsValid(AnimInstance) && AnimInstance->GetClass() == DesiredClass)
        {
            AnimInstance->SetOuter(this);
            AnimInstance->SetOwningComponent(this);
            AnimInstance->NativeInitializeAnimation();
            break;
        }

        ClearAnimInstance();

        UObject* Obj = FObjectFactory::Get().Create(DesiredClass->GetName(), this);
        AnimInstance = Cast<UAnimInstance>(Obj);
		if (!AnimInstance)
        {
            // 클래스가 등록 안됐거나 캐스트 실패 — 무관한 객체가 생성됐을 수 있으니 정리.
            if (Obj) UObjectManager::Get().DestroyObject(Obj);
            return;
        }
        AnimInstance->SetOwningComponent(this);

        AnimInstance->NativeInitializeAnimation();
        break;
    }
    default:
        break;
    }
}

void USkeletalMeshComponent::ClearAnimInstance()
{
    UAnimInstance* InstanceToDestroy = IsValid(AnimInstance) ? AnimInstance : nullptr;
    AnimInstance = nullptr;
    if (InstanceToDestroy)
    {
        InstanceToDestroy->SetOwningComponent(nullptr);
        UObjectManager::Get().DestroyObject(InstanceToDestroy);
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    if (!IsValid(this) || IsPendingKill())
    {
        return;
    }

    const bool bEvaluatedAnimation =
        ShouldAdvanceAnimationDuringTick()
            ? EvaluateAnimInstance(DeltaTime)
            : false;
    UpdatePhysicsPoseBlend(DeltaTime);

    if (bUsePhysicsAssetPose && ApplyPhysicsAssetPose())
    {
        // Physics is applied after animation evaluation so entry/recovery blending has a
        // stable animation pose to blend against each frame.
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        TickClothSimulation(DeltaTime);
        return;
    }

    if (bEvaluatedAnimation)
    {
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        TickClothSimulation(DeltaTime);
        return;
    }

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickClothSimulation(DeltaTime);
}

void USkeletalMeshComponent::TickClothSimulation(float DeltaTime)
{
    UWorld* World = GetWorld();
    const bool bRecordClothCollisionStats = World && World->GetWorldType() != EWorldType::EditorPreview;
    if (bRecordClothCollisionStats)
    {
        CLOTH_COLLISION_STATS_ADD_TICK_ATTEMPT();
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset)
    {
        if (bRecordClothCollisionStats)
        {
            CLOTH_COLLISION_STATS_SKIP_NO_ASSET();
        }
        if (ClothRuntime)
        {
            ClothRuntime->Reset();
        }
        return;
    }

    if (Asset->ClothPayload.LODs.empty())
    {
        if (bRecordClothCollisionStats)
        {
            CLOTH_COLLISION_STATS_SKIP_NO_CLOTH_PAYLOAD();
        }
        if (ClothRuntime)
        {
            ClothRuntime->Reset();
        }
        return;
    }

    if (GetEffectiveSkinningMode() != ESkinningMode::CPU)
    {
        if (bRecordClothCollisionStats)
        {
            CLOTH_COLLISION_STATS_SKIP_NON_CPU_SKINNING();
        }
        if (ClothRuntime)
        {
            ClothRuntime->Reset();
        }
        return;
    }

    if (!ClothRuntime)
    {
        ClothRuntime = std::make_unique<FSkeletalClothRuntime>();
    }

    TArray<FVertexPNCTT>& MutableSkinnedVertices = GetMutableSkinnedVerticesForCloth();
    if (MutableSkinnedVertices.empty())
    {
        if (bRecordClothCollisionStats)
        {
            CLOTH_COLLISION_STATS_SKIP_NO_SKINNED_VERTICES();
        }
        return;
    }

    FClothWorldForceContext ForceContext;
    if (UWorld* World = GetWorld())
    {
        ForceContext.WorldGravity = World->GetWorldSettings().Gravity;
        const FVector& RuntimeWindVelocity = World->GetClothWorldWindVelocity();
        if (!RuntimeWindVelocity.IsNearlyZero())
        {
            ForceContext.WorldWindVelocity = RuntimeWindVelocity;
            ForceContext.bHasWorldWindVelocity = true;
        }
    }
    if (bClothPreviewWindOverride)
    {
        ForceContext.WorldWindVelocity = ClothPreviewWorldWindVelocity;
        ForceContext.bHasWorldWindVelocity = !ClothPreviewWorldWindVelocity.IsNearlyZero();
    }

    FClothCollisionGatherer CollisionGatherer;
    TArray<FClothCollisionSectionResult> CollisionResults;
    UPhysicsAsset* PhysicsAsset = GetEffectivePhysicsAsset();
    IPhysicsRuntime* PhysicsRuntime = nullptr;
    if (World)
    {
        IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
        PhysicsRuntime = PhysicsScene ? PhysicsScene->GetRuntime() : nullptr;
    }

    FBoundingBox ComponentClothWorldBounds;
    TArray<FBoundingBox> SectionWorldBounds;
    TArray<const FSkeletalClothData*> CollisionCloths;
    const FMatrix& ComponentWorldMatrix = GetWorldMatrix();
    for (const FSkeletalClothLODData& LODData : Asset->ClothPayload.LODs)
    {
        for (const FSkeletalClothData& ClothData : LODData.Cloths)
        {
            if (!ClothData.bEnabled)
            {
                continue;
            }

            if (bRecordClothCollisionStats)
            {
                CLOTH_COLLISION_STATS_ADD_ENABLED_SECTION();
            }

            if (!HasPositiveClothPaintRadius(ClothData))
            {
                continue;
            }

            if (!ClothData.Config.bEnablePhysicsAssetCollision &&
                !ClothData.Config.bEnableWorldStaticClothCollision &&
                !ClothData.Config.bEnableWorldDynamicClothCollision)
            {
                continue;
            }

            const FBoundingBox Bounds =
                BuildClothSectionWorldBounds(ClothData, MutableSkinnedVertices, ComponentWorldMatrix);
            if (bRecordClothCollisionStats)
            {
                CLOTH_COLLISION_STATS_ADD_COLLISION_ELIGIBLE_SECTION(
                    ClothData.Config.bEnableWorldStaticClothCollision,
                    ClothData.Config.bEnableWorldDynamicClothCollision,
                    Bounds.IsValid(),
                    PhysicsRuntime != nullptr);
            }
            if (!Bounds.IsValid())
            {
                continue;
            }

            ComponentClothWorldBounds.Expand(Bounds.Min);
            ComponentClothWorldBounds.Expand(Bounds.Max);
            SectionWorldBounds.push_back(Bounds);
            CollisionCloths.push_back(&ClothData);
        }
    }

    if (ComponentClothWorldBounds.IsValid())
    {
        for (int32 Index = 0; Index < static_cast<int32>(CollisionCloths.size()); ++Index)
        {
            const FSkeletalClothData& ClothData = *CollisionCloths[Index];
            FClothCollisionSectionResult SectionResult;
            SectionResult.LODIndex = ClothData.Binding.LODIndex;
            SectionResult.SectionIndex = ClothData.Binding.SectionIndex;
            SectionResult.bWorldStaticCollisionEnabled = ClothData.Config.bEnableWorldStaticClothCollision;
            SectionResult.bWorldDynamicCollisionEnabled = ClothData.Config.bEnableWorldDynamicClothCollision;
            SectionResult.GatherResult = CollisionGatherer.GatherForSection(
                *this,
                *Mesh,
                PhysicsAsset,
                PhysicsRuntime,
                ClothData.Config,
                ComponentClothWorldBounds,
                SectionWorldBounds[Index]);
            DrawClothWorldCollisionCandidates(GetWorld(), SectionResult.GatherResult);
            CollisionResults.push_back(std::move(SectionResult));
        }
    }

    const bool bClothModified = ClothRuntime->Tick(
        *Asset,
        DeltaTime,
        MutableSkinnedVertices,
        GetWorldMatrix(),
        ForceContext,
        CollisionResults.empty() ? nullptr : &CollisionResults);

    if (bRecordClothCollisionStats && !CollisionResults.empty())
    {
        ClothRuntime->CopyCollisionDebugStats(CollisionResults);
        for (const FClothCollisionSectionResult& SectionResult : CollisionResults)
        {
            CLOTH_COLLISION_STATS_ADD_SECTION(
                SectionResult.GatherResult,
                SectionResult.bWorldStaticCollisionEnabled,
                SectionResult.bWorldDynamicCollisionEnabled);
        }
        CLOTH_COLLISION_STATS_ADD_COMPONENT();
    }

    if (bClothModified)
    {
        MarkSkinnedVerticesModifiedByCloth();
    }
}

// ──────────────────────────────────────────────
// Editor / 직렬화 통합
// ──────────────────────────────────────────────
void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
    Super::GetEditableProperties(OutProps);

    // AnimInstance 자체 properties (Speed 등) 도 패널에 같이 노출 — 컴포넌트가 forward.
    // 자식이 자기 카테고리(예: "Animation|Character") 로 그룹화.
    if (IsValid(AnimInstance)) AnimInstance->GetEditableProperties(OutProps);
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    Super::PostEditProperty(PropertyName);
    if (!PropertyName) return;

    if (std::strcmp(PropertyName, "AnimationMode") == 0)
    {
        InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimInstanceClass") == 0)
    {
        // 클래스 슬롯이 바뀌면 Custom 모드에서 인스턴스 재생성 필요. (ours — Phase 6)
        if (AnimationMode == EAnimationMode::AnimationCustom) InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimationData") == 0)
    {
        LoadAnimationFromPath();

        if (IsValid(AnimInstance))
        {
            InitializeAnimation();
        }
    }
    else if (std::strcmp(PropertyName, "AnimToPlayPath") == 0)
    {
        // theirs (main): FAnimationManager 가 path 로 실제 UAnimSequence 로딩 — Phase 4 의 TODO 해소.
        // Mode 가 None 이면 SingleNode 로 자동 전환, AnimInstance 없으면 Initialize, 있으면 SingleNode setter 들 갱신.
        LoadAnimationFromPath();

        if (AnimationMode == EAnimationMode::None)
        {
            AnimationMode = EAnimationMode::AnimationSingleNode;
        }

        if (!IsValid(AnimInstance))
        {
            InitializeAnimation();
        }
        else if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
        {
            if (!CanUseAnimation(AnimationData.AnimToPlay))
            {
                AnimationData.AnimToPlay = nullptr;
                AnimationData.AnimToPlayPath = "None";
            }
            SingleNode->SetAnimationAsset(AnimationData.AnimToPlay);
            SingleNode->SetPlayRate(AnimationData.PlayRate);
            SingleNode->SetLooping(AnimationData.bLooping);
            SingleNode->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        }
    }
    else if (std::strcmp(PropertyName, "PlayRate") == 0)
    {
        SetPlayRate(AnimationData.PlayRate);
    }
    else if (std::strcmp(PropertyName, "bLooping") == 0)
    {
        SetLooping(AnimationData.bLooping);
    }
    else if (std::strcmp(PropertyName, "bPlaying") == 0)
    {
        SetPlaying(AnimationData.bPlaying);
    }
    else if (std::strcmp(PropertyName, "PhysicsAssetOverridePath") == 0 ||
             std::strcmp(PropertyName, "Physics Asset Override") == 0)
    {
        if (!PhysicsAssetOverridePath.IsNull())
        {
            ResolvePhysicsAssetOverride();
        }
        else
        {
            PhysicsAssetOverride.Reset();
        }

        OnPhysicsAssetChanged();
    }

    // AnimInstance 자체 properties 는 자식이 자체 PostEdit 처리. 컴포넌트는 dispatch 만.
    // 컴포넌트가 인식한 이름과 겹치지 않는 한 무해 (자식이 모르는 이름은 no-op).
    if (IsValid(AnimInstance)) AnimInstance->PostEditProperty(PropertyName);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);

    uint8 ModeRaw = static_cast<uint8>(AnimationMode);
    Ar << ModeRaw;
    AnimationMode = static_cast<EAnimationMode>(ModeRaw);

    // AnimToPlay 의 path 만 라운드트립. 실제 포인터 복원은 InitializeAnimation() → LoadAnimationFromPath() 가 처리.
    FString AnimToPlayPath = Ar.IsSaving() ? AnimationData.AnimToPlayPath.ToString() : FString();
    Ar << AnimToPlayPath;
    if (Ar.IsLoading())
    {
        AnimationData.AnimToPlayPath.SetPath(AnimToPlayPath);
        PhysicsAssetOverride.Reset();
        ResetRagdollRuntimeState();
    }
    Ar << AnimationData.PlayRate;
    Ar << AnimationData.bLooping;
    Ar << AnimationData.bPlaying;

}

bool USkeletalMeshComponent::EvaluateAnimInstance(float DeltaTime)
{
    if (!IsValid(this) || IsPendingKill()) return false;
    if (!IsValid(AnimInstance))
    {
        AnimInstance = nullptr;
        return false;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return false; 
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Bones.empty()) return false;

    if (UAnimSingleNodeInstance* SingleNode = (IsValid(AnimInstance) ? Cast<UAnimSingleNodeInstance>(AnimInstance) : nullptr))
    {
        if (!CanUseAnimation(SingleNode->GetAnimationAsset()))
        {
            SingleNode->SetAnimationAsset(nullptr);
            return false;
        }
    }

    AnimInstance->UpdateAnimation(DeltaTime);
    if (!IsValid(AnimInstance))
    {
        AnimInstance = nullptr;
        return false;
    }

    // Root motion 적용은 UCharacterMovementComponent 가 책임.
    // CMC::TickComponent (TG_DuringPhysics) 가 매 frame 이 AnimInstance->ConsumeRootMotion 으로
    // 누적값을 가져가 capsule 이동 / 회전에 반영한다 (sweep / floor stick 통과).
    // Mesh 는 actor transform 을 직접 만지지 않는다 — UE 본가 패턴.
    //
    // 주의: CMC 가 없는 actor 에 root motion 켠 anim 을 붙이면 누적값이 anywhere 도
    // 소비되지 않아 in-place 로 보인다. ACharacter 외 케이스에서 root motion 이 필요해지면
    // 별도 소비 경로가 추가되어야 한다.

    FPoseContext Out;
    Out.SkeletalMesh = Mesh;
    Out.Pose.resize(Asset->Bones.size());
    Out.ResetToRefPose();
    if (!IsValid(AnimInstance))
    {
        AnimInstance = nullptr;
        return false;
    }
    AnimInstance->EvaluatePose(Out);

    SetAnimationPose(Out.Pose, Out.MorphWeights);
    return true;
}


void USkeletalMeshComponent::BeginDestroy()
{
    DestroyPhysicsAssetInstance();
    ClearAnimInstance();
    Super::BeginDestroy();
}
