#include "Physics/PhysicsAssetValidation.h"

#include "Animation/Skeleton/SkeletonManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetPreviewUtils.h"

namespace
{
    bool HasMeaningfulBoneName(const FName& BoneName)
    {
        return BoneName.IsValid() && BoneName != FName::None;
    }

    void AppendIssue(
        TArray<FPhysicsAssetValidationIssue>& OutIssues,
        const FString& Message,
        FPhysicsAssetValidationIssue::ESeverity Severity = FPhysicsAssetValidationIssue::ESeverity::Error,
        int32 BodyIndex = -1,
        int32 ConstraintIndex = -1)
    {
        FPhysicsAssetValidationIssue Issue;
        Issue.Message = Message;
        Issue.BodyIndex = BodyIndex;
        Issue.ConstraintIndex = ConstraintIndex;
        Issue.Severity = Severity;
        OutIssues.push_back(Issue);
    }
}

bool FPhysicsAssetValidation::ValidateBodySetup(
    const USkeletalMesh* SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    int32 BodyIndex,
    FPhysicsAssetValidationIssue* OutIssue)
{
    TArray<FPhysicsAssetValidationIssue> Issues;
    ValidateAll(SkeletalMesh, PhysicsAsset, Issues);

    for (const FPhysicsAssetValidationIssue& Issue : Issues)
    {
        if (Issue.BodyIndex == BodyIndex)
        {
            if (OutIssue)
            {
                *OutIssue = Issue;
            }
            return false;
        }
    }

    if (!Issues.empty())
    {
        if (OutIssue)
        {
            *OutIssue = Issues.front();
        }
        return false;
    }

    return true;
}

bool FPhysicsAssetValidation::ValidateConstraintSetup(
    const USkeletalMesh* SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    int32 ConstraintIndex,
    FPhysicsAssetValidationIssue* OutIssue)
{
    TArray<FPhysicsAssetValidationIssue> Issues;
    ValidateAll(SkeletalMesh, PhysicsAsset, Issues);

    for (const FPhysicsAssetValidationIssue& Issue : Issues)
    {
        if (Issue.ConstraintIndex == ConstraintIndex)
        {
            if (OutIssue)
            {
                *OutIssue = Issue;
            }
            return false;
        }
    }

    if (!Issues.empty())
    {
        if (OutIssue)
        {
            *OutIssue = Issues.front();
        }
        return false;
    }

    return true;
}

