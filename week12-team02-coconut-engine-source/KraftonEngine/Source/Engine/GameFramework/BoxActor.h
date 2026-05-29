#pragma once

#include "GameFramework/AActor.h"

#include "BoxActor.generated.h"

class UBoxComponent;

UCLASS(Actor)
class ABoxActor : public AActor
{
public:
	GENERATED_BODY(ABoxActor)

	ABoxActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UBoxComponent* GetBoxComponent() const { return BoxComponent; }

private:
	UBoxComponent* BoxComponent = nullptr;
};
