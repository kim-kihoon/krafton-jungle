#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/HeightFogActor.generated.h"
class UHeightFogComponent;
class UBillboardComponent;

UCLASS()
class AHeightFogActor : public AActor
{
public:
	GENERATED_BODY()
	AHeightFogActor();
	void InitDefaultComponents();

	UFUNCTION(Pure, Category="Actor|Components")
	UHeightFogComponent* GetFogComponent() const { return FogComponent; }

private:
	TWeakObjectPtr<UHeightFogComponent> FogComponent = nullptr;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
};
