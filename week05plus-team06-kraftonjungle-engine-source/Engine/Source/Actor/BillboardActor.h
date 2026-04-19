#pragma once
#include "CoreMinimal.h"
#include "Actor.h"


class UBillboardComponent;

class ENGINE_API ABillboardActor : public AActor
{
public:
	DECLARE_RTTI(ABillboardActor, AActor)
	GENERATE_SHALLOW_CLONE(ABillboardActor)

	virtual void PostSpawnInitialize() override;

	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	UBillboardComponent* BillboardComp = nullptr;
};

