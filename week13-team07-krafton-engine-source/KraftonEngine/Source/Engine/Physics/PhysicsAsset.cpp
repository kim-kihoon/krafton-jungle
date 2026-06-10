#include "PhysicsAsset.h"
#include "PhysicsConstraintTemplate.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <utility>

static bool IsValidIndex(int32 Index, int32 Count)
{
	return Index >= 0 && Index < Count;
}

namespace
{
	static constexpr uint32 PhysicsAssetSerializeVersion = 2;

	void SerializeConstraintFrame(FArchive& Ar, FConstraintFrame& Frame)
	{
		Ar << Frame.Position;
		Ar << Frame.Rotation;
	}

	void SerializeConstraintInstanceEditorData(FArchive& Ar, FConstraintInstance& Instance)
	{
		Ar << Instance.ConstraintName;
		Ar << Instance.ParentBoneName;
		Ar << Instance.ChildBoneName;
		SerializeConstraintFrame(Ar, Instance.ParentFrame);
		SerializeConstraintFrame(Ar, Instance.ChildFrame);
		Ar << Instance.bDisableCollision;

		uint8 LinearX = static_cast<uint8>(Instance.LinearXMotion);
		uint8 LinearY = static_cast<uint8>(Instance.LinearYMotion);
		uint8 LinearZ = static_cast<uint8>(Instance.LinearZMotion);
		Ar << LinearX;
		Ar << LinearY;
		Ar << LinearZ;
		if (Ar.IsLoading())
		{
			Instance.LinearXMotion = static_cast<EConstraintMotion>(LinearX);
			Instance.LinearYMotion = static_cast<EConstraintMotion>(LinearY);
			Instance.LinearZMotion = static_cast<EConstraintMotion>(LinearZ);
		}
		Ar << Instance.LinearLimitSize;

		uint8 Swing1 = static_cast<uint8>(Instance.Swing1Motion);
		uint8 Swing2 = static_cast<uint8>(Instance.Swing2Motion);
		uint8 Twist = static_cast<uint8>(Instance.TwistMotion);
		Ar << Swing1;
		Ar << Swing2;
		Ar << Twist;
		if (Ar.IsLoading())
		{
			Instance.Swing1Motion = static_cast<EConstraintMotion>(Swing1);
			Instance.Swing2Motion = static_cast<EConstraintMotion>(Swing2);
			Instance.TwistMotion = static_cast<EConstraintMotion>(Twist);
		}
		Ar << Instance.Swing1LimitDegrees;
		Ar << Instance.Swing2LimitDegrees;
		Ar << Instance.TwistLimitMinDegrees;
		Ar << Instance.TwistLimitMaxDegrees;
	}
}

void UPhysicsAsset::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);

	for (USkeletalBodySetup* BodySetup : SkeletalBodySetups)
	{
		Collector.AddReferencedObject(BodySetup);
	}

	for (UPhysicsConstraintTemplate* ConstraintSetup : ConstraintSetups)
	{
		Collector.AddReferencedObject(ConstraintSetup);
	}
}

