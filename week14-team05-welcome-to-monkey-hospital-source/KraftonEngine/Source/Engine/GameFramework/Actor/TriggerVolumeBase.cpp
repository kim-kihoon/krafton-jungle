#include "GameFramework/Actor/TriggerVolumeBase.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/GameMode/GameModeBase.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Serialization/Archive.h"

namespace
{
	const char* GetPawnNameSafe(APawn* Pawn)
	{
		return Pawn ? Pawn->GetName().c_str() : "None";
	}

	const char* LexToString(EPhysicsCollisionRole Role)
	{
		switch (Role)
		{
		case EPhysicsCollisionRole::CharacterLocomotionProxy:
			return "CharacterLocomotionProxy";
		case EPhysicsCollisionRole::CharacterQueryProxy:
			return "CharacterQueryProxy";
		case EPhysicsCollisionRole::CharacterMeshPrimitive:
			return "CharacterMeshPrimitive";
		case EPhysicsCollisionRole::PartialReactionBody:
			return "PartialReactionBody";
		case EPhysicsCollisionRole::FullRagdollBody:
			return "FullRagdollBody";
		case EPhysicsCollisionRole::TriggerVolume:
			return "TriggerVolume";
		case EPhysicsCollisionRole::WorldStatic:
			return "WorldStatic";
		case EPhysicsCollisionRole::WorldDynamic:
			return "WorldDynamic";
		default:
			return "None";
		}
	}

	bool CanPrimitiveParticipateInTriggerOccupancy(const UPrimitiveComponent* Primitive, ECollisionChannel TriggerChannel)
	{
		if (!Primitive || !Primitive->IsCollisionEnabled())
		{
			return false;
		}

		const ECollisionEnabled CollisionEnabled = Primitive->GetCollisionEnabled();
		if (CollisionEnabled == ECollisionEnabled::PhysicsOnly)
		{
			return false;
		}

		if (!Primitive->GetGenerateOverlapEvents())
		{
			return false;
		}

		return Primitive->GetCollisionResponseToChannel(TriggerChannel) != ECollisionResponse::Ignore;
	}

	EPhysicsCollisionRole GetPrimitiveTriggerOccupancyRole(const APawn* Pawn, const UPrimitiveComponent* Primitive)
	{
		if (!Primitive)
		{
			return EPhysicsCollisionRole::None;
		}

		const ACharacter* Character = Cast<ACharacter>(const_cast<APawn*>(Pawn));
		if (!Character)
		{
			return EPhysicsCollisionRole::None;
		}

		return Character->GetCollisionRoleForComponent(Primitive);
	}

	bool CanPrimitiveOwnTriggerOccupancy(const APawn* Pawn, const UPrimitiveComponent* Primitive)
	{
		const ACharacter* Character = Cast<ACharacter>(const_cast<APawn*>(Pawn));
		if (!Character)
		{
			return true;
		}

		return Character->IsGameplayOverlapOwnerComponent(Primitive);
	}
}

void ATriggerVolumeBase::InitDefaultComponents(const FVector& Extent)
{
	TriggerBox = AddComponent<UBoxComponent>();
	SetRootComponent(TriggerBox);

	TriggerBox->SetBoxExtent(Extent);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionObjectType(ECollisionChannel::Trigger);
	TriggerBox->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

void ATriggerVolumeBase::PostDuplicate()
{
	Super::PostDuplicate();
	TriggerBox = Cast<UBoxComponent>(GetRootComponent());
}

void ATriggerVolumeBase::BeginPlay()
{
	if (!GetRootComponent())
	{
		InitDefaultComponents();
	}

	if (!TriggerBox)
	{
		TriggerBox = Cast<UBoxComponent>(GetRootComponent());
	}

	if (TriggerBox)
	{
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TriggerBox->SetCollisionObjectType(ECollisionChannel::Trigger);
		TriggerBox->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
		TriggerBox->SetGenerateOverlapEvents(true);
		TriggerBox->SetSimulatePhysics(false);
	}

	Super::BeginPlay();

	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddWeakUObject(this, &ATriggerVolumeBase::HandleBeginOverlap);
		TriggerBox->OnComponentEndOverlap.AddWeakUObject(this, &ATriggerVolumeBase::HandleEndOverlap);
	}
}

APawn* ATriggerVolumeBase::ResolvePossessedPawn(AActor* OtherActor) const
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	return (Pawn && Pawn->IsPossessed()) ? Pawn : nullptr;
}

