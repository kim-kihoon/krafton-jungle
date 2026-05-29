#pragma once

#include "GameFramework/AActor.h"
#include "SphereActor.generated.h"

class USphereComponent;

UCLASS(Actor)
class ASphereActor : public AActor
{
public:
	GENERATED_BODY(ASphereActor)

	ASphereActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;
	void BeginPlay() override;

	USphereComponent* GetSphereComponent() const { return SphereComponent; }

private:
	USphereComponent* SphereComponent = nullptr;
};
