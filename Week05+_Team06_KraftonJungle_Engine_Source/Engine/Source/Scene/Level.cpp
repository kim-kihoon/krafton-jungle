#include "Level.h"

#include "Core/Paths.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Object/ObjectFactory.h"
#include "Component/PrimitiveComponent.h"
#include "Object/Class.h"

#include "Serializer/SceneSerializer.h"
#include "World/World.h"
#include <algorithm>



#include "Component/LineBatchComponent.h"

IMPLEMENT_RTTI(ULevel, UObject)

ULevel::~ULevel()
{
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	Actors.clear();


}


FCamera* ULevel::GetCamera() const
{
	UWorld* World = GetTypedOuter<UWorld>();
	return World ? World->GetCamera() : nullptr;
}

EWorldType ULevel::GetWorldType() const
{
	UWorld* World = GetTypedOuter<UWorld>();
	return World ? World->GetWorldType() : EWorldType::Game;
}

bool ULevel::IsEditorLevel() const
{
	return GetWorldType() == EWorldType::Editor;
}

bool ULevel::IsGameLevel() const
{
	const EWorldType WorldType = GetWorldType();
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}



void ULevel::ClearActors()
{


	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	Actors.clear();

	bBegunPlay = false;
}

void ULevel::RegisterActor(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	const auto It = std::find(Actors.begin(), Actors.end(), InActor);
	if (It != Actors.end())
	{
		return;
	}

	Actors.push_back(InActor);
	InActor->SetLevel(this);
}

void ULevel::DestroyActor(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}


	InActor->Destroy();
}

void ULevel::CleanupDestroyedActors()
{
	const auto NewEnd = std::ranges::remove_if(Actors,
		[](const AActor* Actor)
		{
			return Actor == nullptr || Actor->IsPendingDestroy();
		}).begin();

	Actors.erase(NewEnd, Actors.end());
}

void ULevel::BeginPlay()
{
	if (bBegunPlay)
	{
		return;
	}

	bBegunPlay = true;

	for (AActor* Actor : Actors)
	{
		if (Actor && !Actor->HasBegunPlay())
		{
			Actor->BeginPlay();
		}
	}
}

void ULevel::Tick(float DeltaTime)
{
	if (!bBegunPlay)
	{
		BeginPlay();
	}

	for (AActor* Actor : Actors)
	{
		if (Actor && !Actor->IsPendingDestroy())
		{
			Actor->Tick(DeltaTime);
		}
	}

	CleanupDestroyedActors();
}

void ULevel::DuplicateSubObjects(FDuplicateionContext& Context)
{
	UObject::DuplicateSubObjects(Context);

	TArray<AActor*> OldActors = this->Actors;
	this->Actors.clear();

	for (AActor* OldActor : OldActors)
	{
		if (OldActor)
		{
			AActor* NewActor = static_cast<AActor*>(OldActor->Duplicate(Context, this));
			this->RegisterActor(NewActor);
		}
	}
}
