#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Light/AmbientLightActor.generated.h"
class UAmbientLightComponent;
class UBillboardComponent;

UCLASS()
class AAmbientLightActor : public AActor
{
public:
	GENERATED_BODY()
	void InitDefaultComponents();

private:
	TWeakObjectPtr<UAmbientLightComponent> LightComponent = nullptr;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
};
