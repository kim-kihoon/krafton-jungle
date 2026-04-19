#include "RotatingMovementComponent.h"

#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Render/Pipeline/RenderBus.h"
#include "SceneComponent.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	void AddDebugLine(FRenderBus& RenderBus, const FVector& Start, const FVector& End, const FColor& Color)
	{
		FDebugLineEntry Entry = {};
		Entry.Start = Start;
		Entry.End = End;
		Entry.Color = Color;
		RenderBus.AddDebugLineEntry(std::move(Entry));
	}

	FVector ComputePivotWorldLocation(const USceneComponent* SourceComponent, const FVector& PivotTranslation)
	{
		if (!SourceComponent)
		{
			return PivotTranslation;
		}

		const USceneComponent* ParentComponent = SourceComponent->GetParent();
		if (!ParentComponent)
		{
			return PivotTranslation;
		}

		return ParentComponent->GetWorldMatrix().TransformPositionWithW(PivotTranslation);
	}
}

IMPLEMENT_CLASS(URotatingMovementComponent, UMovementComponent)

void URotatingMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent || (RotationRate.IsNearlyZero() && PivotTranslation.Length() < EPSILON))
	{
		return;
	}

	FQuat DeltaQuat = (RotationRate * DeltaTime).ToQuaternion();

	if (PivotTranslation.Length() < EPSILON)
	{
		// 단순 자전
		if (bRotationInLocalSpace)
		{
			UpdatedSceneComponent->AddLocalRotation(DeltaQuat);
		}
		else
		{
			// 월드 축 기준 회전
			FQuat NewQuat = DeltaQuat * UpdatedSceneComponent->GetRelativeQuat();
			UpdatedSceneComponent->SetRelativeRotation(NewQuat);
		}
	}
	else
	{
		// 피벗 기준 공전 + 자전
		FVector RelativeLocation = UpdatedSceneComponent->GetRelativeLocation();
		FQuat RelativeQuat = UpdatedSceneComponent->GetRelativeQuat();

		FVector ToObject = RelativeLocation - PivotTranslation;
		FVector RotatedOffset = DeltaQuat.RotateVector(ToObject);
		FVector NewLocation = PivotTranslation + RotatedOffset;

		FQuat NewQuat = DeltaQuat * RelativeQuat;

		UpdatedSceneComponent->SetRelativeLocation(NewLocation);
		UpdatedSceneComponent->SetRelativeRotation(NewQuat);
	}
}

void URotatingMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	Ar << RotationRate.Pitch;
	Ar << RotationRate.Yaw;
	Ar << RotationRate.Roll;
	Ar << PivotTranslation.X;
	Ar << PivotTranslation.Y;
	Ar << PivotTranslation.Z;
	Ar << bRotationInLocalSpace;
}

void URotatingMovementComponent::CollectEditorVisualizations(FRenderBus& RenderBus) const
{
	USceneComponent* SourceComponent = GetUpdatedComponent();
	if (!SourceComponent)
	{
		AActor* OwnerActor = GetOwner();
		SourceComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	}
	if (!SourceComponent)
	{
		return;
	}

	const FVector ComponentWorldLocation = SourceComponent->GetWorldLocation();
	const FVector PivotWorldLocation = ComputePivotWorldLocation(SourceComponent, PivotTranslation);
	const float PivotDistance = FVector::Distance(ComponentWorldLocation, PivotWorldLocation);
	const float MarkerExtent = std::max(0.15f, PivotDistance * 0.12f);
	const FColor PivotColor(255, 196, 0, 255);
	const FColor LinkColor(255, 128, 0, 255);

	AddDebugLine(RenderBus, PivotWorldLocation - FVector(MarkerExtent, 0.0f, 0.0f), PivotWorldLocation + FVector(MarkerExtent, 0.0f, 0.0f), PivotColor);
	AddDebugLine(RenderBus, PivotWorldLocation - FVector(0.0f, MarkerExtent, 0.0f), PivotWorldLocation + FVector(0.0f, MarkerExtent, 0.0f), PivotColor);
	AddDebugLine(RenderBus, PivotWorldLocation - FVector(0.0f, 0.0f, MarkerExtent), PivotWorldLocation + FVector(0.0f, 0.0f, MarkerExtent), PivotColor);

	if (PivotDistance > EPSILON)
	{
		AddDebugLine(RenderBus, ComponentWorldLocation, PivotWorldLocation, LinkColor);
	}
}

void URotatingMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Rotation Rate", EPropertyType::Rotator, &RotationRate, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Pivot Translation", EPropertyType::Vec3, &PivotTranslation, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Rotation In Local Space", EPropertyType::Bool, &bRotationInLocalSpace, 0.0f, 0.0f, 0.1f });
}
