#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Light/SpotLightActor.generated.h"
class UBillboardComponent;
class USpotLightComponent;

UCLASS()
class ASpotLightActor : public AActor
{
public:
	GENERATED_BODY()
	void InitDefaultComponents();

private:
	TWeakObjectPtr<USpotLightComponent> LightComponent = nullptr;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
};
