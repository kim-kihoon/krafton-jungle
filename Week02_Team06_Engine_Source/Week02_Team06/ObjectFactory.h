#pragma once
#include "Object.h"
#include "Memory.h"

class UEngineStatics;

class UObjectFactory
{
public:
	template<typename T>
	static T* NewObject()
	{
		static_assert(std::is_base_of_v<UObject, T>, "T must derive from UObject");

		T* Obj             = new T;
		if (!UEngineStatics::bIsLoading)
		{
			Obj->UUID = UEngineStatics::GetUUID();
		}
		Obj->internalIndex = GUObjectArray.Size();
		GUObjectArray.PushBack(Obj);

		return Obj;
	}

	static void DestroyObject(UObject* Obj);
	static void DestroyAllObjects();
};

template<typename T>
T* NewObject()
{
	return UObjectFactory::NewObject<T>();
}
