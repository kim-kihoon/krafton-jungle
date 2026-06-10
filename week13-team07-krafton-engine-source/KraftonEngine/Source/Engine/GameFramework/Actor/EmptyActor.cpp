#include "GameFramework/Actor/EmptyActor.h"
#include "Component/SceneComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Materials/MaterialManager.h"

void AEmptyActor::InitDefaultComponents()
{
	RootSceneComponent = AddComponent<USceneComponent>();
	SetRootComponent(RootSceneComponent);

	BillboardComponent = AddComponent<UBillboardComponent>();
	BillboardComponent->AttachToComponent(RootSceneComponent);
	BillboardComponent->SetAbsoluteScale(true);
	BillboardComponent->SetEditorOnlyComponent(true);
	BillboardComponent->SetHiddenInComponentTree(true);
	BillboardComponent->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(EditorIconMaterialPath));
}

void AEmptyActor::PostDuplicate()
{
	Super::PostDuplicate();
	RootSceneComponent = Cast<USceneComponent>(GetRootComponent());
	if (!RootSceneComponent)
	{
		RootSceneComponent = GetComponentByClass<USceneComponent>();
	}
	BillboardComponent = GetComponentByClass<UBillboardComponent>();
}

void AEmptyActor::OnOwnedComponentRemoved(UActorComponent* Component)
{
	Super::OnOwnedComponentRemoved(Component);
	if (Component == RootSceneComponent)
	{
		RootSceneComponent = nullptr;
	}
	if (Component == BillboardComponent)
	{
		BillboardComponent = nullptr;
	}
}
