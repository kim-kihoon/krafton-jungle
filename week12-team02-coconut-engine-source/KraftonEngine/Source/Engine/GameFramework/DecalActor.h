#pragma once

#include "GameFramework/AActor.h"
#include "DecalActor.generated.h"

class UTextRenderComponent;
class UDecalComponent;
class UBillboardComponent;

UCLASS(Actor)
class ADecalActor : public AActor
{
public:
	GENERATED_BODY(ADecalActor)

	ADecalActor();

	void InitDefaultComponents();

	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
	UDecalComponent* DecalComponent;
	UBillboardComponent* BillboardComponent = nullptr;
	UTextRenderComponent* TextRenderComponent = nullptr;
	
	const FString DefaultDecalMaterialPath = "Asset/Materials/Editor/DefaultDecal.mat";
};
