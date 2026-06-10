#pragma once

#include "Object/Object.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Physics/PhysicsAssetTypes.h"

#include "Source/Engine/Physics/PhysicsAsset.generated.h"

UCLASS()
class UPhysicsAsset : public UObject
{
public:
    enum class EEditorSetupState : uint8
    {
        // Ready for runtime creation as-is.
        RuntimeReady,
        // Allowed during authoring, but still missing required data such as target bones.
        Placeholder,
        // Structurally inconsistent and should not be treated as runtime-ready.
        Invalid
    };

    GENERATED_BODY()
    UPhysicsAsset()           = default;
    ~UPhysicsAsset() override = default;

    void Serialize(FArchive& Ar) override;
    void SerializePackagePayload(FArchive& Ar, uint32 PackageVersion);
    bool RepairInvalidLegacyConstraintNamesFromSkeleton();
    bool ShouldReflectProperties() const override { return false; }

    const FString& GetAssetPathFileName() const
    {
        return AssetPathFileName;
    }

    void SetAssetPathFileName(const FString& InPath)
    {
        AssetPathFileName = InPath;
    }

    void SetSkeletonBinding(const FSkeletonBinding& InBinding)
    {
        SkeletonBinding = InBinding;
    }

    const FSkeletonBinding& GetSkeletonBinding() const
    {
        return SkeletonBinding;
    }

    const TArray<FPhysicsAssetBodySetup>& GetBodySetups() const
    {
        return BodySetups;
    }

    TArray<FPhysicsAssetBodySetup>& GetMutableBodySetups()
    {
        return BodySetups;
    }

    // Tool code keeps selection state outside the asset and uses these helpers to mutate
    // asset data safely without reaching into raw arrays directly.
    int32 AddBodySetup(const FPhysicsAssetBodySetup& InBodySetup);
    bool RemoveBodySetupByIndex(int32 BodyIndex);
    bool RemoveBodySetupByBoneName(const FName& BoneName);
    bool UpdateBodySetup(int32 BodyIndex, const FPhysicsAssetBodySetup& InBodySetup);
    void ClearBodySetups();

    const TArray<FPhysicsAssetConstraintSetup>& GetConstraintSetups() const
    {
        return ConstraintSetups;
    }

    TArray<FPhysicsAssetConstraintSetup>& GetMutableConstraintSetups()
    {
        return ConstraintSetups;
    }

    int32 AddConstraintSetup(const FPhysicsAssetConstraintSetup& InConstraintSetup);
    bool RemoveConstraintSetupByIndex(int32 ConstraintIndex);
    bool RemoveConstraintSetup(const FName& ParentBoneName, const FName& ChildBoneName);
    bool UpdateConstraintSetup(int32 ConstraintIndex, const FPhysicsAssetConstraintSetup& InConstraintSetup);
    void ClearConstraintSetups();

    // Asset-side lookup helpers keep tools and runtime code from re-implementing the
    // same bone/constraint queries at each call site.
    int32 FindBodySetupIndexByBoneName(const FName& BoneName) const;
    bool HasBodySetupForBone(const FName& BoneName) const;
    const FPhysicsAssetBodySetup* FindBodySetupByBoneName(const FName& BoneName) const;
    FPhysicsAssetBodySetup* FindMutableBodySetupByBoneName(const FName& BoneName);
    EEditorSetupState GetBodySetupEditorState(int32 BodyIndex) const;
    int32 FindConstraintSetupIndex(const FName& ParentBoneName, const FName& ChildBoneName) const;
    bool HasConstraintBetweenBones(const FName& ParentBoneName, const FName& ChildBoneName) const;
    const FPhysicsAssetConstraintSetup* FindConstraintSetup(const FName& ParentBoneName, const FName& ChildBoneName) const;
    FPhysicsAssetConstraintSetup* FindMutableConstraintSetup(const FName& ParentBoneName, const FName& ChildBoneName);
    TArray<const FPhysicsAssetConstraintSetup*> FindConstraintSetupsForBone(const FName& BoneName) const;
    EEditorSetupState GetConstraintSetupEditorState(int32 ConstraintIndex) const;

private:
    void SerializePayload(FArchive& Ar, bool bUseStringConstraintNames);
    void SerializeConstraintSetups(FArchive& Ar, bool bUseStringConstraintNames);

private:
    FString AssetPathFileName = "None";

    FSkeletonBinding SkeletonBinding;
    TArray<FPhysicsAssetBodySetup> BodySetups;
    TArray<FPhysicsAssetConstraintSetup> ConstraintSetups;
};
