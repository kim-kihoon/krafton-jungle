#pragma once

#include "CoreMinimal.h"
#include "Scene/WorldTypes.h"

class AActor;
class ULevel;

struct ENGINE_API FLevelContext
{
	FString ContextName;
	EWorldType WorldType = EWorldType::Game;
	ULevel* Level = nullptr;

	bool IsValid() const { return Level != nullptr; }
	void Reset()
	{
		ContextName.clear();
		WorldType = EWorldType::Game;
		Level = nullptr;
	}
};

struct ENGINE_API FEditorLevelContext : public FLevelContext
{
	void Reset()
	{
		FLevelContext::Reset();
	}
};
