#pragma once

#include "GameFramework/AActor.h"
#include "AmbientLightActor.generated.h"

class UAmbientLightComponent;
class UBillboardComponent;

UCLASS(Actor)
class AAmbientLightActor : public AActor
{
public:
	GENERATED_BODY(AAmbientLightActor)

	void InitDefaultComponents();

private:
	UAmbientLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
