#include "pch.h"
#include "Actor.h"
#include "Level.h"
#include "World.h"
#include "ObjectFactory.h"

#include "SceneComponent.h"
#include "SphereComponent.h"
#include "ArrowComponent.h"
#include "ResourceManager.h"


IMPLEMENT_CLASS(AActor, UObject)

UWorld* AActor::GetWorld()
{
	if (OwningLevel)
	{
		return OwningLevel->GetWorld();
	}
	return nullptr;
}

void AActor::BeginPlay()
{
}

void AActor::Tick(float DeltaTime)
{
	for (int32 i = 0; i < Components.Size(); ++i)
	{
		Components[i]->TickComponent(DeltaTime);
	}

	for (int32 i = 0; i < Components.Size(); ++i)
	{
		if (!Components[i]->IsA(USceneComponent::StaticClass())) continue;
		if (RootComponent == Components[i]) continue;

		USceneComponent* SceneComponent = static_cast<USceneComponent*>(Components[i]);

		SceneComponent->SetPosition(RootComponent->GetPosition());

		FVector RootScale = RootComponent->GetScale();
		FVector RelativeScale = SceneComponent->GetRelativeScale();

		SceneComponent->SetScale({ RootScale.X * RelativeScale.X , RootScale.Y * RelativeScale.Y , RootScale.Z * RelativeScale.Z });

	}
}

void AActor::Release()
{
	for (size_t i = 0; i < Components.Size(); ++i)
	{
		UObjectFactory::DestroyObject(Components[i]);
	}

	// Actor 컴포넌트 또한 GUIObject가 관리한다
	Components.Clear();
	if (bIsSelected) {
		OwningLevel->GetWorld()->SelectedActor = nullptr;
	}
}
