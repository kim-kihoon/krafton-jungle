#pragma once

#include "GameFramework/AActor.h"
#include "PointLightActor.generated.h"

class UBillboardComponent;
class UPointLightComponent;

UCLASS(Actor)
class APointLightActor : public AActor
{
public:
	GENERATED_BODY(APointLightActor)

	void InitDefaultComponents();

private:
	UPointLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
