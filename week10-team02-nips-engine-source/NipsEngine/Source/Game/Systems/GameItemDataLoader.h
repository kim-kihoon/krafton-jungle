#pragma once

#include "Engine/Core/Containers/String.h"

class FItemSystem;

class FGameItemDataLoader
{
public:
	static bool LoadFromFile(const FString& RelativePath, FItemSystem& ItemSystem);
};
