#pragma once

#include "GameFramework/AActor.h"
#include "HeightFogActor.generated.h"

class UHeightFogComponent;
class UBillboardComponent;

UCLASS(Actor)
class AHeightFogActor : public AActor
{
public:
	GENERATED_BODY(AHeightFogActor)

	AHeightFogActor();
	void InitDefaultComponents();

	UHeightFogComponent* GetFogComponent() const { return FogComponent; }

private:
	UHeightFogComponent* FogComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
