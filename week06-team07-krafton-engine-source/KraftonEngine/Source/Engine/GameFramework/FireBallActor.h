#pragma once
#include "GameFramework/AActor.h"

class UStaticMeshComponent;
class UFireBallComponent;
class AFireBallActor : public AActor
{
public:
	DECLARE_CLASS(AFireBallActor, AActor)
	AFireBallActor();
	void InitDefaultComponents();
	virtual void TickActor(float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction);

private:
	UStaticMeshComponent* StaticMeshComp = nullptr;
	UFireBallComponent* FireBallComp = nullptr;
};