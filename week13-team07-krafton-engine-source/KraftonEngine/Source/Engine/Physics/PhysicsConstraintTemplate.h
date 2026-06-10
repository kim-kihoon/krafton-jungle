#pragma once
#include "Object/Object.h"
#include "ConstraintInstance.h"

#include "Source/Engine/Physics/PhysicsConstraintTemplate.generated.h"

UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
public:
	GENERATED_BODY()

public:
	FName GetConstraintName() const
	{
		return ConstraintName;
	}

	void SetConstraintName(const FName& InName)
	{
		ConstraintName = InName;
		DefaultInstance.ConstraintName = InName;
	}

	FName GetParentBoneName() const
	{
		return ParentBoneName;
	}

	void SetParentBoneName(const FName& InName)
	{
		ParentBoneName = InName;
		DefaultInstance.ParentBoneName = InName;
	}

	FName GetChildBoneName() const
	{
		return ChildBoneName;
	}

	void SetChildBoneName(const FName& InName)
	{
		ChildBoneName = InName;
		DefaultInstance.ChildBoneName = InName;
	}

	FConstraintInstance& GetDefaultInstance()
	{
		return DefaultInstance;
	}

	const FConstraintInstance& GetDefaultInstance() const
	{
		return DefaultInstance;
	}

	void SetDefaultInstance(const FConstraintInstance& InInstance)
	{
		DefaultInstance = InInstance;

		ConstraintName = InInstance.ConstraintName;
		ParentBoneName = InInstance.ParentBoneName;
		ChildBoneName = InInstance.ChildBoneName;
	}

	bool IsValid() const
	{
		return !ParentBoneName.IsNone() && !ChildBoneName.IsNone();
	}

private:
	UPROPERTY(Edit, Save, Category = "Constraint", DisplayName = "Constraint Name")
	FName ConstraintName;

	UPROPERTY(Edit, Save, Category = "Constraint", DisplayName = "Parent Bone")
	FName ParentBoneName;

	UPROPERTY(Edit, Save, Category = "Constraint", DisplayName = "Child Bone")
	FName ChildBoneName;

	UPROPERTY(Edit, Save, Category = "Constraint", DisplayName = "Default Instance")
	FConstraintInstance DefaultInstance;
};