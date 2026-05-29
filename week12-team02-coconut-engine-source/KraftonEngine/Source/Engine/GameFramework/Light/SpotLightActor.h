#pragma once

#include "GameFramework/AActor.h"
#include "SpotLightActor.generated.h"

class UBillboardComponent;
class USpotLightComponent;

UCLASS(Actor)
class ASpotLightActor : public AActor
{
public:
	GENERATED_BODY(ASpotLightActor)

	void InitDefaultComponents();

private:
	USpotLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
