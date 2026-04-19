#include "Component/MovementComponent.h"

#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"
#include <cmath>

// Base movement logic only; concrete movement types should be added instead.
IMPLEMENT_ABSTRACT_CLASS(UMovementComponent, UActorComponent)

void UMovementComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	TryAutoRegisterUpdatedComponent();
}

void UMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Auto Register Updated", EPropertyType::Bool, &bAutoRegisterUpdatedComponent });
}

void UMovementComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	// UpdatedComponent 포인터는 BeginPlay에서 재해결되므로 직렬화 제외.
	Ar << bAutoRegisterUpdatedComponent;
}

void UMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	UpdatedComponent = NewUpdatedComponent;
}

void UMovementComponent::TryAutoRegisterUpdatedComponent()
{
	if (!bAutoRegisterUpdatedComponent || UpdatedComponent != nullptr)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	SetUpdatedComponent(OwnerActor->GetRootComponent());
}