bool ATriggerVolumeBase::AddOccupyingPawn(APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}

	return OccupyingPawns.insert(TWeakObjectPtr<APawn>(Pawn)).second;
}

bool ATriggerVolumeBase::RemoveOccupyingPawn(APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}

	return OccupyingPawns.erase(TWeakObjectPtr<APawn>(Pawn)) > 0;
}

bool ATriggerVolumeBase::IsPawnStillInsideTrigger(APawn* Pawn) const
{
	UBoxComponent* Box = TriggerBox.Get();
	if (!Pawn || !Box || !Box->IsCollisionEnabled())
	{
		return false;
	}

	const FBoundingBox TriggerBounds = Box->GetWorldBoundingBox();
	const ECollisionChannel TriggerChannel = Box->GetCollisionObjectType();

	for (UPrimitiveComponent* Primitive : Pawn->GetPrimitiveComponents())
	{
		if (!Primitive || Primitive == Box)
		{
			continue;
		}

		if (!CanPrimitiveParticipateInTriggerOccupancy(Primitive, TriggerChannel))
		{
			continue;
		}

		if (!CanPrimitiveOwnTriggerOccupancy(Pawn, Primitive))
		{
			continue;
		}

		if (TriggerBounds.IsIntersected(Primitive->GetWorldBoundingBox()))
		{
			UE_LOG("[TriggerVolume] TriggerOccupancyRoleAccepted Trigger=%s Pawn=%s Primitive=%s Role=%s",
				GetName().c_str(),
				GetPawnNameSafe(Pawn),
				Primitive->GetName().c_str(),
				LexToString(GetPrimitiveTriggerOccupancyRole(Pawn, Primitive)));
			return true;
		}
	}

	return false;
}

void ATriggerVolumeBase::HandleBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	APawn* Pawn = ResolvePossessedPawn(OtherActor);
	if (!Pawn)
	{
		return;
	}

	UE_LOG("[TriggerVolume] BeginOverlapReceived Trigger=%s Pawn=%s OtherComp=%s OccupantsBefore=%d",
		GetName().c_str(),
		GetPawnNameSafe(Pawn),
		OtherComp ? OtherComp->GetName().c_str() : "None",
		GetOccupyingPawnCount());

	if (!AddOccupyingPawn(Pawn))
	{
		return;
	}

	UE_LOG("[TriggerVolume] OccupancyAdded Trigger=%s Pawn=%s Occupants=%d",
		GetName().c_str(),
		GetPawnNameSafe(Pawn),
		GetOccupyingPawnCount());

	OnPossessedPawnEntered(Pawn);

	if (UWorld* W = GetWorld())
	{
		if (AGameModeBase* GM = W->GetGameMode())
		{
			GM->OnPossessedPawnEnteredTrigger(this, Pawn);
		}
	}
}

void ATriggerVolumeBase::HandleEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/)
{
	APawn* Pawn = ResolvePossessedPawn(OtherActor);
	if (!Pawn)
	{
		return;
	}

	UE_LOG("[TriggerVolume] EndOverlapReceived Trigger=%s Pawn=%s OtherComp=%s OccupantsBefore=%d",
		GetName().c_str(),
		GetPawnNameSafe(Pawn),
		OtherComp ? OtherComp->GetName().c_str() : "None",
		GetOccupyingPawnCount());

	if (OccupyingPawns.find(TWeakObjectPtr<APawn>(Pawn)) == OccupyingPawns.end())
	{
		return;
	}

	if (IsPawnStillInsideTrigger(Pawn))
	{
		UE_LOG("[TriggerVolume] OccupancyRetainedAfterRecheck Trigger=%s Pawn=%s Occupants=%d OverlapOwnerRecheck=true",
			GetName().c_str(),
			GetPawnNameSafe(Pawn),
			GetOccupyingPawnCount());
		return;
	}

	if (!RemoveOccupyingPawn(Pawn))
	{
		return;
	}

	UE_LOG("[TriggerVolume] OccupancyRemoved Trigger=%s Pawn=%s Occupants=%d",
		GetName().c_str(),
		GetPawnNameSafe(Pawn),
		GetOccupyingPawnCount());

	OnPossessedPawnExited(Pawn);

	if (UWorld* W = GetWorld())
	{
		if (AGameModeBase* GM = W->GetGameMode())
		{
			GM->OnPossessedPawnExitedTrigger(this, Pawn);
		}
	}
}
