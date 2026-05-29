#pragma once

#include "GameFramework/AActor.h"
#include "CapsuleActor.generated.h"

class UCapsuleComponent;

UCLASS(Actor)
class ACapsuleActor : public AActor
{
public:
	GENERATED_BODY(ACapsuleActor)

	ACapsuleActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }

private:
	UCapsuleComponent* CapsuleComponent = nullptr;
};
