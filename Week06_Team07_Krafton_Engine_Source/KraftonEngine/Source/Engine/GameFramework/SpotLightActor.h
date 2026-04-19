#pragma once

#include "GameFramework/AActor.h"

class UDecalComponent;
class UBillboardComponent;
class USceneComponent;

class ASpotLightActor : public AActor
{
public:
	DECLARE_CLASS(ASpotLightActor, AActor)
	ASpotLightActor() {}

	void InitDefaultComponents();

private:
	USceneComponent* Root = nullptr;
	UBillboardComponent* LightSource = nullptr;
	//UBillboardComponent* LightShaft = nullptr;
	UDecalComponent* FloorDecal = nullptr;
};
