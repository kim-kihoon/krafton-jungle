#pragma once

#include "EngineTypes.h"
#include <functional>

class UObject;

struct FObjectFactory
{
private:
	using ConstructorFunc = std::function<UObject* ()>;
public:
	static UObject* ConstructObject(const void* id);

	static void RegisterObjectType(const void* id, ConstructorFunc func);

private:
	static TMap<const void*, ConstructorFunc>& GetRegistry()
	{
		static TMap<const void*, ConstructorFunc> registry;
		return registry;
	}
};

