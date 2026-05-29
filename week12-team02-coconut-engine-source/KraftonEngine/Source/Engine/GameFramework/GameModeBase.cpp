#include "GameFramework/GameModeBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/World.h"
#include "Object/UClass.h"
#include "Core/Log.h"
#include "Core/ProjectSettings.h"

AGameModeBase::AGameModeBase()
{
	// 기본값 — 서브클래스 생성자가 더 구체 클래스로 덮어쓸 수 있다.
	GameStateClass = AGameStateBase::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
	// TODO: Object Reflection이 된다면 이걸 에디터에서 설정하게 만들면 참 좋은데
	// 지금은 무리니 상속받은 코드에서 static class를 설정하게 만들어야함
	DefaultPawnClass = ACharacter::StaticClass();
}

void AGameModeBase::BeginPlay()
{
	AActor::BeginPlay();

	// GameState spawn — World 경유로 등록되어 BeginPlay/Tick에 편입된다.
	if (UWorld* World = GetWorld())
	{
		UClass* StateClass = GameStateClass ? GameStateClass : AGameStateBase::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(StateClass);
		GameState = Cast<AGameStateBase>(Spawned);
	}
}

void AGameModeBase::EndPlay()
{
	GameState = nullptr;
	PlayerController = nullptr;
	AActor::EndPlay();
}

void AGameModeBase::StartMatch()
{
	// PlayerController spawn — Editor 월드에선 GameMode 자체가 안 만들어지므로 안전.
	if (UWorld* World = GetWorld())
	{
		UClass* PCClass = PlayerControllerClass ? PlayerControllerClass : APlayerController::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(PCClass);
		PlayerController = Cast<APlayerController>(Spawned);
	}

	if (!PlayerController)
	{
		return;
	}
	
	if (APawn* ExistingPawn = FindAutoPossessPawn())
	{
		PlayerController->Possess(ExistingPawn);
		UE_LOG("[GameMode] Auto-possessed Pawn: %s", ExistingPawn->GetName().c_str());
		return;
	}

	if (APawn* SpawnedPawn = SpawnDefaultPawn())
	{
		PlayerController->Possess(SpawnedPawn);
		UE_LOG("[GameMode] Spawned and possessed default Pawn: %s", SpawnedPawn->GetName().c_str());
		return;
	}

	UE_LOG("[GameMode] No Pawn to possess. DefaultPawnClass is null or spawn failed.");
}

void AGameModeBase::EndMatch()
{
	if (PlayerController)
	{
		PlayerController->UnPossess();
	}
}

UClass* AGameModeBase::ResolveClassFromProjectSettings(UClass* InDefault)
{
	UClass* Result = InDefault;
	const FString& ConfiguredName = FProjectSettings::Get().Game.GameModeClassName;
	if (ConfiguredName.empty())
	{
		return Result;
	}

	UClass* Found = UClass::FindByName(ConfiguredName.c_str());
	if (Found && Found->IsChildOf(AGameModeBase::StaticClass()))
	{
		return Found;
	}

	UE_LOG("[GameMode] GameModeClassName '%s' not found or not a AGameModeBase subclass — using default %s",
		ConfiguredName.c_str(), Result ? Result->GetName() : "(null)");
	return Result;
}

APawn* AGameModeBase::FindAutoPossessPawn() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor) continue;
		APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn) continue;
		if (!Pawn->GetAutoPossessPlayer()) continue;

		return Pawn;
	}

	return nullptr;
}

APawn* AGameModeBase::SpawnDefaultPawn()
{
	if (!DefaultPawnClass)
	{
		return nullptr;
	}

	if (!DefaultPawnClass->IsChildOf(APawn::StaticClass()))
	{
		UE_LOG("[GameMode] DefaultPawnClass is not an APawn subclass.");
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AActor* Spawned = World->SpawnActorByClass(DefaultPawnClass);
	APawn* Pawn = Cast<APawn>(Spawned);

	if (!Pawn)
	{
		UE_LOG("[GameMode] DefaultPawnClass is not an APawn subclass.");
		return nullptr;
	}

	return Pawn;
}
