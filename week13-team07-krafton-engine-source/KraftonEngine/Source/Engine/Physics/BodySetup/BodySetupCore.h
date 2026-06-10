#pragma once

#include "Object/Object.h"
#include "Object/FName.h"
#include "Physics/BodySetup/BodySetupEnums.h"

#include "Source/Engine/Physics/BodySetup/BodySetupCore.generated.h"

class FArchive;

// UE: UBodySetupCore — PhysicsCore/Public/BodySetupCore.h
UCLASS()
class UBodySetupCore : public UObject
{
public:
	GENERATED_BODY()

	ECollisionTraceFlag GetCollisionTraceFlag() const { return CollisionTraceFlag; }
	EPhysicsType GetPhysicsType() const { return PhysicsType; }
	EBodyCollisionResponse GetCollisionResponse() const { return CollisionResponse; }

	static void SerializeCore(FArchive& Ar, UBodySetupCore& BodySetupCore);

	FName GetBoneName() const { return BoneName; }
	void SetBoneName(FName InBoneName) { BoneName = InBoneName; }

protected:
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Bone Name")
	FName BoneName;

	UPROPERTY(Edit, Save, Category="Physics")
	EPhysicsType PhysicsType = EPhysicsType::Default;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Collision Complexity")
	ECollisionTraceFlag CollisionTraceFlag = ECollisionTraceFlag::UseDefault;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Collision Response")
	EBodyCollisionResponse CollisionResponse = EBodyCollisionResponse::Enabled;
};
