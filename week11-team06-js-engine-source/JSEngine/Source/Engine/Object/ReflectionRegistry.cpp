#include "ReflectionRegistry.h"
#include "Object/Class.h"

void FReflectionRegistry::RegisterUClass(UClass* Class)
{
    if (!Class || !Class->GetName())
    {
        return;
    }

    const FString Name = Class->GetName();

    auto It = Classes.find(Name);
    if (It != Classes.end())
    {
        if (It->second == Class)
        {
            return;
        }

        return;
    }

    Classes[Name] = Class;
}

UClass* FReflectionRegistry::FindClass(const FString& ClassName) const
{
    if (ClassName.empty())
    {
        return nullptr;
    }

    auto It = Classes.find(ClassName);
    return It != Classes.end() ? It->second : nullptr;
}
