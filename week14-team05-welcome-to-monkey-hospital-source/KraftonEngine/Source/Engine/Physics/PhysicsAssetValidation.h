#pragma once

#include "Core/Types/CoreTypes.h"

class UPhysicsAsset;
class USkeletalMesh;

struct FPhysicsAssetValidationIssue
{
    enum class ESeverity : uint8
    {
        // Authoring can still continue, but the setup is not fully runtime-ready yet.
        Warning,
        // The setup is structurally incompatible with runtime use and should be fixed.
        Error
    };

    FString Message;
    int32 BodyIndex = -1;
    int32 ConstraintIndex = -1;
    ESeverity Severity = ESeverity::Error;
};

class FPhysicsAssetValidation
{
public:
    // Validation collects tool-friendly issues instead of hard-failing so editor flows can
    // surface actionable feedback even while the asset is still being authored.
    static bool ValidateBodySetup(
        const USkeletalMesh* SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        int32 BodyIndex,
        FPhysicsAssetValidationIssue* OutIssue = nullptr);

    static bool ValidateConstraintSetup(
        const USkeletalMesh* SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        int32 ConstraintIndex,
        FPhysicsAssetValidationIssue* OutIssue = nullptr);

    static bool ValidateAll(
        const USkeletalMesh* SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        TArray<FPhysicsAssetValidationIssue>& OutIssues);
};
