#pragma once
#include "Object/Object.h"
#include "Math/Transform.h"
#include "Source/Engine/Physics/ConstraintInstance.generated.h"

struct FPhysScene;
struct FBodyInstance;
struct FBodyInstanceInitParams;

namespace physx
{
	class PxD6Joint;
}

// typedef FPhysicsConstraintHandle_PhysX		FPhysicsConstraintHandle;
// PhysX 에 대한 직접적인 노출이 아닌 추상화를 위한 장치
struct FPhysicsConstraintHandle
{
	physx::PxD6Joint* Joint = nullptr;

	bool IsValid() const
	{
		return Joint != nullptr;
	}

	void Reset()
	{
		Joint = nullptr;
	}

	physx::PxD6Joint* GetPxD6Joint() const
	{
		return Joint;
	}
};


USTRUCT()
struct FConstraintInstanceBase
{
	GENERATED_BODY()

public:
	FConstraintInstanceBase()
	{
		Reset();
	}

	void Reset()
	{
		ConstraintIndex = -1;
		ConstraintHandle.Reset();
		PhysScene = nullptr;
	}

	FPhysScene* GetPhysicsScene()
	{
		return PhysScene;
	}

	const FPhysScene* GetPhysicsScene() const
	{
		return PhysScene;
	}

	bool IsValidConstraintInstance() const
	{
		return ConstraintHandle.IsValid();
	}

public:
	// SkeletalMeshComponent::Constraints 안에서의 index
	int32 ConstraintIndex = -1;

	// 내부 물리 constraint 핸들
	FPhysicsConstraintHandle ConstraintHandle;

	// 이 constraint가 속한 physics scene
	FPhysScene* PhysScene = nullptr;
};

UENUM()
enum class EConstraintMotion : uint8
{
	Free,
	Limited,
	Locked
};

USTRUCT()
struct FConstraintFrame
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category = "Constraint")
	FVector Position;

	UPROPERTY(Edit, Save, Category = "Constraint")
	FRotator Rotation;
};

USTRUCT()
struct FConstraintInstance : public FConstraintInstanceBase
{
	GENERATED_BODY()

public:
	// Identity / bone binding

	UPROPERTY(Edit, Save, Category = "Constraint")
	FName ConstraintName;

	UPROPERTY(Edit, Save, Category = "Constraint")
	FName ParentBoneName;

	UPROPERTY(Edit, Save, Category = "Constraint")
	FName ChildBoneName;

	// Local joint frame
	// Parent body local / Child body local

	UPROPERTY(Edit, Save, Category = "Constraint")
	FConstraintFrame ParentFrame;

	UPROPERTY(Edit, Save, Category = "Constraint")
	FConstraintFrame ChildFrame;

	// Collision

	UPROPERTY(Edit, Save, Category = "Constraint")
	bool bDisableCollision = true;

	// Linear DOF
	// Ragdoll은 보통 Linear는 Locked

	UPROPERTY(Edit, Save, Category = "Linear Limit")
	EConstraintMotion LinearXMotion = EConstraintMotion::Locked;

	UPROPERTY(Edit, Save, Category = "Linear Limit")
	EConstraintMotion LinearYMotion = EConstraintMotion::Locked;

	UPROPERTY(Edit, Save, Category = "Linear Limit")
	EConstraintMotion LinearZMotion = EConstraintMotion::Locked;

	UPROPERTY(Edit, Save, Category = "Linear Limit")
	float LinearLimitSize = 0.0f;

	// Angular DOF
	UPROPERTY(Edit, Save, Category = "Angular Limit")
	EConstraintMotion Swing1Motion = EConstraintMotion::Limited;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	EConstraintMotion Swing2Motion = EConstraintMotion::Limited;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	EConstraintMotion TwistMotion = EConstraintMotion::Limited;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	float Swing1LimitDegrees = 30.0f;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	float Swing2LimitDegrees = 30.0f;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	float TwistLimitMinDegrees = -45.0f;

	UPROPERTY(Edit, Save, Category = "Angular Limit")
	float TwistLimitMaxDegrees = 45.0f;

public:
	bool IsValidSetup() const
	{
		return !ParentBoneName.IsNone() && !ChildBoneName.IsNone();
	}

	void SetLinearMotion(
		EConstraintMotion InX,
		EConstraintMotion InY,
		EConstraintMotion InZ)
	{
		LinearXMotion = InX;
		LinearYMotion = InY;
		LinearZMotion = InZ;
	}

	void SetAngularMotion(
		EConstraintMotion InSwing1,
		EConstraintMotion InSwing2,
		EConstraintMotion InTwist)
	{
		Swing1Motion = InSwing1;
		Swing2Motion = InSwing2;
		TwistMotion = InTwist;
	}

	void SetAngularLimits(
		float InSwing1,
		float InSwing2,
		float InTwistMin,
		float InTwistMax)
	{
		Swing1LimitDegrees = InSwing1;
		Swing2LimitDegrees = InSwing2;
		TwistLimitMinDegrees = InTwistMin;
		TwistLimitMaxDegrees = InTwistMax;
	}

	void InitConstraint(
		FBodyInstance* ParentBody,
		FBodyInstance* ChildBody,
		const FBodyInstanceInitParams& InitParams);
	void TermConstraint();
};