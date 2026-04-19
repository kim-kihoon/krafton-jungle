#include "pch.h"
#include "PrimitiveComponent.h"

IMPLEMENT_CLASS(UPrimitiveComponent, USceneComponent)

void UPrimitiveComponent::Release()
{

}

void UPrimitiveComponent::TickComponent(float DeltaTime)
{
	USceneComponent::TickComponent(DeltaTime);
}
