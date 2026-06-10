#pragma once

#include "GameFramework/AActor.h"

#include "Source/Engine/GameFramework/Actor/EmptyActor.generated.h"
class UBillboardComponent;
class USceneComponent;

UCLASS()
class AEmptyActor : public AActor
{
public:
	GENERATED_BODY()

	void InitDefaultComponents();
	void PostDuplicate() override;

protected:
	void OnOwnedComponentRemoved(UActorComponent* Component) override;

private:
	USceneComponent* RootSceneComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;

	const FString EditorIconMaterialPath = "Content/Material/Editor/EmptyActor.mat";
};
