#pragma once
#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Math/MathUtils.h"
#include "BodySetup/BodySetup.h"
#include "Source/Engine/Physics/PhysicsAsset.generated.h"

class UPhysicsConstraintTemplate;
class USkeletalBodySetup;

USTRUCT()
struct FRigidBodyIndexPair
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save)
	int32 BodyIndexA = -1;

	UPROPERTY(Edit, Save)
	int32 BodyIndexB = -1;

	FRigidBodyIndexPair() = default;

	FRigidBodyIndexPair(int32 InA, int32 InB)
	{
		BodyIndexA = FMath::Min(InA, InB);
		BodyIndexB = FMath::Max(InA, InB);
	}

	bool operator==(const FRigidBodyIndexPair& Other) const
	{
		return BodyIndexA == Other.BodyIndexA && BodyIndexB == Other.BodyIndexB;
	}
};

namespace std
{
	template<>
	struct hash<FRigidBodyIndexPair>
	{
		size_t operator()(const FRigidBodyIndexPair& Pair) const noexcept
		{
			const size_t H1 = std::hash<int32>()(Pair.BodyIndexA);
			const size_t H2 = std::hash<int32>()(Pair.BodyIndexB);

			return H1 ^ (H2 + 0x9e3779b9 + (H1 << 6) + (H1 >> 2));
		}
	};
}

UCLASS()
class UPhysicsAsset : public UObject
{
public:
	GENERATED_BODY()

public:
	void Serialize(FArchive& Ar) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	const FString& GetSourcePath() const { return SourcePath; }
	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetPreviewSkeletalMeshPath() const { return PreviewSkeletalMeshPath; }
	void SetPreviewSkeletalMeshPath(const FString& InPath) { PreviewSkeletalMeshPath = InPath.empty() ? FString("None") : InPath; }

	// BodySetup access
	int32 GetBodySetupCount() const { return static_cast<int32>(SkeletalBodySetups.size()); }

	USkeletalBodySetup* GetBodySetup(int32 BodyIndex);
	const USkeletalBodySetup* GetBodySetup(int32 BodyIndex) const;

	int32 FindBodyIndex(FName BodyName) const;

	USkeletalBodySetup* FindBodySetup(FName BodyName);
	const USkeletalBodySetup* FindBodySetup(FName BodyName) const;

	const TArray<USkeletalBodySetup*>& GetBodySetups() const
	{
		return SkeletalBodySetups;
	}

	// Editor mutation
	// Physics Asset Editor용
	USkeletalBodySetup* AddBodySetup(FName BoneName);
	bool RemoveBodySetup(FName BoneName);
	bool RemoveBodySetupAt(int32 BodyIndex);

	// Constraint access
	int32 GetConstraintSetupCount() const { return static_cast<int32>(ConstraintSetups.size()); }

	UPhysicsConstraintTemplate* GetConstraintSetup(int32 ConstraintIndex);
	const UPhysicsConstraintTemplate* GetConstraintSetup(int32 ConstraintIndex) const;

	int32 FindConstraintIndex(FName ConstraintName) const;

	UPhysicsConstraintTemplate* FindConstraintSetup(FName ConstraintName);
	const UPhysicsConstraintTemplate* FindConstraintSetup(FName ConstraintName) const;

	const TArray<UPhysicsConstraintTemplate*>& GetConstraintSetups() const
	{
		return ConstraintSetups;
	}

	void BodyFindConstraints(int32 BodyIndex, TArray<int32>& OutConstraintIndices) const;

	// Editor mutation
	// Physics Asset Editor용
	UPhysicsConstraintTemplate* AddConstraintSetup(
		FName ConstraintName,
		FName ParentBoneName,
		FName ChildBoneName);

	bool RemoveConstraintSetup(FName ConstraintName);
	bool RemoveConstraintSetupAt(int32 ConstraintIndex);

	// Collision pair setting
	void DisableCollision(int32 BodyIndexA, int32 BodyIndexB);
	void EnableCollision(int32 BodyIndexA, int32 BodyIndexB);
	bool IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const;

	// Cache
	void UpdateBodySetupIndexMap();

	// Bounds / Debug
	const TArray<int32>& GetBoundsBodies() const { return BoundsBodies; }
	void UpdateBoundsBodiesArray();

	// Editor refresh hook
	void RefreshPhysicsAssetChange();

private:
	FString SourcePath = "None";

	UPROPERTY(Edit, Save, Category = "Physics Asset", DisplayName = "Preview Skeletal Mesh", AssetType = "SkeletalMesh")
	FString PreviewSkeletalMeshPath = "None";

	UPROPERTY(Edit, Save, Category = "Physics Asset", DisplayName = "Skeletal Body Setups")
	TArray<USkeletalBodySetup*> SkeletalBodySetups;

	UPROPERTY(Edit, Save, Category = "Physics Asset", DisplayName = "Constraint Setups")
	TArray<UPhysicsConstraintTemplate*> ConstraintSetups;

	UPROPERTY(Save)
	TArray<int32> BoundsBodies;

	/** This caches the BodySetup Index by BodyName to speed up FindBodyIndex */
	TMap<FName, int32> BodySetupIndexMap;
	 
	/**
	 *	Table indicating which pairs of bodies have collision disabled between them. Used internally.
	 *	Note, this is accessed from within physics engine, so is not safe to change while physics is running
	 */
	TMap<FRigidBodyIndexPair, bool> CollisionDisableTable;
};

UCLASS()
class USkeletalBodySetup : public UBodySetup
{
public:
	GENERATED_BODY()

	/** If true we ignore scale changes from animation. This is useful for subtle scale animations like breathing where the physics collision should remain unchanged*/
	UPROPERTY(EditAnywhere, Category = BodySetup)
	bool bSkipScaleFromAnimation = false;
};
