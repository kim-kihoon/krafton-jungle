#include "GameFramework/GameMode/GameplayStatics.h"

#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Particle/ParticleSystemManager.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectFactory.h"

AActor* FGameplayStatics::FindFirstActorByTag(const UWorld* World, const FName& Tag)
{
	if (!World) return nullptr;
	for (AActor* Actor : World->GetActors())
	{
		if (Actor && Actor->HasTag(Tag))
		{
			return Actor;
		}
	}
	return nullptr;
}

TArray<AActor*> FGameplayStatics::FindActorsByTag(const UWorld* World, const FName& Tag)
{
	TArray<AActor*> Result;
	if (!World) return Result;
	for (AActor* Actor : World->GetActors())
	{
		if (Actor && Actor->HasTag(Tag))
		{
			Result.push_back(Actor);
		}
	}
	return Result;
}

UParticleSystemComponent* FGameplayStatics::SpawnEmitterAtLocation(UWorld* World, UParticleSystem* Template,
	const FVector& Location, const FRotator& Rotation, bool bActivate)
{
	if (!World || !Template) return nullptr;

	// 에디터 배치(FLevelViewportLayout)와 동일 패턴: Create → InitDefaultComponents → 세팅 → AddActor.
	UObject* Created = FObjectFactory::Get().Create(AParticleSystemActor::StaticClass()->GetName(), World);
	AParticleSystemActor* Actor = Cast<AParticleSystemActor>(Created);
	if (!Actor) return nullptr;

	Actor->InitDefaultComponents();   // PSC 생성 + root 지정 (데모용 빈 PS 는 아래에서 교체)
	UParticleSystemComponent* PSC = Actor->GetParticleSystemComponent();
	if (!PSC)
	{
		World->DestroyActor(Actor);
		return nullptr;
	}

	PSC->SetTemplate(Template);
	PSC->RebuildInstances(true);

	Actor->SetActorLocation(Location);
	Actor->SetActorRotation(Rotation);

	World->AddActor(Actor);           // world 가 BeginPlay 진행 중이면 여기서 BeginPlay → 컴포넌트 등록

	if (bActivate) PSC->Activate(true);
	return PSC;
}

UParticleSystemComponent* FGameplayStatics::SpawnEmitterAtLocation(UWorld* World, const FString& TemplatePath,
	const FVector& Location, const FRotator& Rotation, bool bActivate)
{
	if (TemplatePath.empty() || TemplatePath == "None") return nullptr;
	UParticleSystem* Template = FParticleSystemManager::Get().Load(TemplatePath);
	return SpawnEmitterAtLocation(World, Template, Location, Rotation, bActivate);
}
