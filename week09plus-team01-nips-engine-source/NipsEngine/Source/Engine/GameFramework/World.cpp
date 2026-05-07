#include "GameFramework/World.h"
#include <algorithm>
#include "Collision/CollisionSystem.h"
#include "Component/CameraComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Audio/AudioSystem.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/PrimitiveActors.h"
#include "Physics/JoltPhysicsSystem.h"

DEFINE_CLASS(UWorld, UObject)
REGISTER_FACTORY(UWorld)

namespace
{
	bool ShouldRunBeginPlay(EWorldType WorldType)
	{
		return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
	}
}

// FName, UUID 발급, 메모리 추적 등을 위해 UObjectManager를 통해 생성, 삭제한다.
UWorld::UWorld()
{
	PersistentLevel = UObjectManager::Get().CreateObject<ULevel>();
}

// 소멸 역시 UObjectManager를 통해 처리한다.
UWorld::~UWorld()
{
	if (FJoltPhysicsSystem::Get().IsCurrentWorld(this))
	{
		FJoltPhysicsSystem::Get().Shutdown();
	}
	SpatialIndex.Clear();
	UObjectManager::Get().DestroyObject(PersistentLevel);
}

/* @brief 비노출 필드를 복사하고, Level을 깊은 복사한 뒤, 복제된 액터들의 소속을 자기 자신으로 재설정합니다. */
void UWorld::PostDuplicate(UObject* Original)
{
	// UWorld 생성자가 기본 PersistentLevel을 생성하므로,
	// 원본의 레벨로 교체하기 전에 먼저 해제합니다.
	if (PersistentLevel)
	{
		UObjectManager::Get().DestroyObject(PersistentLevel);
		PersistentLevel = nullptr;
	}

	const UWorld* OrigWorld = Cast<UWorld>(Original);

	// 프로퍼티 시스템에 노출되지 않은 필드를 직접 복사합니다.
	WorldType      = OrigWorld->WorldType;
	ActiveCamera   = nullptr;
	ActiveCameraComponent = nullptr;
	bHasBegunPlay  = false; // 항상 미시작 상태로 시작

	// PersistentLevel 을 깊은 복사한 뒤, 복제된 액터들의 소속을 새 월드로 재설정합니다.
	if (OrigWorld->PersistentLevel)
	{
		PersistentLevel = Cast<ULevel>(OrigWorld->PersistentLevel->Duplicate());
		for (AActor* DuplicatedActor : PersistentLevel->GetActors())
		{
			if (!DuplicatedActor) continue;

			const TArray<UActorComponent*>& Comps = DuplicatedActor->GetComponents();
			for (int32 i = static_cast<int32>(Comps.size()) - 1; i >= 0; --i)
			{
				if (Comps[i]) Comps[i]->OnUnregister();
			}

			DuplicatedActor->SetWorld(this);

			for (UActorComponent* Comp : Comps)
			{
				if (Comp) Comp->OnRegister();
			}
		}
	}

	RebuildSpatialIndex();
}

APlayerStartActor* UWorld::FindPlayerStart() const
{
	if (PersistentLevel == nullptr)
	{
		return nullptr;
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (APlayerStartActor* PlayerStart = Cast<APlayerStartActor>(Actor))
		{
			if (PlayerStart->IsActive())
			{
				return PlayerStart;
			}
		}
	}

	return nullptr;
}

APawnActor* UWorld::FindPawn() const
{
	if (PersistentLevel == nullptr)
	{
		return nullptr;
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (APawnActor* Pawn = Cast<APawnActor>(Actor))
		{
			if (Pawn->IsActive())
			{
				return Pawn;
			}
		}
	}

	return nullptr;
}

void UWorld::BeginPlay()
{
	if (bHasBegunPlay || !ShouldRunBeginPlay(WorldType))
	{
		return;
	}

	bHasBegunPlay = true;
	if (WorldType != EWorldType::Editor)
	{
		if (ActiveCameraComponent)
		{
			FAudioSystem::Get().SetListenerTransform(
				ActiveCameraComponent->GetWorldLocation(),
				ActiveCameraComponent->GetForwardVector(),
				ActiveCameraComponent->GetUpVector());
		}
		else if (ActiveCamera)
		{
			FAudioSystem::Get().SetListenerTransform(
				ActiveCamera->GetLocation(),
				ActiveCamera->GetForwardVector(),
				ActiveCamera->GetUpVector());
		}
	}
	PersistentLevel->BeginPlay();
	RebuildSpatialIndex();
	if (ShouldRunBeginPlay(WorldType))
	{
		FJoltPhysicsSystem::Get().RebuildWorld(this);
	}
}

void UWorld::Tick(float DeltaTime)
{
	if (!PersistentLevel)
		return;

	if (WorldType != EWorldType::Editor)
	{
		if (ActiveCameraComponent)
		{
			FAudioSystem::Get().SetListenerTransform(
				ActiveCameraComponent->GetWorldLocation(),
				ActiveCameraComponent->GetForwardVector(),
				ActiveCameraComponent->GetUpVector());
		}
		else if (ActiveCamera)
		{
			FAudioSystem::Get().SetListenerTransform(
				ActiveCamera->GetLocation(),
				ActiveCamera->GetForwardVector(),
				ActiveCamera->GetUpVector());
		}
	}

	if (WorldType == EWorldType::Editor)
		PersistentLevel->TickEditor(DeltaTime);
	else if (ShouldRunBeginPlay(WorldType))
	{
		PersistentLevel->TickGame(DeltaTime);
		FJoltPhysicsSystem::Get().Step(this, DeltaTime);
		SyncSpatialIndex();
		FCollisionSystem::UpdateWorldCollision(this);
	}
}

