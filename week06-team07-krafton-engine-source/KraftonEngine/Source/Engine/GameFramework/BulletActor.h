#pragma once

#include "GameFramework/AActor.h"?


class UProjectileMovementComponent;
class UStaticMeshComponent;
class ABulletActor : public AActor
{
public:
	DECLARE_CLASS(ABulletActor, AActor)
	ABulletActor() {}

	void InitDefaultComponents();

private:
	UStaticMeshComponent* MeshComponent = nullptr;
	UProjectileMovementComponent* MovementComp = nullptr;
};
