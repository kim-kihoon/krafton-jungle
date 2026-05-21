#pragma once

#include "Core/Singleton.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/String.h"

class UClass;

class FReflectionRegistry : public TSingleton<FReflectionRegistry>
{
    friend class TSingleton<FReflectionRegistry>;

public:
    void RegisterUClass(UClass* Class);
    UClass* FindClass(const FString& ClassName) const;

private:
    TMap<FString, UClass*> Classes;
};