void UWorld::DeactivateActor(AActor* Actor)
{
	if (!Actor || Actor->GetFocusedWorld() != this)
	{
		return;
	}

	Actor->SetActive(false);
	Actor->SetVisible(false);
	SpatialIndex.UnregisterActor(Actor);
}

void UWorld::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	if (bHasBegunPlay)
	{
		bHasBegunPlay = false;
		PersistentLevel->EndPlay(EndPlayReason);
		if (ShouldRunBeginPlay(WorldType))
		{
			FJoltPhysicsSystem::Get().Shutdown();
		}
	}
}

void UWorld::RebuildSpatialIndex()
{
	SpatialIndex.Rebuild(this);
}

void UWorld::SyncSpatialIndex()
{
	SpatialIndex.FlushDirtyBounds();
}

bool UWorld::LineTraceSingle(const FRay& Ray, float MaxDistance, FHitResult& OutHit, const AActor* IgnoredActor)
{
	OutHit.Reset();

	if (MaxDistance <= 0.0f)
	{
		return false;
	}

	FWorldSpatialIndex::FPrimitiveRayQueryScratch Scratch;
	TArray<UPrimitiveComponent*> Candidates;
	TArray<float> BroadHitTs;
	SpatialIndex.RayQueryPrimitives(Ray, Candidates, BroadHitTs, Scratch);

	bool bFoundHit = false;
	float ClosestDistance = MaxDistance;

	for (UPrimitiveComponent* Candidate : Candidates)
	{
		if (Candidate == nullptr || Candidate->GetOwner() == IgnoredActor)
		{
			continue;
		}
		if (WorldType != EWorldType::Editor && Candidate->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
		{
			continue;
		}
		if (WorldType != EWorldType::Editor && Candidate->IsEditorOnly())
		{
			continue;
		}

		FHitResult CandidateHit;
		if (!Candidate->Raycast(Ray, CandidateHit) || !CandidateHit.IsValid())
		{
			continue;
		}

		if (CandidateHit.Distance < 0.0f || CandidateHit.Distance > ClosestDistance)
		{
			continue;
		}

		ClosestDistance = CandidateHit.Distance;
		OutHit = CandidateHit;
		bFoundHit = true;
	}

	return bFoundHit;
}

bool UWorld::LineTraceMulti(const FRay& Ray, float MaxDistance, TArray<FHitResult>& OutHits, const AActor* IgnoredActor)
{
    OutHits.clear();

    if (MaxDistance <= 0.0f)
        return false;

    FWorldSpatialIndex::FPrimitiveRayQueryScratch Scratch;
    TArray<UPrimitiveComponent*> Candidates;
    TArray<float> BroadHitTs;
    SpatialIndex.RayQueryPrimitives(Ray, Candidates, BroadHitTs, Scratch);

    for (UPrimitiveComponent* Candidate : Candidates)
    {
        if (Candidate == nullptr || Candidate->GetOwner() == IgnoredActor)
            continue;
        if (WorldType != EWorldType::Editor && Candidate->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
            continue;
        if (WorldType != EWorldType::Editor && Candidate->IsEditorOnly())
            continue;

        FHitResult CandidateHit;
        if (!Candidate->Raycast(Ray, CandidateHit) || !CandidateHit.IsValid())
            continue;
        if (CandidateHit.Distance < 0.0f || CandidateHit.Distance > MaxDistance)
            continue;

        OutHits.push_back(CandidateHit);
    }

    std::sort(OutHits.begin(), OutHits.end(), [](const FHitResult& A, const FHitResult& B)
    {
        return A.Distance < B.Distance;
    });

    return !OutHits.empty();
}

FLightHandle UWorld::RegisterLight(ULightComponentBase* Comp)
{
	FLightHandle LightHandle;
	FLightSlot LightSlot;

	if (FreeLightSlotList.empty())
	{
		// 새로 생성
		uint32 Index = static_cast<uint32>(WorldLightSlots.size());
		LightSlot.LightData = Comp;
		LightSlot.Generation = 0;
		LightSlot.bAlive = true;

		WorldLightSlots.push_back(LightSlot);

		LightHandle.Index = Index;
		LightHandle.Generation = WorldLightSlots[Index].Generation;
	}
	else
	{
		// Free Slot 사용
		uint32 Index = FreeLightSlotList.back();
		FreeLightSlotList.pop_back();
		WorldLightSlots[Index].Generation += 1;
		WorldLightSlots[Index].LightData = Comp;
		WorldLightSlots[Index].bAlive = true;

		LightHandle.Index = Index;
		LightHandle.Generation = WorldLightSlots[Index].Generation;
	}

	Comp->SetLightHandle(LightHandle);

	return LightHandle;
}

void UWorld::UnregisterLight(ULightComponentBase* Comp)
{
	FLightHandle LightHandle = Comp->GetLightHandle();
	// LightHandle이 없거나, 해당 Slot에 다른 데이터가 들어가 있으면 등록 해제 취소
	if (!LightHandle.IsValid() || WorldLightSlots[LightHandle.Index].Generation != LightHandle.Generation)
	{
		return;
	}

	WorldLightSlots[LightHandle.Index].bAlive = false;
	WorldLightSlots[LightHandle.Index].LightData = nullptr;
	FreeLightSlotList.push_back(LightHandle.Index);
}