void UPhysicsAsset::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

	uint32 Version = PhysicsAssetSerializeVersion;
	Ar << Version;

	if (Version >= 2)
	{
		Ar << PreviewSkeletalMeshPath;
		if (Ar.IsLoading() && PreviewSkeletalMeshPath.empty())
		{
			PreviewSkeletalMeshPath = "None";
		}
	}
	else if (Ar.IsLoading())
	{
		PreviewSkeletalMeshPath = "None";
	}

	if (Ar.IsLoading())
	{
		for (USkeletalBodySetup* BodySetup : SkeletalBodySetups)
		{
			if (BodySetup)
			{
				UObjectManager::Get().DestroyObject(BodySetup);
			}
		}
		SkeletalBodySetups.clear();

		for (UPhysicsConstraintTemplate* ConstraintSetup : ConstraintSetups)
		{
			if (ConstraintSetup)
			{
				UObjectManager::Get().DestroyObject(ConstraintSetup);
			}
		}
		ConstraintSetups.clear();
		CollisionDisableTable.clear();
		BoundsBodies.clear();
		BodySetupIndexMap.clear();
	}

	uint32 BodyCount = static_cast<uint32>(SkeletalBodySetups.size());
	Ar << BodyCount;
	if (Ar.IsLoading())
	{
		SkeletalBodySetups.resize(BodyCount, nullptr);
	}
	for (uint32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		if (Ar.IsLoading())
		{
			SkeletalBodySetups[BodyIndex] = nullptr;
		}

		USkeletalBodySetup* BodySetup = SkeletalBodySetups[BodyIndex];
		bool bHasBodySetup = BodySetup != nullptr;
		Ar << bHasBodySetup;
		if (!bHasBodySetup)
		{
			if (Ar.IsLoading())
			{
				SkeletalBodySetups[BodyIndex] = nullptr;
			}
			continue;
		}

		if (Ar.IsLoading())
		{
			BodySetup = UObjectManager::Get().CreateObject<USkeletalBodySetup>(this);
			SkeletalBodySetups[BodyIndex] = BodySetup;
		}

		BodySetup->SerializeCollision(Ar);
		Ar << BodySetup->bSkipScaleFromAnimation;
	}

	uint32 ConstraintCount = static_cast<uint32>(ConstraintSetups.size());
	Ar << ConstraintCount;
	if (Ar.IsLoading())
	{
		ConstraintSetups.resize(ConstraintCount, nullptr);
	}
	for (uint32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
	{
		UPhysicsConstraintTemplate* Template = ConstraintSetups[ConstraintIndex];
		FConstraintInstance Instance;
		if (Ar.IsSaving() && Template)
		{
			Instance = Template->GetDefaultInstance();
			if (Instance.ConstraintName.IsNone())
			{
				Instance.ConstraintName = Template->GetConstraintName();
			}
			if (Instance.ParentBoneName.IsNone())
			{
				Instance.ParentBoneName = Template->GetParentBoneName();
			}
			if (Instance.ChildBoneName.IsNone())
			{
				Instance.ChildBoneName = Template->GetChildBoneName();
			}
		}

		bool bHasTemplate = Ar.IsSaving() ? Template != nullptr : true;
		Ar << bHasTemplate;
		if (!bHasTemplate)
		{
			continue;
		}

		SerializeConstraintInstanceEditorData(Ar, Instance);

		if (Ar.IsLoading())
		{
			Template = UObjectManager::Get().CreateObject<UPhysicsConstraintTemplate>(this);
			Template->SetDefaultInstance(Instance);
			ConstraintSetups[ConstraintIndex] = Template;
		}
	}

	if (Ar.IsLoading())
	{
		UpdateBodySetupIndexMap();
	}

	TArray<std::pair<FName, FName>> DisabledCollisionPairs;
	if (Ar.IsSaving())
	{
		for (const auto& Pair : CollisionDisableTable)
		{
			if (!Pair.second)
			{
				continue;
			}

			const USkeletalBodySetup* BodyA = GetBodySetup(Pair.first.BodyIndexA);
			const USkeletalBodySetup* BodyB = GetBodySetup(Pair.first.BodyIndexB);
			if (!BodyA || !BodyB)
			{
				continue;
			}

			DisabledCollisionPairs.push_back({ BodyA->GetBoneName(), BodyB->GetBoneName() });
		}
	}

	uint32 DisabledPairCount = static_cast<uint32>(DisabledCollisionPairs.size());
	Ar << DisabledPairCount;
	for (uint32 PairIndex = 0; PairIndex < DisabledPairCount; ++PairIndex)
	{
		FName BoneA;
		FName BoneB;
		if (Ar.IsSaving())
		{
			BoneA = DisabledCollisionPairs[PairIndex].first;
			BoneB = DisabledCollisionPairs[PairIndex].second;
		}

		Ar << BoneA;
		Ar << BoneB;

		if (Ar.IsLoading())
		{
			const int32 BodyIndexA = FindBodyIndex(BoneA);
			const int32 BodyIndexB = FindBodyIndex(BoneB);
			if (IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) &&
				IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) &&
				BodyIndexA != BodyIndexB)
			{
				CollisionDisableTable[FRigidBodyIndexPair(BodyIndexA, BodyIndexB)] = true;
			}
		}
	}

	if (Ar.IsLoading())
	{
		UpdateBodySetupIndexMap();
		UpdateBoundsBodiesArray();
	}
}

USkeletalBodySetup* UPhysicsAsset::GetBodySetup(int32 BodyIndex)
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return nullptr;
	}

	return SkeletalBodySetups[BodyIndex];
}

const USkeletalBodySetup* UPhysicsAsset::GetBodySetup(int32 BodyIndex) const
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return nullptr;
	}

	return SkeletalBodySetups[BodyIndex];
}

