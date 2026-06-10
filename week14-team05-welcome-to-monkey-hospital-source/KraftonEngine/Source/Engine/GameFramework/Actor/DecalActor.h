#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/DecalActor.generated.h"
class UTextRenderComponent;
class UDecalComponent;
class UBillboardComponent;

UCLASS()
class ADecalActor : public AActor
{
public:
	GENERATED_BODY()
	ADecalActor();

	void InitDefaultComponents();

	UFUNCTION(Pure, Category="Actor|Components")
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
	TWeakObjectPtr<UDecalComponent> DecalComponent;
	TWeakObjectPtr<UBillboardComponent> BillboardComponent = nullptr;
	TWeakObjectPtr<UTextRenderComponent> TextRenderComponent = nullptr;
	
	const FString DefaultDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";
};
