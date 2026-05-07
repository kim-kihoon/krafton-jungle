#pragma once

#include "Game/Systems/CleaningGameTypes.h"

class FEndingSystem
{
public:
	static FEndingSystem& Get();

	FEndingResult EvaluateEnding() const;
	FString BuildEndingIdFromContext() const;

private:
	FEndingSystem() = default;
};
