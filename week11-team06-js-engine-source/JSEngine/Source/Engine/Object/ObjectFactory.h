#pragma once

#include <functional>
#include <utility>
#include "Object/Object.h"
#include "Object/ReflectionRegistry.h"
#include "Core/Singleton.h"

#define REGISTER_FACTORY(TypeName)															\
namespace {																					\
	 struct TypeName##_RegisterFactory {													\
		TypeName##_RegisterFactory() {														\
				FObjectFactory::Get().Register(												\
					#TypeName,																\
					[]()->UObject* {return UObjectManager::Get().CreateObject<TypeName>();},\
					&TypeName::s_TypeInfo													\
				);																			\
		}																					\
	};																						\
TypeName##_RegisterFactory G##TypeName##_RegisterFactory;} 																												

struct FObjectFactoryEntry
{
	std::function<UObject*()> Spawner;
	const FTypeInfo* TypeInfo = nullptr;
};

// Different from UFactory class
class FObjectFactory : public TSingleton<FObjectFactory>
{
	friend class TSingleton<FObjectFactory>;

public:
	void Register(const char* TypeName, std::function<UObject*()> Spawner, const FTypeInfo* TypeInfo = nullptr) {
		Registry[TypeName] = FObjectFactoryEntry{ std::move(Spawner), TypeInfo };
	}

	UObject* Create(const std::string& TypeName) {
        if (UClass* Class = FReflectionRegistry::Get().FindClass(FString(TypeName)))
        {
            if (UObject* Object = Class->CreateObject())
            {
                return Object;
            }
        }

        auto Spawner = Registry.find(TypeName);
        return (Spawner != Registry.end() && Spawner->second.Spawner)
                   ? Spawner->second.Spawner()
                   : nullptr;	
	}

	void GetRegisteredTypeInfos(TArray<const FTypeInfo*>& OutTypes) const {
		for (const auto& [TypeName, Entry] : Registry)
		{
			if (Entry.TypeInfo)
			{
				OutTypes.push_back(Entry.TypeInfo);
			}
		}
	}

private:
	TMap<std::string, FObjectFactoryEntry> Registry;
};