int32 UPhysicsAsset::FindBodyIndex(FName BodyName) const
{
	if (BodyName.IsNone())
	{
		return -1;
	}

	auto It = BodySetupIndexMap.find(BodyName);
	if (It == BodySetupIndexMap.end())
	{
		return -1;
	}

	return It->second;
}

USkeletalBodySetup* UPhysicsAsset::FindBodySetup(FName BodyName)
{
	const int32 BodyIndex = FindBodyIndex(BodyName);
	return GetBodySetup(BodyIndex);
}

const USkeletalBodySetup* UPhysicsAsset::FindBodySetup(FName BodyName) const
{
	const int32 BodyIndex = FindBodyIndex(BodyName);
	return GetBodySetup(BodyIndex);
}

USkeletalBodySetup* UPhysicsAsset::AddBodySetup(FName BoneName)
{
	if (BoneName.IsNone())
	{
		return nullptr;
	}

	if (USkeletalBodySetup* Existing = FindBodySetup(BoneName))
	{
		return Existing;
	}

	USkeletalBodySetup* NewBodySetup = UObjectManager::Get().CreateObject<USkeletalBodySetup>(this);
	NewBodySetup->SetBoneName(BoneName);

	SkeletalBodySetups.push_back(NewBodySetup);

	UpdateBodySetupIndexMap();
	UpdateBoundsBodiesArray();
	RefreshPhysicsAssetChange();

	return NewBodySetup;
}

bool UPhysicsAsset::RemoveBodySetup(FName BoneName)
{
	const int32 BodyIndex = FindBodyIndex(BoneName);
	return RemoveBodySetupAt(BodyIndex);
}

bool UPhysicsAsset::RemoveBodySetupAt(int32 BodyIndex)
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return false;
	}

	// 이 Body에 연결된 Constraint 제거
	for (int32 ConstraintIndex = static_cast<int32>(ConstraintSetups.size()) - 1; ConstraintIndex >= 0; --ConstraintIndex)
	{
		UPhysicsConstraintTemplate* Constraint = ConstraintSetups[ConstraintIndex];
		if (!Constraint)
		{
			continue;
		}

		const FName RemovedBodyName = SkeletalBodySetups[BodyIndex]->GetBoneName();

		const bool bConnected =
			Constraint->GetParentBoneName() == RemovedBodyName ||
			Constraint->GetChildBoneName() == RemovedBodyName;

		if (bConnected)
		{
			RemoveConstraintSetupAt(ConstraintIndex);
		}
	}

	USkeletalBodySetup* Removed = SkeletalBodySetups[BodyIndex];
	SkeletalBodySetups.erase(SkeletalBodySetups.begin() + BodyIndex);

	if (Removed)
	{
		UObjectManager::Get().DestroyObject(Removed);
	}

	// Body index가 밀렸으므로 index 기반 collision table은 무효화
	CollisionDisableTable.clear();

	UpdateBodySetupIndexMap();
	UpdateBoundsBodiesArray();
	RefreshPhysicsAssetChange();

	return true;
}

UPhysicsConstraintTemplate* UPhysicsAsset::GetConstraintSetup(int32 ConstraintIndex)
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return nullptr;
	}

	return ConstraintSetups[ConstraintIndex];
}

const UPhysicsConstraintTemplate* UPhysicsAsset::GetConstraintSetup(int32 ConstraintIndex) const
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return nullptr;
	}

	return ConstraintSetups[ConstraintIndex];
}

int32 UPhysicsAsset::FindConstraintIndex(FName ConstraintName) const
{
	if (ConstraintName.IsNone())
	{
		return -1;
	}

	for (int32 Index = 0; Index < static_cast<int32>(ConstraintSetups.size()); ++Index)
	{
		const UPhysicsConstraintTemplate* Constraint = ConstraintSetups[Index];
		if (!Constraint)
		{
			continue;
		}

		if (Constraint->GetConstraintName() == ConstraintName)
		{
			return Index;
		}
	}

	return -1;
}

UPhysicsConstraintTemplate* UPhysicsAsset::FindConstraintSetup(FName ConstraintName)
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return GetConstraintSetup(ConstraintIndex);
}

const UPhysicsConstraintTemplate* UPhysicsAsset::FindConstraintSetup(FName ConstraintName) const
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return GetConstraintSetup(ConstraintIndex);
}

