#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/SphereActor.generated.h"
class USphereComponent;

UCLASS()
class ASphereActor : public AActor
{
public:
	GENERATED_BODY()
	ASphereActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;
	void BeginPlay() override;

	UFUNCTION(Pure, Category="Actor|Components")
	USphereComponent* GetSphereComponent() const { return SphereComponent; }

private:
	TWeakObjectPtr<USphereComponent> SphereComponent;
};
