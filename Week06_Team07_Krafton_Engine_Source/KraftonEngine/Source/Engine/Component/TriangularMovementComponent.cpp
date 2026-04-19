#include "TriangularMovementComponent.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UTriangularMovementComponent, UMovementComponent)


void UTriangularMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	RunningTime += DeltaTime;

	FVector Pos = Owner->GetActorLocation();
	Pos.Z = BaseZ + std::abs(std::sin(RunningTime * 3.0f)) * BounceHeight;
	Owner->SetActorLocation(Pos);
}

void UTriangularMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "BounceHeight", EPropertyType::Float, &BounceHeight });
}

void UTriangularMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	Ar << RunningTime;
	Ar << Radius;
	Ar << BaseZ;
	Ar << BounceHeight;
}

void UTriangularMovementComponent::BeginPlay()
{
	BaseZ = GetOwner()->GetActorLocation().Z;
}
