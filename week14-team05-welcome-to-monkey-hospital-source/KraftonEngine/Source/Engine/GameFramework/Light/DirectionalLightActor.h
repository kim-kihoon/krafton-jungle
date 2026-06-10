#pragma once
#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Light/DirectionalLightActor.generated.h"
class UBillboardComponent;
class UDirectionalLightComponent;

UCLASS()
class ADirectionalLightActor : public AActor
{
public:
	GENERATED_BODY()
	void InitDefaultComponents();

private:
	TWeakObjectPtr<UDirectionalLightComponent> LightComponent = nullptr;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
};
