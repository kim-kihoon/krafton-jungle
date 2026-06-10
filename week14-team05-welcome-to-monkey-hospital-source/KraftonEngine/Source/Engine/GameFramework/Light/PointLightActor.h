#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Light/PointLightActor.generated.h"
class UBillboardComponent;
class UPointLightComponent;

UCLASS()
class APointLightActor : public AActor
{
public:
	GENERATED_BODY()
	void InitDefaultComponents();

private:
	TWeakObjectPtr<UPointLightComponent> LightComponent = nullptr;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
};