bool FPhysicsAssetValidation::ValidateAll(
    const USkeletalMesh* SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    TArray<FPhysicsAssetValidationIssue>& OutIssues)
{
    OutIssues.clear();

    if (!SkeletalMesh)
    {
        AppendIssue(OutIssues, "Target skeletal mesh is missing.");
        return false;
    }

    if (!PhysicsAsset)
    {
        AppendIssue(OutIssues, "PhysicsAsset is missing.");
        return false;
    }

    const FSkeletonCompatibilityReport CompatibilityReport =
        FSkeletonManager::CheckCompatibility(SkeletalMesh->GetSkeletonBinding(), PhysicsAsset->GetSkeletonBinding());
    if (CompatibilityReport.Result != ESkeletonCompatibilityResult::ExactSkeleton)
    {
        AppendIssue(
            OutIssues,
            FString("Skeleton binding mismatch: ") + CompatibilityReport.Reason);
    }

    const TArray<FPhysicsAssetBodySetup>& BodySetups = PhysicsAsset->GetBodySetups();
    const TArray<FPhysicsAssetConstraintSetup>& ConstraintSetups = PhysicsAsset->GetConstraintSetups();

    // Placeholder authoring states are reported as warnings so tools can keep editing,
    // while runtime-breaking states remain errors.
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        const FPhysicsAssetBodySetup& BodySetup = BodySetups[BodyIndex];
        const UPhysicsAsset::EEditorSetupState BodySetupState =
            PhysicsAsset->GetBodySetupEditorState(BodyIndex);
        if (BodySetupState == UPhysicsAsset::EEditorSetupState::Placeholder)
        {
            AppendIssue(
                OutIssues,
                "Body setup is still a placeholder and has no target bone assigned.",
                FPhysicsAssetValidationIssue::ESeverity::Warning,
                BodyIndex);
        }

        if (HasMeaningfulBoneName(BodySetup.BoneName))
        {
            int32 BoneIndex = -1;
            if (!FPhysicsAssetPreviewUtils::ResolveBodyBoneIndex(SkeletalMesh, BodySetup, BoneIndex))
            {
                AppendIssue(
                    OutIssues,
                    FString("Body setup references a missing bone: ") + BodySetup.BoneName.ToString(),
                    FPhysicsAssetValidationIssue::ESeverity::Error,
                    BodyIndex);
            }

            const int32 ExistingBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BodySetup.BoneName);
            if (ExistingBodyIndex >= 0 && ExistingBodyIndex != BodyIndex)
            {
                AppendIssue(
                    OutIssues,
                    FString("Duplicate body setup for bone: ") + BodySetup.BoneName.ToString(),
                    FPhysicsAssetValidationIssue::ESeverity::Error,
                    BodyIndex);
            }
        }

        if (BodySetup.Shapes.empty())
        {
            AppendIssue(
                OutIssues,
                "Body setup has no shapes.",
                FPhysicsAssetValidationIssue::ESeverity::Error,
                BodyIndex);
        }
    }

    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(ConstraintSetups.size()); ++ConstraintIndex)
    {
        const FPhysicsAssetConstraintSetup& ConstraintSetup = ConstraintSetups[ConstraintIndex];
        const UPhysicsAsset::EEditorSetupState ConstraintSetupState =
            PhysicsAsset->GetConstraintSetupEditorState(ConstraintIndex);

        if (ConstraintSetupState == UPhysicsAsset::EEditorSetupState::Placeholder)
        {
            AppendIssue(
                OutIssues,
                "Constraint setup is still a placeholder and is missing one or more bones.",
                FPhysicsAssetValidationIssue::ESeverity::Warning,
                -1,
                ConstraintIndex);
        }

        if (!HasMeaningfulBoneName(ConstraintSetup.ChildBoneName))
        {
            AppendIssue(
                OutIssues,
                "Constraint setup has no child bone.",
                FPhysicsAssetValidationIssue::ESeverity::Error,
                -1,
                ConstraintIndex);
        }

        if (!HasMeaningfulBoneName(ConstraintSetup.ParentBoneName))
        {
            AppendIssue(
                OutIssues,
                "Constraint setup has no parent bone.",
                FPhysicsAssetValidationIssue::ESeverity::Error,
                -1,
                ConstraintIndex);
        }

        if (HasMeaningfulBoneName(ConstraintSetup.ParentBoneName) &&
            ConstraintSetup.ParentBoneName == ConstraintSetup.ChildBoneName)
        {
            AppendIssue(
                OutIssues,
                "Constraint setup uses the same parent and child bone.",
                FPhysicsAssetValidationIssue::ESeverity::Error,
                -1,
                ConstraintIndex);
        }

        int32 ParentBoneIndex = -1;
        int32 ChildBoneIndex = -1;
        if (!FPhysicsAssetPreviewUtils::ResolveConstraintBoneIndices(
                SkeletalMesh,
                ConstraintSetup,
                ParentBoneIndex,
                ChildBoneIndex))
        {
            if (HasMeaningfulBoneName(ConstraintSetup.ParentBoneName) && ParentBoneIndex < 0)
            {
                AppendIssue(
                    OutIssues,
                    FString("Constraint parent bone is missing from the skeletal mesh: ") + ConstraintSetup.ParentBoneName.ToString(),
                    FPhysicsAssetValidationIssue::ESeverity::Error,
                    -1,
                    ConstraintIndex);
            }

            if (HasMeaningfulBoneName(ConstraintSetup.ChildBoneName) && ChildBoneIndex < 0)
            {
                AppendIssue(
                    OutIssues,
                    FString("Constraint child bone is missing from the skeletal mesh: ") + ConstraintSetup.ChildBoneName.ToString(),
                    FPhysicsAssetValidationIssue::ESeverity::Error,
                    -1,
                    ConstraintIndex);
            }
        }

        const int32 ExistingConstraintIndex =
            PhysicsAsset->FindConstraintSetupIndex(ConstraintSetup.ParentBoneName, ConstraintSetup.ChildBoneName);
        if (ExistingConstraintIndex >= 0 && ExistingConstraintIndex != ConstraintIndex)
        {
                AppendIssue(
                    OutIssues,
                    FString("Duplicate constraint setup for pair: ") +
                        ConstraintSetup.ParentBoneName.ToString() + " -> " +
                        ConstraintSetup.ChildBoneName.ToString(),
                    FPhysicsAssetValidationIssue::ESeverity::Error,
                    -1,
                    ConstraintIndex);
        }

        if (HasMeaningfulBoneName(ConstraintSetup.ParentBoneName) &&
            !PhysicsAsset->HasBodySetupForBone(ConstraintSetup.ParentBoneName))
        {
            AppendIssue(
                OutIssues,
                FString("Constraint parent body is missing: ") + ConstraintSetup.ParentBoneName.ToString(),
                FPhysicsAssetValidationIssue::ESeverity::Error,
                -1,
                ConstraintIndex);
        }

        if (HasMeaningfulBoneName(ConstraintSetup.ChildBoneName) &&
            !PhysicsAsset->HasBodySetupForBone(ConstraintSetup.ChildBoneName))
        {
            AppendIssue(
                OutIssues,
                FString("Constraint child body is missing: ") + ConstraintSetup.ChildBoneName.ToString(),
                FPhysicsAssetValidationIssue::ESeverity::Error,
                -1,
                ConstraintIndex);
        }
    }

    return OutIssues.empty();
}