void UPhysicsAsset::BodyFindConstraints(int32 BodyIndex, TArray<int32>& OutConstraintIndices) const
{
	OutConstraintIndices.clear();

	const USkeletalBodySetup* BodySetup = GetBodySetup(BodyIndex);
	if (!BodySetup)
	{
		return;
	}

	const FName BodyName = BodySetup->GetBoneName();

	for (int32 Index = 0; Index < static_cast<int32>(ConstraintSetups.size()); ++Index)
	{
		const UPhysicsConstraintTemplate* Constraint = ConstraintSetups[Index];
		if (!Constraint)
		{
			continue;
		}

		if (Constraint->GetParentBoneName() == BodyName ||
			Constraint->GetChildBoneName() == BodyName)
		{
			OutConstraintIndices.push_back(Index);
		}
	}
}

UPhysicsConstraintTemplate* UPhysicsAsset::AddConstraintSetup(
	FName ConstraintName,
	FName ParentBoneName,
	FName ChildBoneName)
{
	if (ConstraintName.IsNone() || ParentBoneName.IsNone() || ChildBoneName.IsNone())
	{
		return nullptr;
	}

	if (ParentBoneName == ChildBoneName)
	{
		return nullptr;
	}

	if (FindConstraintSetup(ConstraintName))
	{
		return nullptr;
	}

	if (FindBodyIndex(ParentBoneName) == -1 ||
		FindBodyIndex(ChildBoneName) == -1)
	{
		return nullptr;
	}

	UPhysicsConstraintTemplate* NewConstraint = UObjectManager::Get().CreateObject<UPhysicsConstraintTemplate>(this);

	NewConstraint->SetConstraintName(ConstraintName);
	NewConstraint->SetParentBoneName(ParentBoneName);
	NewConstraint->SetChildBoneName(ChildBoneName);

	FConstraintInstance& Instance = NewConstraint->GetDefaultInstance();
	Instance.ConstraintName = ConstraintName;
	Instance.ParentBoneName = ParentBoneName;
	Instance.ChildBoneName = ChildBoneName;

	ConstraintSetups.push_back(NewConstraint);

	RefreshPhysicsAssetChange();

	return NewConstraint;
}

bool UPhysicsAsset::RemoveConstraintSetup(FName ConstraintName)
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return RemoveConstraintSetupAt(ConstraintIndex);
}

bool UPhysicsAsset::RemoveConstraintSetupAt(int32 ConstraintIndex)
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return false;
	}

	UPhysicsConstraintTemplate* Removed = ConstraintSetups[ConstraintIndex];
	ConstraintSetups.erase(ConstraintSetups.begin() + ConstraintIndex);

	if (Removed)
	{
		UObjectManager::Get().DestroyObject(Removed);
	}

	RefreshPhysicsAssetChange();

	return true;
}

void UPhysicsAsset::DisableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return;
	}

	CollisionDisableTable[FRigidBodyIndexPair(BodyIndexA, BodyIndexB)] = true;

	RefreshPhysicsAssetChange();
}

void UPhysicsAsset::EnableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return;
	}

	CollisionDisableTable.erase(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));

	RefreshPhysicsAssetChange();
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return false;
	}

	const FRigidBodyIndexPair Pair(BodyIndexA, BodyIndexB);
	return CollisionDisableTable.find(Pair) == CollisionDisableTable.end();
}

void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.clear();

	for (int32 Index = 0; Index < static_cast<int32>(SkeletalBodySetups.size()); ++Index)
	{
		USkeletalBodySetup* BodySetup = SkeletalBodySetups[Index];
		if (!BodySetup)
		{
			continue;
		}

		const FName BoneName = BodySetup->GetBoneName();
		if (BoneName.IsNone())
		{
			continue;
		}

		BodySetupIndexMap[BoneName] = Index;
	}
}

void UPhysicsAsset::UpdateBoundsBodiesArray()
{
	BoundsBodies.clear();

	for (int32 Index = 0; Index < static_cast<int32>(SkeletalBodySetups.size()); ++Index)
	{
		if (SkeletalBodySetups[Index])
		{
			BoundsBodies.push_back(Index);
		}
	}
}

void UPhysicsAsset::RefreshPhysicsAssetChange()
{
	// TODO:
	// 나중에 PhysicsAsset Editor / SkeletalMeshComponent 쪽에서
	// asset 변경 알림을 받을 수 있도록 delegate를 연결하면 됨.
}
