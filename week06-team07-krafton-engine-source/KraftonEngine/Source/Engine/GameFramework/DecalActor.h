#pragma once

#include "GameFramework/AActor.h"?

class UDecalComponent;
class UBillboardComponent;

class ADecalActor : public AActor
{
public:
	DECLARE_CLASS(ADecalActor, AActor)
	ADecalActor() {}

	void InitDefaultComponents();

private:
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* SpriteComponent = nullptr;
};

